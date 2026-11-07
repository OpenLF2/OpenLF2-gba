// SPDX-License-Identifier: MIT
// Runs the fighting simulation on the PC with every fighter computer-controlled, checking for broken invariants.
//   sim_host [ticks] [fighters] [seed] [roster index of first fighter]
#include "../../game/world.hpp"
#include "assets.h"
#include <cstdio>
#include <cstdlib>

using namespace lf2;

int main(int argc, char** argv) {
    const int ticks = argc > 1 ? atoi(argv[1]) : 3000;
    const int count = argc > 2 ? atoi(argv[2]) : 4;
    const unsigned seed = argc > 3 ? unsigned(atoi(argv[3])) : 1;
    const int first = argc > 4 ? atoi(argv[4]) : 0;
    static World w;
    Stage s;
    s.width = 1600; s.z1 = 360; s.z2 = 500;
    w.init(s, seed);
    for (int i = 0; i < count; ++i) w.add_fighter((first + i) % ROSTER_COUNT, i + 1, false, 60);
    w.place_fighters();
    int hits = 0, last_hp[8] = {};
    for (int i = 0; i < count; ++i) last_hp[i] = w.ents[i].hp;
    int spawned_max = 0, pf[8] = {}, pf2[8] = {}, stuck[8] = {};
    for (int t = 0; t < ticks; ++t) {
        w.tick();
        int live = 0;
        for (int i = 0; i < max_ents; ++i) {
            const Ent& e = w.ents[i];
            if (!e.live) continue;
            ++live;
            if (e.frame < 0 || e.frame >= e.def->hdr->nframes || !e.def->defined(e.frame)) {
                printf("tick %d: ent %d (obj %d) in bad frame %d\n", t, i, e.def->hdr->id, e.frame);
                return 1;
            }
            if (e.y > 256 * 60 || e.y < -256 * 700 || e.x < -256 * 2000 || e.x > 256 * 5000) {
                printf("tick %d: ent %d (obj %d) out of range x=%d y=%d z=%d frame %d\n", t, i, e.def->hdr->id,
                       e.x / 256, e.y / 256, e.z / 256, e.frame);
                return 1;
            }
        }
        if (getenv("TRACE")) for (int i = 0; i < count; ++i) {
            const Ent& e = w.ents[i];
            if (e.frame != pf[i] && (e.frame >= 180 && e.frame < 210)) printf("t=%d fighter %d: frame %d -> %d  y=%d vy=%d hp=%d lag=%d fall=%d\n", t, i, pf[i], e.frame, e.y / 256, e.vy / 256, e.hp, e.hit_lag, e.fall);
            pf[i] = e.frame;
        }
        for (int i = 0; i < count; ++i) {
            const Ent& e = w.ents[i];
            if (e.frame == pf2[i] && e.hp > 0) ++stuck[i]; else { stuck[i] = 0; pf2[i] = e.frame; }
            const int st = e.def->frame(e.frame).state;
            if (stuck[i] == 400 && st != 0 && st != 14) printf("t=%d: fighter %d (%s) stuck in frame %d state %d (y=%d vy=%d)\n", t, i, ROSTER_NAME[(first + i) % ROSTER_COUNT], e.frame, st, e.y / 256, e.vy / 256);
        }
        if (live > spawned_max) spawned_max = live;
        for (int i = 0; i < count; ++i) if (w.ents[i].hp < last_hp[i]) { ++hits; last_hp[i] = w.ents[i].hp; } else last_hp[i] = w.ents[i].hp;
        if (t % 300 == 0) {
            printf("t=%5d cam=%4d live=%2d |", t, w.camera, live);
            for (int i = 0; i < count; ++i) printf(" %s hp%3d mp%3d f%3d", ROSTER_NAME[(first + i) % ROSTER_COUNT], w.ents[i].hp, w.ents[i].mp, w.ents[i].frame);
            printf("\n");
        }
        if (w.finished) { printf("finished at tick %d, winner team %d\n", t, w.winner); break; }
    }
    printf("hits %d, max live ents %d\n", hits, spawned_max);
    return 0;
}
