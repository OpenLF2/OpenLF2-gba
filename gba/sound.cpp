// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// Software mixer for the GBA's direct sound channel A.
//
// The sounds are 8-bit samples at the mixer's rate. Each frame sound_update() adds up
// the active voices into one of two 224-sample buffers; 224 samples at one per 1254 cycles is
// exactly one frame (280896 cycles). A vertical-blank interrupt then restarts the DMA that feeds
// the sound FIFO on the buffer mixed during the previous frame (or on silence if there is none),
// so playback stays in step with the display even when the main loop is late.

#include "sound.hpp"
#include "assets.h"
#include "hw.hpp"

namespace {
constexpr int buffer_size = 224;
constexpr int voice_count = 4;
constexpr std::uint32_t fifo_a = 0x040000A0;
// DMA: enabled, start on sound request, repeat, 32-bit words, destination fixed.
constexpr std::uint32_t dma_control = 0x80000000u | 0x30000000u | 0x02000000u | 0x04000000u | 0x00400000u;

alignas(4) std::int8_t buffers[2][buffer_size];
alignas(4) std::int8_t silence[buffer_size];
volatile int ready_buffer = -1;          // buffer to play at the next vertical blank, or -1 for silence
int mix_buffer = 0;

struct Voice {
    const std::int8_t* data = nullptr;
    std::uint32_t position = 0;          // sample index into data
    std::uint32_t length = 0;            // in samples
};
Voice voices[voice_count];

// Background music: IMA ADPCM, 256 byte blocks of 505 samples at the mixer's rate.
constexpr int block_bytes = 256, block_samples = 505;
// Not const: the tables live in IWRAM, where the mixer reads them without ROM wait states.
std::int8_t index_table[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
std::int16_t step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};
struct Music {
    const std::uint8_t* data = nullptr;
    std::uint32_t blocks = 0, block = 0;
    int sample = 0;                       // index inside the block
    int predictor = 0, index = 0;
} music;

}

// Runs at every vertical blank: starts the audio DMA again so that the buffer plays in step with the frame.
extern "C" IWRAM_CODE void sound_vblank_handler() {
    REG_IF = 1;
    BIOS_IRQ_FLAGS = BIOS_IRQ_FLAGS | 1;
    const std::int8_t* source = ready_buffer >= 0 ? buffers[ready_buffer] : silence;
    ready_buffer = -1;
    REG_DMA1CNT = 0;
    REG_TM0CNT_H = 0;
    REG_SOUNDCNT_H = REG_SOUNDCNT_H | 0x0800;           // clear the FIFO
    REG_DMA1SAD = reinterpret_cast<std::uint32_t>(source);
    REG_DMA1DAD = fifo_a;
    REG_DMA1CNT = dma_control;
    REG_TM0CNT_L = std::uint16_t(65536 - 1254);
    REG_TM0CNT_H = 0x80;                                  // enable, prescaler 1
}

void sound_init() {
    REG_SOUNDCNT_X = 0x80;      // master enable
    // Channel A: full volume, left and right, timer 0.
    REG_SOUNDCNT_H = 0x0304;
    REG_IME = 0;
    IRQ_HANDLER = reinterpret_cast<std::uint32_t>(&sound_vblank_handler);
    REG_DISPSTAT = REG_DISPSTAT | 0x0008;               // vertical blank interrupt on
    REG_IE = REG_IE | 1;
    REG_IME = 1;
}

void sound_play(int id) {
    if (id <= 0 || id >= SOUND_COUNT || SOUND_LENGTH[id] == 0) return;
    // A free voice, otherwise the one with the least left to play.
    int chosen = 0;
    std::uint32_t least = 0xFFFFFFFFu;
    for (int i = 0; i < voice_count; ++i) {
        const std::uint32_t left = voices[i].data ? voices[i].length - voices[i].position : 0;
        if (left < least) { least = left; chosen = i; }
    }
    Voice& voice = voices[chosen];
    voice.data = reinterpret_cast<const std::int8_t*>(sounds_bin + SOUND_OFFSET[id]);
    voice.position = 0;
    voice.length = SOUND_LENGTH[id];
}

void music_play(int track) {
    if (track < 0 || track >= MUSIC_COUNT || MUSIC_BLOCKS[track] == 0) { music.data = nullptr; return; }
    music.data = music_bin + MUSIC_OFFSET[track];
    music.blocks = MUSIC_BLOCKS[track];
    music.block = 0;
    music.sample = 0;
}

void music_stop() { music.data = nullptr; }

// Mixes one frame. It runs from IWRAM as ARM code and keeps the decoder state in registers: it is the
// most expensive routine of a frame.
IWRAM_CODE void sound_update() {
    bool any = music.data != nullptr;
    for (const auto& voice : voices) any |= voice.data != nullptr;
    if (!any) return;
    int sums[buffer_size];
    if (music.data) {
        const std::uint8_t* block = music.data + music.block * block_bytes;
        int sample = music.sample, predictor = music.predictor, index = music.index;
        for (int i = 0; i < buffer_size; ++i) {
            int value;
            if (sample == 0) {
                predictor = std::int16_t(block[0] | (block[1] << 8));
                index = block[2] > 88 ? 88 : block[2];
                sample = 1;
                value = predictor;
            } else {
                const int n = sample - 1;
                const int byte = block[4 + (n >> 1)];
                const int code = (n & 1) ? byte >> 4 : byte & 15;
                const int step = step_table[index];
                int diff = ((2 * (code & 7) + 1) * step) >> 3;
                if (code & 8) diff = -diff;
                value = predictor + diff;
                if (value > 32767) value = 32767;
                if (value < -32768) value = -32768;
                predictor = value;
                index += index_table[code & 7];
                index = index < 0 ? 0 : index > 88 ? 88 : index;
                if (++sample >= block_samples) {
                    sample = 0;
                    if (++music.block >= music.blocks) music.block = 0;
                    block = music.data + music.block * block_bytes;
                }
            }
            sums[i] = value >> 9;
        }
        music.sample = sample; music.predictor = predictor; music.index = index;
    } else {
        for (int i = 0; i < buffer_size; ++i) sums[i] = 0;
    }
    for (auto& voice : voices) {
        if (!voice.data) continue;
        if (voice.position >= voice.length) { voice.data = nullptr; continue; }
        std::uint32_t count = voice.length - voice.position;
        if (count > std::uint32_t(buffer_size)) count = buffer_size;
        const std::int8_t* in = voice.data + voice.position;
        for (std::uint32_t i = 0; i < count; ++i) sums[i] += in[i];
        voice.position += count;
    }
    std::int8_t* out = buffers[mix_buffer];
    for (int i = 0; i < buffer_size; ++i) {
        int sum = sums[i];
        sum = sum - sum / 4;    // leave some room when several sounds overlap
        out[i] = std::int8_t(sum > 127 ? 127 : sum < -128 ? -128 : sum);
    }
    ready_buffer = mix_buffer;
    mix_buffer ^= 1;
}
