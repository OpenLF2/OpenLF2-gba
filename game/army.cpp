// SPDX-License-Identifier: MIT
#include "army.hpp"
namespace lf2 {
const int army_ids[army_types] = {30,31,33,34,39,32,35,36,37,122,123};
const int army_hp[army_types] = {50,50,150,100,150,200,200,250,200,200,500};
namespace {
// Original preset tables: balanced, inferior, ranged, melee and giant.
constexpr int totals[5][army_types] = {
    {20,20,8,8,8,2,2,3,2,10,10}, {42,42,2,8,0,0,0,3,0,10,10},
    {0,20,12,12,0,0,8,3,0,10,10}, {20,0,4,0,10,8,0,3,6,10,10},
    {0,0,0,0,0,9,8,3,8,10,10},
};
constexpr int first[5][army_types] = {
    {7,7,4,4,4,1,1,1,1,3,3}, {20,20,1,4,0,0,0,1,0,3,3},
    {0,10,6,6,0,0,4,1,1,3,3}, {10,0,2,0,5,4,0,1,3,3,3},
    {0,0,0,0,0,6,4,1,4,3,3},
};
}
void ArmyRun::preset(int side, int strategy, int size) {
    if (side < 0 || side > 1) return;
    if (strategy < 0) strategy = 0;
    if (strategy > 6) strategy = 6;
    if (size < 1) size = 1;
    if (size > 3) size = 3;
    for (int i = 0; i < army_types; ++i) {
        int total = strategy == 5 ? 0 : strategy == 6 ? (i == 7 ? 16 : i >= 9 ? 33 : 32) : totals[strategy][i] * size / 3;
        int initial = strategy == 5 ? 0 : strategy == 6 ? (i == 7 ? 1 : i >= 9 ? 3 : 2) : first[strategy][i] * size / 3;
        if (initial < 1 && total > 0) initial = 1;
        on_field[side][i] = initial;
        stock[side][i] = total < initial ? initial : total;
    }
    over = false; winner = 0;
}
void ArmyRun::configure(int side, const int* first_count, const int* reserve_count) {
    if (side < 0 || side > 1 || !first_count || !reserve_count) return;
    for (int i = 0; i < army_types; ++i) {
        const int first = first_count[i] > 0 ? first_count[i] : 0;
        const int reserve = reserve_count[i] > 0 ? reserve_count[i] : 0;
        on_field[side][i] = first;
        stock[side][i] = first > 0 ? first + reserve : 0;
    }
    over = false; winner = 0;
}
int ArmyRun::reserves(int side) const {
    if (side < 0 || side > 1) return 0;
    int count = 0;
    for (int i = 0; i < 9; ++i) count += stock[side][i];
    return count;
}
int ArmyRun::result(int a, int b) {
    if (a > 0 && b > 0) return -1;
    return a > 0 ? 1 : b > 0 ? 2 : 0;
}
void ArmyRun::tick(const ArmyHooks& hooks) {
    if (!hooks.census || !hooks.living || !hooks.spawn) return;
    // The reference counts living fighters before adding this tick's reinforcements.
    const int a = hooks.living(hooks.context, 0), b = hooks.living(hooks.context, 1);
    for (int side = 0; side < 2; ++side) {
        for (int kind = 0; kind < army_types; ++kind) {
            if (stock[side][kind] > 0 && hooks.census(hooks.context, side, kind) < on_field[side][kind] &&
                hooks.spawn(hooks.context, side, kind, army_ids[kind], army_hp[kind])) --stock[side][kind];
        }
    }
    over = !((reserves(0) > 0 && reserves(1) > 0) || (a > 0 && b > 0));
    winner = over ? result(a, b) : 0;
}
}  // namespace lf2
