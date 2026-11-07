// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once

void sound_init();
// Starts the sound effect `id` (the number of the original's data/NNN.wav, or SOUND_MENU_*).
void sound_play(int id);
// Mixes one video frame of audio; call once per frame.
void sound_update();

// Background music: a looping track from assets.h (MUSIC_*), or music_stop().
void music_play(int track);
void music_stop();
