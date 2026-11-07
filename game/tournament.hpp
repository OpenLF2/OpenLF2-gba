// SPDX-License-Identifier: MIT
// Eight entrants in a single-elimination individual or two-on-two championship.
#pragma once
#include <cstdint>
namespace lf2 {
struct TournamentEntrant {
    int fighter = -1;      // roster index
    int original_id = 0;   // original object id, used for computer-only results
    int hp = 500;
    bool human = false;
    int transform_id = -1; // learned transformation target, retained between matches
};
struct TournamentMatch {
    int count = 0;
    int entrants[4] = {-1,-1,-1,-1}; // side 1 followed by side 2
};
class Tournament {
public:
    TournamentEntrant entrants[8];
    void start(bool team, std::uint32_t seed, int difficulty = 1);
    TournamentMatch current() const;
    // Health is the recoverable health at the end of the match, in current() order.
    void result(int winner, const int* dark_hp, const bool* alive);
    // Resolve a match containing no human entrants using the original boss preference and health loss.
    bool simulate();
    bool finished() const { return remaining_ == 1; }
    int champion() const { return finished() ? active_[0] : -1; }
    int round() const { return round_; }
    bool team() const { return team_; }
private:
    int active_[8] = {}, next_[4] = {};
    int remaining_ = 0, match_ = 0, round_ = 0;
    bool team_ = false;
    int difficulty_ = 1;
    std::uint32_t seed_ = 1;
    int random(int max);
};
}  // namespace lf2
