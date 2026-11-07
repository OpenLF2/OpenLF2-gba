// SPDX-License-Identifier: MIT
// Scripted stage encounter scheduling. Entity creation and lifetime are supplied by the caller.
#pragma once
#include "stage_data.hpp"

namespace lf2 {
constexpr int stage_max_entries = 60;
constexpr int stage_max_parallel = 40;

struct StageHooks {
    void* context;
    // Return an entity handle, -1 when no slot is available, or -2 for a spawned nonblocking item.
    int (*spawn)(void*, const StageEntry&);
    bool (*alive)(void*, int);
    void (*phase_started)(void*, const StagePhase&);
    bool (*blocks)(void*, int) = nullptr; // a live prisoner or recruited ally does not hold a phase open
    bool (*enemies_left)(void*) = nullptr; // includes enemy characters spawned outside the script
    void (*clear_reservations)(void*) = nullptr;
    void (*reserve)(void*, int) = nullptr; // keep boss slots from being reused during this update
};

class StageRun {
public:
    void start(const StageScript& script, const StagePhase* phases, const StageEntry* entries, int players);
    void tick(const StageHooks& hooks);
    void set_players(int players) { players_ = players > 0 ? players : 1; }
    int phase() const { return phase_; }
    bool next_bound_same() const;
    bool finished() const { return finished_; }
private:
    struct EntryRun {
        int total = 0, spawned = 0, parallel = 0;
        bool initial = true;
        std::int8_t entities[stage_max_parallel] = {}; // entity slots fit in a signed byte
    };
    const StageScript* script_ = nullptr;
    const StagePhase* phases_ = nullptr;
    const StageEntry* entries_ = nullptr;
    EntryRun runs_[stage_max_entries];
    int players_ = 1, phase_ = -1;
    bool finished_ = false;
    void enter_phase(int phase, const StageHooks& hooks);
};
}  // namespace lf2
