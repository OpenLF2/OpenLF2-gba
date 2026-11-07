// SPDX-License-Identifier: MIT
#include "tournament.hpp"
namespace lf2 {
int Tournament::random(int max) {
    if (max <= 0) return 0;
    seed_ = seed_ * 1664525u + 1013904223u;
    return int((seed_ >> 8) % unsigned(max));
}
void Tournament::start(bool team, std::uint32_t seed, int difficulty) {
    team_ = team; difficulty_ = difficulty < -1 ? -1 : difficulty > 2 ? 2 : difficulty;
    seed_ = seed ? seed : 1; round_ = match_ = 0;
    remaining_ = team ? 4 : 8;
    for (int i=0; i<remaining_; ++i) active_[i] = i;
    for (int i=remaining_-1; i>0; --i) {
        int j=random(i+1), tmp=active_[i]; active_[i]=active_[j]; active_[j]=tmp;
    }
}
TournamentMatch Tournament::current() const {
    TournamentMatch m;
    if (remaining_ <= 1 || match_ >= remaining_ / 2) return m;
    const int first=active_[match_*2], second=active_[match_*2+1];
    if (team_) { m.count=4; m.entrants[0]=first*2; m.entrants[1]=first*2+1; m.entrants[2]=second*2; m.entrants[3]=second*2+1; }
    else { m.count=2; m.entrants[0]=first; m.entrants[1]=second; }
    return m;
}
void Tournament::result(int winner, const int* dark_hp, const bool* alive) {
    const TournamentMatch m=current();
    if (!m.count || !dark_hp || !alive) return;
    if (winner != 1 && winner != 2) winner=random(2)+1;
    for (int i=0; i<m.count; ++i) {
        int health=dark_hp[i];
        if (team_ && !alive[i]) health /= 2;
        entrants[m.entrants[i]].hp=health;
    }
    next_[match_] = active_[match_*2 + winner-1];
    if (++match_ == remaining_/2) {
        remaining_ /= 2;
        for (int i=0; i<remaining_; ++i) active_[i]=next_[i];
        match_=0; ++round_;
    }
}
bool Tournament::simulate() {
    const TournamentMatch m=current();
    if (!m.count) return false;
    for (int i=0; i<m.count; ++i) if (entrants[m.entrants[i]].human) return false;
    const int half=m.count/2;
    bool boss[2] = {};
    for (int i=0; i<m.count; ++i) if (entrants[m.entrants[i]].original_id / 10 == 5) boss[i/half]=true;
    const int winner=boss[0] != boss[1] ? (team_ ? (boss[0] ? 1 : 2) : (boss[0] ? 2 : 1)) : random(2)+1;
    int hp[4] = {}; bool alive[4] = {};
    for (int i=0; i<m.count; ++i) {
        hp[i]=entrants[m.entrants[i]].hp;
        alive[i]=true;
        if (i/half == winner-1) {
            const int factor = team_ ? 18 : 12;
            int spread = difficulty_ * factor + 15;
            hp[i] = hp[i] * (85 - difficulty_ * factor + random(spread)) / 100;
        }
    }
    result(winner,hp,alive);
    return true;
}
}  // namespace lf2
