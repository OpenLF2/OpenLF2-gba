// SPDX-License-Identifier: MIT
#include "stage_run.hpp"

namespace lf2 {
bool StageRun::next_bound_same() const {
    if (!script_ || phase_ < 0 || phase_ >= script_->phase_count) return false;
    const StagePhase& current = phases_[script_->first_phase + phase_];
    const int next = current.next_phase >= 0 ? current.next_phase : phase_ + 1;
    return next < script_->phase_count && current.bound == phases_[script_->first_phase + next].bound;
}

void StageRun::start(const StageScript& script, const StagePhase* phases, const StageEntry* entries, int players) {
    script_ = &script;
    phases_ = phases;
    entries_ = entries;
    players_ = players > 0 ? players : 1;
    phase_ = -1;
    finished_ = script.first_phase < 0 || script.phase_count <= 0;
    for (auto& run : runs_) run = EntryRun{};
}

void StageRun::enter_phase(int phase, const StageHooks& hooks) {
    if (phase < 0 || phase >= script_->phase_count) { finished_ = true; return; }
    phase_ = phase;
    const StagePhase& definition = phases_[script_->first_phase + phase];
    for (auto& run : runs_) run = EntryRun{};
    const int count = definition.entry_count < stage_max_entries ? definition.entry_count : stage_max_entries;
    for (int i = 0; i < count; ++i) {
        const StageEntry& entry = entries_[definition.first_entry + i];
        EntryRun& run = runs_[i];
        for (auto& handle : run.entities) handle = -1;
        if (entry.id < 0) continue;
        if (entry.ratio <= 0) { run.total = entry.times; run.parallel = 1; }
        else {
            run.total = entry.times * players_ * entry.ratio / 100;
            run.parallel = players_ * entry.ratio / 100;
            if (run.parallel > stage_max_parallel) run.parallel = stage_max_parallel;
        }
    }
    if (hooks.phase_started) hooks.phase_started(hooks.context, definition);
}

void StageRun::tick(const StageHooks& hooks) {
    if (finished_ || !script_ || !hooks.spawn || !hooks.alive) return;
    if (phase_ < 0) enter_phase(0, hooks);
    if (finished_) return;
    const StagePhase& definition = phases_[script_->first_phase + phase_];
    const int count = definition.entry_count < stage_max_entries ? definition.entry_count : stage_max_entries;
    if (hooks.clear_reservations) hooks.clear_reservations(hooks.context);
    if (hooks.reserve) for (int i = 0; i < count; ++i) {
        const StageEntry& entry = entries_[definition.first_entry + i];
        if (entry.kind <= 1) continue;
        const EntryRun& run = runs_[i];
        for (int j = 0; j < run.parallel; ++j)
            if (run.entities[j] >= 0) hooks.reserve(hooks.context, run.entities[j]);
    }
    bool boss_pending = false;
    for (int i = 0; i < count; ++i) {
        EntryRun& run = runs_[i];
        const StageEntry& entry = entries_[definition.first_entry + i];
        for (int j = 0; j < run.parallel; ++j)
            if (run.entities[j] >= 0 && !hooks.alive(hooks.context, run.entities[j])) run.entities[j] = -1;
        if (entry.kind > 1) for (int j = 0; j < run.parallel; ++j)
            if (run.entities[j] >= 0 && (!hooks.blocks || hooks.blocks(hooks.context, run.entities[j]))) boss_pending = true;
    }
    bool pending = false;
    for (int i = 0; i < count; ++i) {
        EntryRun& run = runs_[i];
        const StageEntry& entry = entries_[definition.first_entry + i];
        for (int j = 0; j < run.parallel; ++j) {
            // Cancel replacements only when a soldier slot actually becomes vacant.
            if (run.entities[j] < 0 && entry.kind == 1 && !boss_pending && run.spawned > 0 && !run.initial)
                run.total = run.spawned;
            if (run.entities[j] < 0 && (run.initial || run.spawned < run.total)) {
                const int handle = hooks.spawn(hooks.context, entry);
                if (handle >= 0) { run.entities[j] = handle; ++run.spawned; }
                else if (handle == -2) ++run.spawned;
                if (!run.initial && handle != -1) pending = true;
            }
            if (run.entities[j] >= 0 && (!hooks.blocks || hooks.blocks(hooks.context, run.entities[j]))) pending = true;
            if (run.entities[j] < 0 && run.spawned < run.total) pending = true;
        }
        run.initial = false;
    }
    if (!pending && hooks.enemies_left && hooks.enemies_left(hooks.context)) pending = true;
    if (!pending) {
        const int next = definition.next_phase >= 0 ? definition.next_phase : phase_ + 1;
        if (next >= script_->phase_count) finished_ = true;
        else enter_phase(next, hooks);
    }
}
}  // namespace lf2
