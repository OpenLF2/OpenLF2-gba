// SPDX-License-Identifier: MIT
// Battle mode troop stocks and reinforcement rules.
#pragma once
namespace lf2 {
constexpr int army_types = 11;
extern const int army_ids[army_types];
extern const int army_hp[army_types];

struct ArmyHooks {
    void* context;
    int (*census)(void*, int side, int kind);
    int (*living)(void*, int side);
    bool (*spawn)(void*, int side, int kind, int id, int hp);
};

class ArmyRun {
public:
    int stock[2][army_types] = {};
    int on_field[2][army_types] = {};
    bool over = false;
    int winner = 0;
    void preset(int side, int strategy, int size);
    void configure(int side, const int* first, const int* reserve);
    void tick(const ArmyHooks& hooks);
    int reserves(int side) const;
    static int result(int living_first, int living_second); // -1 while both sides fight, 0 draw, 1/2 winner
};
}  // namespace lf2
