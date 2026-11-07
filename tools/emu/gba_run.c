// SPDX-License-Identifier: MIT
// Headless GBA test runner built on libmgba: runs a ROM, applies scripted key presses and saves
// screenshots as PPM files.
//
//   gba_run ROM.gba FRAMES "frame:keys[,frame:keys...]" "shot_frame[,shot_frame...]" OUTPREFIX
//
// keys is a string of letters held from that frame on: A B S(elect) T(start) R L U D l(eft) r(ight)
// (r = D-pad right, R = R shoulder). "-" releases everything.
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba-util/audio-buffer.h>
#include <mgba/core/interface.h>
#include <mgba-util/image.h>
#include <mgba-util/vfs.h>
#include <mgba/gba/core.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t parse_keys(const char* s) {
    uint32_t k = 0;
    for (; *s; ++s) switch (*s) {
    case 'A': k |= 1 << 0; break;  case 'B': k |= 1 << 1; break;
    case 'S': k |= 1 << 2; break;  case 'T': k |= 1 << 3; break;
    case 'r': k |= 1 << 4; break;  case 'l': k |= 1 << 5; break;
    case 'U': k |= 1 << 6; break;  case 'D': k |= 1 << 7; break;
    case 'R': k |= 1 << 8; break;  case 'L': k |= 1 << 9; break;
    }
    return k;
}

static void quiet_log(struct mLogger* l, int category, enum mLogLevel level, const char* format, va_list args) { (void) l; (void) category; (void) level; (void) format; (void) args; }
static struct mLogger quiet = { .log = quiet_log };

int main(int argc, char** argv) {
    mLogSetDefaultLogger(&quiet);
    if (argc < 6) { fprintf(stderr, "usage\n"); return 2; }
    struct mCore* core = GBACoreCreate();
    if (!core) { fprintf(stderr, "no core for %s\n", argv[1]); return 1; }
    core->init(core);
    mCoreInitConfig(core, NULL);
    unsigned w, h;
    core->baseVideoSize(core, &w, &h);
    uint32_t* buf = malloc(w * h * 4);
    core->setVideoBuffer(core, (mColor*) buf, w);
    core->setAudioBufferSize(core, 1024);
    struct VFile* rom = VFileOpen(argv[1], O_RDONLY);
    if (!rom || !core->loadROM(core, rom)) { fprintf(stderr, "cannot load ROM\n"); return 1; }
    core->reset(core);
    int frames = atoi(argv[2]);
    // key events
    int n_ev = 0; static int ev_frame[65536]; static uint32_t ev_keys[65536];
    char* spec = strdup(argv[3]);
    for (char* t = strtok(spec, ","); t; t = strtok(NULL, ",")) {
        char* c = strchr(t, ':'); if (!c) continue; *c = 0;
        ev_frame[n_ev] = atoi(t); ev_keys[n_ev] = parse_keys(c + 1); ++n_ev;
    }
    int shots[256]; int n_shot = 0;
    char* sspec = strdup(argv[4]);
    for (char* t = strtok(sspec, ","); t; t = strtok(NULL, ",")) shots[n_shot++] = atoi(t);
    FILE* audio = getenv("AUDIO_OUT") ? fopen(getenv("AUDIO_OUT"), "wb") : NULL;
    uint32_t keys = 0; int ev = 0;
    for (int f = 0; f <= frames; ++f) {
        while (ev < n_ev && ev_frame[ev] <= f) keys = ev_keys[ev++];
        core->setKeys(core, keys);
        core->runFrame(core);
        if (audio) {
            struct mAudioBuffer* ab = core->getAudioBuffer(core);
            int16_t tmp[4096];
            size_t n;
            while ((n = mAudioBufferRead(ab, tmp, 2048)) > 0) fwrite(tmp, 4, n, audio);
        }
        for (int i = 0; i < n_shot; ++i) if (shots[i] == f) {
            char name[512]; snprintf(name, sizeof name, "%s_%05d.ppm", argv[5], f);
            FILE* o = fopen(name, "wb"); fprintf(o, "P6\n%u %u\n255\n", w, h);
            for (unsigned p = 0; p < w * h; ++p) { uint32_t c = buf[p]; fputc(c & 255, o); fputc((c >> 8) & 255, o); fputc((c >> 16) & 255, o); }
            fclose(o);
        }
    }
    core->deinit(core);
    return 0;
}
