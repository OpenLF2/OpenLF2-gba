// SPDX-License-Identifier: MIT
// Stage-script table layout generated from the original data/stage.dat.
#pragma once
#include <cstdint>

namespace lf2 {
struct StageEntry {
    std::int16_t id, x, hp, times, reserve, join, join_reserve, act, y, ratio;
    std::int16_t kind; // 0 ordinary, 1 soldier, 2 boss; ratio is in hundredths
};
struct StagePhase {
    std::int16_t bound, next_phase;
    std::uint16_t first_entry, entry_count;
    std::int16_t music;
};
struct StageScript {
    std::int16_t first_phase, phase_count;
};
}  // namespace lf2
