// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// Hits, blocks, knock-downs and the self-steering projectiles. The rules follow OpenLF2's combat.lua and objects.lua.

#include "world.hpp"
#include <initializer_list>
#ifdef SIM_DEBUG
#include <cstdio>
#endif

namespace lf2 {

namespace {
enum State { S_STAND = 0, S_WALK = 1, S_RUN = 2, S_ATTACK = 3, S_JUMP = 4, S_DASH = 5, S_ROW = 6, S_DEFEND = 7,
             S_BROKEN = 8, S_CATCH = 9, S_CAUGHT = 10, S_HURT = 11, S_FALL = 12, S_ICE = 13, S_LYING = 14,
             S_DIZZY = 16, S_BURN = 18, S_BURN_RUN = 19 };
// Sound effects the engine asks for by number: the installer's data/NNN.wav for each fixed effect slot.
constexpr int fixed_sound[18] = {1, 2, 6, 10, 11, 4, 16, 17, 20, 21, 25, 32, 33, 39, 65, 66, 68, 85};
constexpr int zwidth_default = 15;
constexpr int F_JUMP_FRAME = 212;
enum { p_attack_, p_jump_ };

bool ball_like(const Ent& e) {
    const int s = e.def->frame(e.frame).state;
    return e.kind == 3 || (s >= 3000 && s <= 3006);
}
bool breakable_ball(int id) {
    return id == 200 || id == 203 || id == 205 || id == 206 || id == 207 || id == 215 || id == 216;
}
int sgn_facing(const Ent& a) { return a.facing == 0 ? 1 : -1; }
}  // namespace

bool World::overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) const {
    return bx - ax < aw && ax - bx < bw && by - ay < ah && ay - by < bh;
}

void World::body_box(const Ent& e, const Bdy& b, int& x, int& y) const {
    const Frame& f = e.def->frame(e.frame_at_pass);
    const int xi = to_int(e.x), yi = to_int(e.y);
    x = e.facing == 0 ? b.x + xi - f.cx : f.cx + xi - b.w - b.x;
    y = b.y + yi - f.cy;
}

void World::itr_box(const Ent& e, const Itr& i, int& x, int& y) const {
    const Frame& f = e.def->frame(e.frame_at_pass);
    const int xi = to_int(e.x), yi = to_int(e.y);
    x = e.facing == 0 ? i.x + xi - f.cx : f.cx + xi - i.w - i.x;
    y = i.y + yi - f.cy;
}

LF2_HOT bool World::eligible(const Ent& a, const Ent& v, const Itr& itr, const Frame& vf) const {
    const int kind = itr.kind;
    if (kind != 0 && kind != 1 && kind != 2 && kind != 3 && kind != 4 && kind != 5 && kind != 6 && kind != 7 && kind != 8 && kind != 9 && kind != 10 && kind != 11 && kind != 14 && kind != 15 && kind != 16) return false;
    const int effect = itr.effect;
    if (kind != 9 && breakable_ball(a.def->hdr->id) && v.def->hdr->id == 209) return false;
    const int vstate = vf.state;
    const bool v_char = v.kind == 0;
    const bool v_weapon = v.kind == 1 || v.kind == 2 || v.kind == 4 || v.kind == 6;
    const bool column_exception = v.def->hdr->id == 212 &&
        (a.def->hdr->id != 212 || (v.frame % 10 == 5 && a.frame % 10 == 0));
    if ((kind == 1 || kind == 3) && !v_char) return false;
    if (kind == 8 && !v_char) return false;
    if (kind == 5 && a.holder >= 0 && a.holder < max_ents) {
        const Ent& holder = ents[a.holder];
        if (holder.live && holder.team > 0 && holder.team == v.team && vstate != S_ICE && !v_weapon && !column_exception) return false;
    }
    if (kind == 0 && effect == 4 && v_char) return false;
    if (kind == 0 && effect == 20 && (!v_char || v.def->frame(v.state_frame).state == S_BURN || v.def->frame(v.state_frame).state == S_BURN_RUN)) return false;
    if (kind == 0 && effect == 21 && (v.def->frame(v.state_frame).state == S_BURN || v.def->frame(v.state_frame).state == S_BURN_RUN)) return false;
    if (kind == 0 && effect == 30 && v.frame >= 200 && v.frame <= 202) return false;
    if (kind == 0 && effect == 2 && a.def->frame(a.state_frame).state == S_BURN_RUN && v.def->frame(v.state_frame).state == S_BURN) return false;
    if (v.blink != 0 && kind != 8 && kind != 14) return false;
    // Team mates are not hurt, except frozen victims and a ball met head on.
    if (kind != 2 && kind != 4 && kind != 5 && kind != 7 && kind != 8 && kind != 14 && (kind != 9 || v_char) && !v_weapon && vstate != S_ICE && vstate != S_CAUGHT && a.team == v.team && a.team > 0) {
        const bool burning_hit = a.def->frame(a.frame_at_pass).state == S_BURN && effect != 21 && effect != 22;
        if (!column_exception && !burning_hit && !(a.kind == 0 && ball_like(v) && a.facing != v.facing)) return false;
    }
    return true;
}

LF2_HOT void World::detect() {
    // Only live entities take part: age the pair rests, then list what each attacker touches.
    int live[max_ents], live_count = 0;
    for (int i = 0; i < max_ents; ++i) if (ents[i].live) live[live_count++] = i;
    for (int vi = 0; vi < live_count; ++vi) {
        const int v = live[vi];
        for (int ai = 0; ai < live_count; ++ai) {
            const int a = live[ai];
            if (ents[v].pair_rest[a] > 0 && a != v) --ents[v].pair_rest[a];
        }
    }
    for (int li = 0; li < live_count; ++li) {
        Ent& e = ents[live[li]];
        e.frame_at_pass = e.frame;
        const Frame& frame = e.def->frame(e.frame);
        if (frame.nitr == 0 || (frame.state == 1001 && e.holder >= 0 && e.holder < max_ents &&
            ents[e.holder].live && ents[e.holder].def->frame(ents[e.holder].frame).wp_attacking == 0)) e.rest = 0;
    }
    const auto record_targets = [&](int a, int v) {
        Ent& at = ents[a];
        const Frame& af = at.def->frame(at.frame_at_pass);
        if (af.nitr == 0 || at.rest > 0) return;
        Ent& df = ents[v];
        if (df.pair_rest[a] > 0) return;
        const Frame& vf = df.def->frame(df.frame_at_pass);
        if (vf.nbdy == 0) return;
        for (int k = 0; k < af.nitr; ++k) {
            const Itr& itr = at.def->itr[af.itr_off + k];
            if (!eligible(at, df, itr, vf)) continue;
            const int window = itr.zwidth != 0 ? itr.zwidth : zwidth_default;
            const int depth = to_int(at.z) - to_int(df.z);
            if (!(depth < window && depth > -window)) continue;
            int ix, iy;
            itr_box(at, itr, ix, iy);
            for (int b = 0; b < vf.nbdy; ++b) {
                const Bdy& body = df.def->bdy[vf.bdy_off + b];
                if (stage_mode && body.kind >= 1000) {
                    const bool direct = at.team != 2 &&
                        (at.kind == 0 || at.def->hdr->id == 201 || at.def->hdr->id == 202);
                    const bool held = at.holder >= 0 && at.holder < max_ents && ents[at.holder].live &&
                        ents[at.holder].kind == 0 && ents[at.holder].team != 2;
                    if (!direct && !held) continue;
                }
                int bx, by;
                body_box(df, body, bx, by);
                if (!overlap(bx, by, body.w, body.h, ix, iy, itr.w, itr.h)) continue;
                // A resting light item only lists pickups or a whirlwind
                // when touched by a character or a held weapon.
                if (vf.state == 1004 && (at.kind == 0 || at.holder >= 0) &&
                    itr.kind != 2 && itr.kind != 7 && itr.kind != 10) continue;
                // A weak attack passes through a falling victim.
                if (itr.kind != 10 && itr.kind != 11 && vf.state == S_FALL && itr.fall < 41) continue;
                if (itr.kind == 1) {
                    // Only a dizzy victim, walked into with the direction held; the nearest catcher wins.
                    const int distance = to_int(at.x) > to_int(df.x) ? to_int(at.x) - to_int(df.x) : to_int(df.x) - to_int(at.x);
                    bool mine = false;
                    if (distance < df.nearest_catcher || (distance == df.nearest_catcher && rnd(2) == 0)) { df.nearest_catcher = distance; mine = true; }
                    const bool toward = (at.keys.right && to_int(at.x) < to_int(df.x)) || (at.keys.left && to_int(df.x) <= to_int(at.x));
                    if (mine && vf.state == S_DIZZY && toward && at.list_n < 20) {
                        at.list_who[at.list_n] = std::uint8_t(v);
                        at.list_itr[at.list_n] = std::uint8_t(k);
                        ++at.list_n;
                    }
                    continue;
                }
                if (itr.kind == 2 || itr.kind == 7) {
                    const bool fresh = at.keys.attack && !at.prev.attack;
                    const bool pickup = itr.kind == 7 ? vf.state == 1004 :
                        (vf.state == 2004 || (vf.state == 1004 && at.held_item < 0));
                    if (fresh && pickup && at.list_n < 20) {
                        at.list_who[at.list_n] = std::uint8_t(v); at.list_itr[at.list_n] = std::uint8_t(k); ++at.list_n;
                    }
                    continue;
                }
                const bool lying_weapon = vf.state == 1004 && (at.kind == 0 || at.holder >= 0);
                if (itr.vrest != 0 || lying_weapon) {
                    if (at.list_n < 20) { at.list_who[at.list_n] = std::uint8_t(v); at.list_itr[at.list_n] = std::uint8_t(k); ++at.list_n; }
                } else {
                    int origin = to_int(at.x);
                    if (at.holder >= 0 && at.holder < max_ents && ents[at.holder].live) origin = to_int(ents[at.holder].x);
                    int distance = origin > to_int(df.x) ? origin - to_int(df.x) : to_int(df.x) - origin;
                    if (v == at.holder) distance = 2000;
                    if (distance < at.nearest || (distance == at.nearest && rnd(2) == 0)) {
                        at.nearest = distance;
                        at.list_who[0] = std::uint8_t(v);
                        at.list_itr[0] = std::uint8_t(k);
                        at.list_n = 1;
                    }
                }
            }
        }
    };
    // Only entities with an active attack can list targets; the others would return at once.
    bool attacks[max_ents];
    for (int li = 0; li < live_count; ++li) {
        const int i = live[li];
        attacks[i] = ents[i].rest <= 0 && ents[i].def->frame(ents[i].frame_at_pass).nitr != 0;
    }
    for (int ai = 0; ai < live_count; ++ai) {
        const int a = live[ai];
        for (int vi = ai + 1; vi < live_count; ++vi) {
            const int v = live[vi];
            if (attacks[a]) record_targets(a, v);
            if (attacks[v]) record_targets(v, a);
        }
    }
}

void World::add_spark(const Ent& a, const Ent& v, const Itr& itr, int kind) {
    for (auto& s : sparks) {
        if (s.live) continue;
        const Frame& frame = a.def->frame(a.frame);
        int x = a.facing == 0 ? to_int(a.x) - frame.cx + itr.x + itr.w
                              : to_int(a.x) + frame.cx - itr.x - itr.w;
        const int vx = to_int(v.x);
        if (a.facing == 0) { if (x > vx) x = vx; } else if (x < vx) x = vx;
        s.live = true;
        int y = itr.h / 2 + to_int(a.y) + itr.y - frame.cy;
        const int lower = to_int(v.y) - frame.cy;
        if (y < lower) y = (y + lower) / 2;
        else if (to_int(v.y) < y) y = (y + to_int(v.y)) / 2;
        s.y = y + rnd(9) - 4;
        s.x = x + rnd(9) - 4;
        s.z = to_int(a.z);
        const int attacker_index = int(&a - ents), victim_index = int(&v - ents);
        s.owner = victim_index;
        if (a.live && (to_int(a.z) > to_int(v.z) ||
                       (to_int(a.z) == to_int(v.z) && victim_index < attacker_index))) s.owner = attacker_index;
        // The world ages effects at the end of the collision tick.
        s.timer = kind == 1 ? 10 : 6;
        s.kind = kind;
        s.small = itr.fall <= 60;
        return;
    }
}

void World::ball_vs_ball(int a, int v) {
    Ent& A = ents[a];
    Ent& V = ents[v];
    for (Ent* e : {&A, &V}) {
        if (e->def->defined(20)) { e->frame = 20; e->wait = 0; e->ix = e->iy = e->iz = 0; }
    }
}

void World::weapon_hit_recoil(Ent& at, const Ent& victim, bool defended) {
    if (at.holder >= 0 && at.holder < max_ents && ents[at.holder].live)
        ents[at.holder].hit_lag = at.hit_lag;
    if (at.def->frame(at.frame).state == 1002) {
        at.frame = rnd(16);
        at.vx = -victim.ix / 2;
        at.vy = FX(-4);
        if (defended) at.vz = -at.vz * 2 / 3;
        if (!defended && at.kind == 4 && victim.kind == 4) at.ix = -victim.ix;
    }
    if (defended && at.def->frame(at.frame).state == 2000 &&
        ((to_int(victim.x) < to_int(at.x) && at.vx < 0) ||
         (to_int(at.x) < to_int(victim.x) && at.vx > 0))) {
        at.vx = at.vx * 2 / 5;
        at.vz = at.vz * 2 / 5;
    }
}

void World::plain_hit(int a, int v, const Itr& itr) {
    Ent& at = ents[a];
    Ent& vc = ents[v];
    const Frame& vframe = vc.def->frame(vc.frame_at_pass);
    int spark_kind = 0;
    vc.defend_pts = 45;
    if (vc.def->hdr->id == 300) {
        const Frame& current = vc.def->frame(vc.frame);
        if (current.nbdy > 0) {
            const int kind = vc.def->bdy[current.bdy_off].kind;
            if (kind > 1000 && vc.def->defined(kind - 1000)) {
                vc.team = 1; vc.frame = kind - 1000;
                at.hit_lag = 3; vc.hit_lag = -3;
            }
        }
        return;
    }
    if (at.kind == 3) play(at.def->hdr->weapon_broken_sound);
    if (vc.kind != 0) {
        // A projectile struck: it turns around for the attacker's team, or bursts.
        if (ball_like(vc)) {
            const int attacker_state = at.def->frame(at.frame).state;
            if (attacker_state == 3000 && at.def->defined(10) &&
                !(at.def->hdr->id == 209 && (breakable_ball(vc.def->hdr->id) ||
                  (vc.def->hdr->id == 209 && vc.frame == 40)))) {
                at.frame = 10; at.wait = 0; at.vx = 0; at.vz = at.def->frame(10).dvy * 256;
            }
            if (vc.kind == 3) {
                play(vc.def->hdr->weapon_hit_sound);
                vc.defend_pts = 45;
                const int damage = scaled_damage(vc, itr.injury);
                vc.hp -= damage;
                vc.dark_hp -= damage / 3;
                if (vc.hp < 1 || itr.effect == 4) vc.fall = 80;
                ++vc.hit_count;
                vc.fall += itr.fall == 0 ? 20 : itr.fall;
                if (vc.def->frame(vc.state_frame).state == S_ICE || vframe.state == S_FALL) vc.fall = 80;
                if (vc.fall != 80 || vc.vx >= FX(5) || vc.vx <= -FX(5) || itr.dvx != 0) {
                    if (attacker_state == 2000) push(vc, to_int(at.x) < to_int(vc.x) ? itr.dvx : -itr.dvx);
                    else if (itr.effect == 22 || itr.effect == 23) push(vc, to_int(vc.x) <= to_int(at.x) ? itr.dvx : -itr.dvx);
                    else push(vc, sgn_facing(at) * itr.dvx);
                } else push(vc, attacker_state == 2000 ? 5 : sgn_facing(at) * 5);
                if (vc.fall == 80) {
                    if (itr.fall > 40) vc.iy += (itr.dvy != 0 ? itr.dvy : -7) * 256;
                    if (itr.dvy != 0 && to_int(to_int(vc.y) * 256 + vc.iy) > 0) vc.iy = FX(12);
                    vc.frame = (vc.facing == 0 ? vc.ix <= 0 : vc.ix >= 0) ? 180 : 186;
                    vc.fall = 0;
                }
                if (at.hit_lag >= 0) at.hit_lag = 3;
                vc.hit_lag = -3;
                if (itr.vrest > 0) vc.pair_rest[a] = std::int8_t(itr.vrest);
                weapon_hit_recoil(at, vc, false);
            }
            Ent* holder = at.holder >= 0 && at.holder < max_ents && ents[at.holder].live ? &ents[at.holder] : nullptr;
            const Ent& src = holder ? *holder : at;
            const int victim_state = vc.def->frame(vc.frame).state;
            if (victim_state != 3005 && (victim_state != 3006 || at.def->frame(at.frame).state == 3005)) {
                vc.team = src.team;
                vc.owner = src.owner;
                vc.hit_this_frame = true;
                vc.wait = 0;
                vc.ix = vc.iy = vc.iz = 0;
                if (at.def->hdr->id == 209 && breakable_ball(vc.def->hdr->id)) {
                    vc.def = at.def;
                    vc.data_group = at.data_group;
                    vc.group = at.group;
                    vc.frame = vc.prev_frame = vc.state_frame = 40;
                } else if ((at.kind == 0 || holder) && itr.effect != 2 && itr.effect != 20) {
                    if (vc.def->defined(30)) vc.frame = 30;
                    if (at.def->hdr->id == 8 && breakable_ball(vc.def->hdr->id)) {
                        const ObjDef* freeze = find_def(at.data_group, 209);
                        if (freeze) {
                            vc.def = freeze; vc.data_group = at.data_group; vc.group = at.group;
                            vc.prev_frame = vc.state_frame = vc.frame;
                        }
                    }
                } else if (vc.def->defined(20)) vc.frame = 20;
                if (holder && at.def->hdr->id == 213 && breakable_ball(vc.def->hdr->id)) {
                    const ObjDef* freeze = find_def(at.data_group, 209);
                    if (freeze) {
                        vc.def = freeze; vc.data_group = at.data_group; vc.group = at.group;
                        vc.prev_frame = vc.state_frame = vc.frame;
                    }
                }
                vc.walk_owner = holder ? at.holder : a;
            }
            const int new_victim_state = vc.def->frame(vc.frame).state;
            const int new_attacker_state = at.def->frame(at.frame).state;
            if ((new_victim_state == 3005 && new_attacker_state == 3005) || (new_victim_state == 3006 && new_attacker_state == 3006)) ball_vs_ball(a, v);
            Ent& lagged = holder ? *holder : at;
            if (lagged.hit_lag > 0) lagged.hit_lag = -lagged.hit_lag;
            if (itr.arest < 4 && itr.vrest == 0) at.rest = 4; else at.rest = itr.arest;
        } else if (vc.kind == 1 || vc.kind == 2 || vc.kind == 4 || vc.kind == 6) {
            play(vc.def->hdr->weapon_hit_sound);
            vc.defend_pts = 45;
            if (vc.kind != 6) {
                const int damage = scaled_damage(vc, itr.injury);
                count_army_hit(vc, damage, false);
                vc.hp -= damage;
                vc.dark_hp -= damage / 3;
            }
            vc.drop_counter -= itr.injury;
            if (itr.bdefend == 100) vc.drop_counter = -1;
            vc.fall = 0;
            vc.team = at.team;
            vc.hit_this_frame = true;
            if (vc.kind == 2) {
                const int rest = itr.fall <= 40 && itr.effect != 4 ? 3 : 19;
                if (at.holder >= 0 && at.holder < max_ents && at.kind == 2) ents[at.holder].pair_rest[v] = rest;
                else if (at.kind != 2) at.pair_rest[v] = rest;
                vc.facing = at.facing;
                vc.frame = itr.fall <= 40 && to_int(vc.y) >= 0 && itr.effect != 4 ? 20 : rnd(6);
            } else {
                vc.frame = rnd(16);
                if (vc.kind == 4 || vc.kind == 6) at.pair_rest[v] = 30;
            }
            if (vc.kind != 2 || itr.fall > 40) vc.hit_count++;
            if (vc.vx < FX(5) && vc.vx > -FX(5) && itr.dvx == 0) {
                push(vc, at.def->frame(at.frame).state == 2000 ? 5 : sgn_facing(at) * 5);
            } else if (at.def->frame(at.frame).state == 2000) {
                push(vc, to_int(at.x) < to_int(vc.x) ? itr.dvx : -itr.dvx);
            } else if (vc.kind == 4 || vc.kind == 6) {
                const fx speed = vc.vx * 55 / 100;
                const fx limit = speed < 0 ? -speed : speed;
                if (limit < itr.dvx * 256 || (at.facing == 0 && vc.ix > 0) || (at.facing == 1 && vc.ix < 0))
                    push(vc, at.facing == 0 ? itr.dvx : -itr.dvx);
                else if ((at.facing == 1 && vc.vx > 0) || (at.facing == 0 && vc.vx < 0)) vc.ix = -speed;
                if (at.def->hdr->id == 100 && at.holder >= 0) {
                    vc.ix = vc.ix * 5 / 2;
                    play(fixed_sound[13]);
                    if (vc.ix > 0 && vc.ix < FX(10)) vc.ix = FX(10);
                    if (vc.ix < 0 && vc.ix > -FX(10)) vc.ix = -FX(10);
                }
            } else if (itr.effect == 22 || itr.effect == 23) {
                push(vc, to_int(vc.x) <= to_int(at.x) ? itr.dvx : -itr.dvx);
            } else push(vc, at.facing == 0 ? itr.dvx : -itr.dvx);
            if (vc.kind != 2 || itr.fall > 40) vc.iy += (itr.dvy != 0 ? itr.dvy : -7) * 256;
            if (itr.dvy != 0 && to_int(to_int(vc.y) * 256 + vc.iy) > 0) vc.iy = FX(12);
            if (at.hit_lag >= 0) at.hit_lag = 3;
            vc.hit_lag = -3;
            at.rest = itr.arest < 4 && itr.vrest == 0 ? 4 : itr.arest;
            if (itr.vrest > 0) vc.pair_rest[a] = std::int8_t(itr.vrest);
            if (at.def->frame(at.frame).state == 3000 && at.def->defined(10)) {
                at.frame = 10; at.wait = 0; at.vx = 0; at.vz = at.def->frame(10).dvy * 256;
            }
            weapon_hit_recoil(at, vc, false);
        }
        if (ball_like(vc) || vc.kind == 1 || vc.kind == 2 || vc.kind == 4 || vc.kind == 6)
            add_spark(at, vc, itr, 0);
        return;
    }
#ifdef SIM_DEBUG
    printf("hit: attacker %d (obj %d frame %d) -> victim %d (frame %d) injury %d fall %d\n", a, at.def->hdr->id, at.frame_at_pass, v, vc.frame_at_pass, itr.injury, itr.fall);
#endif
    vc.defend_pts = 45;
    {
        const int damage = scaled_damage(vc, itr.injury);
        count_army_hit(vc, damage);
        vc.hp -= damage;
        vc.dark_hp -= damage / 3;
    }
    if (vc.hp < 1 || itr.effect == 4) vc.fall = 80;
    vc.hit_count += 1;
    vc.fall += itr.fall == 0 ? 20 : itr.fall;
    if (vc.def->frame(vc.state_frame).state == S_ICE || vframe.state == S_FALL) vc.fall = 80;
    const int b0 = vc.fall;
    const int same = vc.facing == at.facing ? 1 : 0;
    if (b0 > 60) vc.fall = 80;
    else if (b0 > 40) { vc.frame = 220 + 6; vc.fall = to_int(vc.y) < 0 ? 80 : 60; }
    else if (b0 > 20) { vc.frame = 220 + 2 + same * 2; vc.fall = to_int(vc.y) < 0 ? 80 : 40; }
    else if (b0 > 0) {
        vc.frame = 220;
        vc.fall = 20;
        if (to_int(vc.y) < 0) vc.frame = 220 + 2 + same * 2;
    }
    if (itr.effect == 0) play(fixed_sound[vc.fall == 80 ? 2 : 0]);
    if (itr.effect == 1) {
        spark_kind = 1;
        if (vc.fall == 80) { play(fixed_sound[12]); play(fixed_sound[2]); }
        else { play(fixed_sound[11]); play(fixed_sound[0]); }
    }
    if (vc.fall != 80 || vc.vx >= FX(5.0) || vc.vx <= -FX(5.0) || itr.dvx != 0) {
        if (at.def->frame(at.frame_at_pass).state == 2000) push(vc, to_int(at.x) < to_int(vc.x) ? itr.dvx : -itr.dvx);
        else if (itr.effect == 22 || itr.effect == 23) push(vc, to_int(vc.x) <= to_int(at.x) ? itr.dvx : -itr.dvx);
        else push(vc, at.facing == 0 ? itr.dvx : -itr.dvx);
    } else {
        push(vc, at.def->frame(at.frame_at_pass).state == 2000 ? 5 : sgn_facing(at) * 5);
    }
    if (at.def->frame(at.frame_at_pass).state == 3000) {
        // The projectile bursts on what it hit (a state 3006 missile flies on through).
        if (at.def->defined(10)) {
            at.frame = 10;
            at.wait = 0;
            at.vx = 0;
            at.vz = at.def->frame(10).dvy * 256;
        }
    }
    if (vc.fall == 80) {
        if (itr.dvy == 0) vc.iy -= FX(7.0);
        else {
            vc.iy += itr.dvy * 256;
            if (to_int(to_int(vc.y) * 256 + vc.iy) > 0) vc.iy = FX(12.0);
        }
        if ((vc.facing == 0 && vc.ix <= 0) || (vc.facing == 1 && vc.ix >= 0)) vc.frame = 180;
        else vc.frame = 180 + 6;
        if (vc.held_item >= 0 && vc.held_item < max_ents) {
            const Ent& held = ents[vc.held_item];
            if (held.live && held.holder == v) {
                at.pair_rest[vc.held_item] = 45;
                vc.pair_rest[vc.held_item] = 30;
            }
        }
    }
    if (at.hit_lag >= 0) at.hit_lag = 3;
    vc.hit_lag = -3;
    if (itr.arest < 4 && itr.vrest == 0) at.rest = 4; else at.rest = itr.arest;
    if (itr.vrest > 0) vc.pair_rest[a] = std::int8_t(itr.vrest);
    weapon_hit_recoil(at, vc, false);
    // A held fighter can play the catch point's hurt animation when struck.
    const Frame& caught_frame = vc.def->frame(vc.frame_at_pass);
    if (caught_frame.cp >= 0 && vc.caught_by >= 0 && vc.caught_by < max_ents) {
        const Cpoint& cp = vc.def->cp[caught_frame.cp];
        const Ent& holder = ents[vc.caught_by];
        if (cp.kind == 2 && holder.live && holder.catching == v && vc.fall != 80 && cp.injury != 0) {
            const int hurt = vc.facing != at.facing ? cp.injury : cp.cover;
            if (vc.def->defined(hurt)) vc.frame = hurt;
        }
    }
    if (vc.fall == 80) vc.fall = 0;
    if (itr.effect == 3 || itr.effect == 30) {
        if (vc.def->frame(vc.state_frame).state != S_ICE) {
            vc.frame = 200;
            vc.wait = 0;
            play(fixed_sound[14]);
        }
    }
    if (itr.effect == 2 || itr.effect == 21 || itr.effect == 22 || (itr.effect == 20 && vc.def->frame(vc.state_frame).state != S_BURN)) {
        vc.frame = 203;
        vc.wait = 0;
        play(fixed_sound[16]);
        vc.facing = vc.ix < 0 ? 0 : 1;
    }
    if (itr.effect == 23) play(fixed_sound[16]);
    if (at.def->hdr->id == 201) at.live = false;
    if (at.def->hdr->id == 214) at.hp = 0;
    add_spark(at, vc, itr, spark_kind);
    if (itr.injury > 40 || vc.fall == 0) shake = 3;
}

void World::defended_hit(int a, int v, const Itr& itr) {
    Ent& at = ents[a];
    Ent& vc = ents[v];
    const Frame& vframe = vc.def->frame(vc.frame_at_pass);
    if (at.kind == 3) play(at.def->hdr->weapon_broken_sound);
    else if (vc.def->hdr->id == 37 || vc.def->hdr->id == 6) play(fixed_sound[17]);
    else play(fixed_sound[1]);
    const int damage = scaled_damage(vc, itr.injury) / 10;
    count_army_hit(vc, damage);
    vc.hp -= damage;
    vc.dark_hp -= damage / 3;
    if (vc.hp <= 0) vc.fall = 80;
    vc.wait = 0;
    vc.defend_pts += itr.bdefend;
    vc.hit_count += 1;
    at.hit_lag = 3;
    vc.hit_lag = -5;
    if (to_int(vc.y) == 0) {
        if (vc.defend_pts < 31 || vframe.state != S_DEFEND) {
            if (vc.frame == 110) vc.frame = 111;
        } else vc.frame = 112;
        if (vc.fall != 80 || vc.vx >= FX(3.0) || vc.vx <= -FX(3.0) || itr.dvx != 0) {
            if (at.def->frame(at.frame).state == 2000) push(vc, to_int(at.x) < to_int(vc.x) ? itr.dvx : -itr.dvx);
            else if (itr.effect == 22 || itr.effect == 23) push(vc, to_int(vc.x) <= to_int(at.x) ? itr.dvx : -itr.dvx);
            else if (at.facing == 0) push(vc, itr.dvx / 2);
            else push(vc, -itr.dvx / 2);
        } else if (at.def->frame(at.frame).state == 2000) push(vc, to_int(at.x) < to_int(vc.x) ? 6 : -6);
        else push(vc, sgn_facing(at) * 3);
    } else {
        if (vc.fall != 80 || vc.vx >= FX(6.0) || vc.vx <= -FX(6.0) || itr.dvx > 5)
            push(vc, itr.effect == 22 || itr.effect == 23 ? (to_int(vc.x) <= to_int(at.x) ? itr.dvx : -itr.dvx) : (at.facing == 0 ? itr.dvx : -itr.dvx));
        else push(vc, sgn_facing(at) * 6);
    }
    if (itr.arest < 4 && itr.vrest == 0) at.rest = 4;
    else at.rest = itr.arest < 12 ? itr.arest : 12;
    if (itr.vrest > 0) {
        const int rest = itr.vrest < 5 ? 4 : int(std::int8_t(itr.vrest));
        vc.pair_rest[a] = std::int8_t(rest < 12 ? rest : 12);
    }
    weapon_hit_recoil(at, vc, true);
    if (at.def->frame(at.frame_at_pass).state == 3000 && at.def->defined(10)) { at.frame = 10; at.wait = 0; at.vx = 0; }
}

void World::apply(int a) {
    Ent& at = ents[a];
    const Frame& frame = at.def->frame(at.frame_at_pass);
    for (int n = 0; n < at.list_n; ++n) {
        const int k = at.list_itr[n];
        if (k >= frame.nitr) break;
        const int v = at.list_who[n];
        Ent& vc = ents[v];
        if (!vc.live || vc.pair_rest[a] > 0) continue;
        if (at.hit_this_frame && vc.kind == 0) return;
        Itr itr = at.def->itr[frame.itr_off + k];
        const Frame& victim_frame = vc.def->frame(vc.frame_at_pass);
        if (victim_frame.cp >= 0 && vc.def->cp[victim_frame.cp].kind == 2 && vc.caught_by >= 0 && vc.caught_by < max_ents) {
            const Ent& holder = ents[vc.caught_by];
            if (holder.live && holder.catching == v) {
                const Frame& holder_frame = holder.def->frame(holder.frame_at_pass);
                if (holder_frame.cp < 0 || holder.def->cp[holder_frame.cp].hurtable == 0) continue;
            }
        }
        if (itr.kind == 0 && itr.effect == 21 &&
            (vc.def->frame(vc.frame).state == S_BURN || vc.def->frame(vc.frame).state == S_BURN_RUN)) return;
        if (itr.kind == 2 || itr.kind == 7) {
            const bool item = vc.kind == 1 || vc.kind == 2 || vc.kind == 4 || vc.kind == 6;
            if ((itr.kind == 2 && item) || (itr.kind == 7 && at.held_item < 0)) {
                at.held_item = v; vc.holder = a; vc.team = at.team; vc.owner = a;
                if (vc.kind == 6 && vc.hp <= 0) vc.drop_counter = 0;
                if (itr.kind == 2) at.frame = vc.kind == 2 ? 116 : 115;
            }
            if (itr.kind == 2) at.wait = 0;
            continue;
        }
        if (itr.kind == 5 && at.holder >= 0 && at.holder < max_ents) {
            const Ent& holder = ents[at.holder];
            const int strength = holder.def->frame(holder.frame_at_pass).wp_attacking;
            if (!holder.live || holder.held_item != a || v == at.holder || strength <= 0 || strength >= 10 || !at.def->strength) continue;
            const Itr& hit = at.def->strength[strength];
            itr.dvx = hit.dvx; itr.dvy = hit.dvy; itr.fall = hit.fall; itr.arest = hit.arest; itr.vrest = hit.vrest;
            itr.bdefend = hit.bdefend; itr.injury = hit.injury; itr.effect = hit.effect; itr.zwidth = hit.zwidth; itr.kind = 0;
        }
        if (itr.kind == 1 || itr.kind == 3) { catch_victim(a, v, itr); continue; }
        if (itr.kind == 4 && at.fall_damage > 0) {
            itr.kind = 0;
            if ((at.vx > 0 && at.facing == 1) || (at.vx < 0 && at.facing == 0)) itr.dvx = -itr.dvx;
        }
        const auto drop_heavy = [&]() {
            if (vc.held_item < 0 || vc.held_item >= max_ents) return;
            Ent& item = ents[vc.held_item];
            if (!item.live || item.kind != 2 || item.holder != v) return;
            at.pair_rest[vc.held_item] = 45;
            vc.pair_rest[vc.held_item] = 30;
            vc.held_item = -1; item.holder = -1;
            item.frame = rnd(6); item.vy = -FX(1.0);
        };
        if (itr.kind == 0) drop_heavy();
        if (vc.kind == 2) { itr.dvx /= 2; itr.dvy /= 2; }
        if (itr.kind == 9 && (vc.kind == 0 || vc.def->frame(vc.frame).state == 1002 ||
                              vc.def->frame(vc.frame).state == 2000)) {
            if (vc.kind == 0) at.hp = 0;
            itr.kind = 0;
        }
        if (itr.kind == 6) { vc.super_window = 3; continue; }
        if (itr.kind == 14) {
            if (to_int(at.x) > to_int(vc.x) + 5 && (vc.vx > 0 || vc.ix > 0)) vc.blocked_right = true;
            else if (to_int(at.x) < to_int(vc.x) - 5 && (vc.vx < 0 || vc.ix < 0)) vc.blocked_left = true;
            if (to_int(at.z) > to_int(vc.z) + 2 && (vc.vz > 0 || vc.iz > 0)) vc.blocked_down = true;
            else if (to_int(at.z) < to_int(vc.z) - 2 && (vc.vz < 0 || vc.iz < 0)) vc.blocked_up = true;
            continue;
        }
        if (itr.kind == 8) {
            vc.regen_timer = 1000 + itr.injury;
            if (at.def->defined(itr.dvx)) at.frame = itr.dvx;
            at.x = vc.x;
            at.z = vc.z + FX(1.0);
            continue;
        }
        if (itr.kind == 9 && vc.kind == 3) {
            play(vc.def->hdr->weapon_broken_sound);
            at.hit_lag = -3;
            vc.hit_this_frame = true;
            if (vc.def->frame(vc.frame).state == 3005) {
                if (vc.def->defined(40)) vc.frame = 40;
            } else {
                vc.team = at.team;
                vc.owner = at.owner;
                if (vc.def->defined(30)) vc.frame = 30;
                vc.wait = 0;
                vc.walk_phase = a;
                vc.walk_owner = a;
                vc.vx = vc.vy = vc.vz = vc.ix = vc.iy = vc.iz = 0;
            }
            continue;
        }
        if (itr.kind == 10 || itr.kind == 11 || itr.kind == 15 || (itr.kind == 16 && vc.kind != 0)) {
            if (itr.kind == 11 && vc.fall_damage >= 0) continue;
            const bool item = vc.kind == 1 || vc.kind == 2 || vc.kind == 4 || vc.kind == 6;
            if (vc.kind != 0 && (!item || vc.def->hdr->id == 201 || vc.def->hdr->id == 202)) continue;
            if (vc.kind != 0) {
                const int airborne = vc.kind == 2 ? 2000 : 1000;
                if (vc.def->frame(vc.frame).state != airborne) vc.frame = 0;
            }
            if (itr.kind == 15 || itr.kind == 16) {
                vc.ix = vc.vx += to_int(vc.x) > to_int(at.x) ? -FX(1.0) : FX(1.0);
                vc.iz = vc.vz += to_int(vc.z) > to_int(at.z) ? -FX(0.5) : FX(0.5);
            } else {
                if (vc.kind == 0) {
                    vc.fall_damage = -20;
                    count_army_hit(vc, 11, false);
                    if (vc.def->defined(182)) vc.frame = 182;
                }
                vc.ix = vc.vx = vc.vx * 100 / 107;
                vc.iz = vc.vz = vc.vz * 100 / 107;
            }
            // The reference adjusts only its transient integer y coordinate here.
            if (to_int(vc.y) >= -2) vc.vy = FX(-6.0);
            if (vc.vy > FX(-6.0)) vc.iy = vc.vy -= vc.kind == 2 ? FX(2.3) : FX(3.0);
            continue;
        }
        if (itr.kind == 16 && vc.kind == 0) {
            const int damage = scaled_damage(vc, itr.injury);
            count_army_hit(vc, damage);
            vc.hp -= damage;
            vc.dark_hp -= damage / 3;
            if (vc.def->defined(200)) vc.frame = 200;
            vc.wait = 0;
            if (itr.vrest > 0) vc.pair_rest[a] = std::int8_t(itr.vrest);
            drop_heavy();
            play(fixed_sound[14]);
            continue;
        }
        if (itr.kind != 0) continue;
        const Frame& vf = vc.def->frame(vc.frame_at_pass);
        bool defended = false;
        const bool exposed = itr.bdefend == 100 || vf.state == S_BROKEN || vf.state == S_HURT || vf.state == S_FALL ||
                             vf.state == S_ICE || vf.state == S_LYING || vf.state == S_DIZZY || vf.state == S_BURN;
        const int id = vc.def->hdr->id, attacker_id = at.def->hdr->id;
        const bool elemental = itr.effect == 2 || itr.effect == 3 || (itr.effect >= 20 && itr.effect < 40);
        const bool armor_exception = attacker_id == 208 || attacker_id == 214;
        if (!exposed && vc.kind == 0 && !armor_exception) {
            if (id == 37 && vc.defend_pts < 16 && !elemental) defended = true;
            if (id == 6 && vc.defend_pts < 2 && !elemental &&
                (vc.frame < 20 || vf.state == S_DASH || vf.state == S_JUMP || vf.state == S_DEFEND)) defended = true;
            if (id == 52 && vc.defend_pts < 16) defended = true;
        }
        const bool blockable_from_behind = attacker_id == 124 || attacker_id == 220 || attacker_id == 221 || attacker_id == 222;
        if (!exposed && vf.state == S_DEFEND && itr.bdefend < 61 &&
            (at.facing != vc.facing || itr.dvx < 0 || blockable_from_behind) && vc.hp > 0 && vc.kind == 0)
            defended = true;
        if (defended) { defended_hit(a, v, itr); add_spark(at, vc, itr, 0); }
        else {
            plain_hit(a, v, itr);
            if (vc.def->hdr->id == 300) return;
        }
    }
}

void World::apply_impulses() {
    for (auto& e : ents) {
        if (!e.live || e.hit_lag != 0) continue;
        if (e.hit_count != 0) {
            e.vx = e.ix * 2 / (e.hit_count + 1);
            e.vy = e.iy * 2 / (e.hit_count + 1);
            e.vz = e.iz * 2 / (e.hit_count + 1);
            e.hit_count = 0;
        }
        e.ix = e.iy = e.iz = 0;
    }
}

namespace {
const Cpoint* cpoint_of(const Ent& e, int frame) {
    const Frame& f = e.def->frame(frame);
    return f.cp >= 0 ? &e.def->cp[f.cp] : nullptr;
}
void enter(Ent& e, int frame) {
    e.frame = frame;
    if (e.frame < 0) { e.facing = 1 - e.facing; e.frame = -e.frame; }
}
}  // namespace

// The walking attack against a dizzy victim: both take their catching frames and are lined up.
void World::catch_victim(int a, int v, const Itr& itr) {
    Ent& at = ents[a];
    Ent& vc = ents[v];
    vc.vx = at.vx = 0;
    at.facing = to_int(at.x) > to_int(vc.x) ? 1 : 0;
    vc.facing = 1 - at.facing;
    const int catching = itr.catching, caught = itr.caught;
#ifdef SIM_DEBUG
    printf("catch: %d grabs %d (frames %d/%d)\n", a, v, catching, caught);
#endif
    if (!at.def->defined(catching) || !vc.def->defined(caught)) return;
    const Cpoint* first = cpoint_of(at, catching);
    const Cpoint* second = cpoint_of(vc, caught);
    if (!first || !second) return;
    at.frame = catching;
    vc.frame = caught;
    const int a_center = at.def->frame(catching).cx, v_center = vc.def->frame(caught).cx;
    const int ax = to_int(at.x);
    int vx;
    if (at.facing == 0) vx = ax - a_center - v_center + second->x + first->x;
    else vx = a_center + v_center + ax - second->x - first->x;
    vc.y = (vc.def->frame(caught).cy - at.def->frame(catching).cy + to_int(at.y)) * 256;
    const fx shift = (to_int(vc.x) - vx) * 128;
    vc.x = vx * 256 + shift;
    at.x = ax * 256 + shift;
    at.y = to_int(at.y) * 256;
    at.catching = v;
    vc.caught_by = a;
    at.catch_timer = 300;
    vc.fall = 0;
}

void World::grab_holds() {
    for (int index = 0; index < max_ents; ++index) {
        Ent& holder = ents[index];
        if (!holder.live) continue;
        const Cpoint* hit = cpoint_of(holder, holder.frame_at_pass);
        if (hit && hit->kind == 1 && holder.hit_lag >= 0) {
            Ent* target = (holder.catching >= 0) ? &ents[holder.catching] : nullptr;
            bool held = target && target->live && target->caught_by == index;
            const Cpoint* tc = held ? cpoint_of(*target, target->frame_at_pass) : nullptr;
            bool released = false;
            if (held && tc && tc->kind == 2) {
                if (hit->decrease > 0) holder.catch_timer -= hit->decrease;
                if (hit->decrease < 0) {
                    holder.catch_timer += hit->decrease;
                    if (holder.catch_timer < 0) {
                        holder.frame = 0;
                        holder.hit_count = 1;
                        target->hit_count = 1;
                        target->ix = to_int(holder.x) > to_int(target->x) ? FX(-4.0) : FX(4.0);
                        target->iy = FX(-3.0);
                        target->frame = 181;
                        released = true;
                    }
                }
                if (!released) {
                    const Keys& k = holder.keys;
                    auto act = [&](int frame) {
                        enter(holder, frame);
                        const Cpoint* now = cpoint_of(holder, holder.frame);
                        if (now) target->frame = now->vaction;
                        target->wait = 0;
                        holder.wait = 0;
                    };
                    if (k.attack && holder.press[p_attack] > 0 && hit->aaction != 0 && ((!k.left && !k.right) || hit->taction == 0)) act(hit->aaction);
                    if (k.attack && holder.press[p_attack] > 0 && (k.left || k.right || k.up || k.down) && hit->taction != 0) act(hit->taction);
                    if (k.jump && holder.press[p_jump] > 0 && hit->jaction != 0) act(hit->jaction);
                }
            } else {
                released = true;
            }
            if (released) holder.frame = 0;
            if (hit->throwvx != 0 && held && target) {
                const int change = hit->throwinjury;
                if (change >= 1) target->fall_damage = change;
                else if (change == -1 && target->data_group) {
                    holder.transform_original = holder.data_group;
                    holder.transform_target = target->data_group;
                    change_character(holder, target->data_group, 0, false);
                    for (Ent& follower : ents) {
                        if (!follower.live || follower.follow != index) continue;
                        follower.def = &target->data_group->objs[0];
                        follower.data_group = target->data_group;
                        follower.kind = follower.def->kind();
                    }
                }
                const Frame& frame = holder.def->frame(holder.frame);
                target->y = (to_int(holder.y) - frame.cy + hit->y) * 256;
                if (holder.facing == 0) target->x = (to_int(holder.x) - frame.cx + hit->x) * 256;
                else target->x = (frame.cx - hit->x + to_int(holder.x)) * 256;
                holder.frame = frame.next;
                holder.frame_at_pass = holder.frame;
                holder.wait = 0;
                target->vx = holder.facing == 0 ? hit->throwvx * 256 : -hit->throwvx * 256;
                target->frame = hit->vaction;
                target->frame_at_pass = target->frame;
                target->vy = hit->throwvy * 256;
                const int depth = hit->throwvz;
                if (holder.keys.up && !holder.keys.down) target->vz = -depth * 256;
                else if (holder.keys.down && !holder.keys.up) target->vz = depth * 256;
                target->caught_by = -1;
                holder.catching = -1;
            }
            if (hit->dircontrol == 1 && holder.wait == 2) {
                if (holder.keys.right && !holder.keys.left) holder.facing = 0;
                if (!holder.keys.right && holder.keys.left) holder.facing = 1;
            }
            if (hit->dircontrol == -1 && holder.wait == 2) {
                if (holder.keys.right && !holder.keys.left) holder.facing = 1;
                if (!holder.keys.right && holder.keys.left) holder.facing = 0;
            }
        } else {
            const Cpoint* mine = cpoint_of(holder, holder.frame);
            if (mine && mine->kind == 2) {
                // A victim whose holder no longer holds it drops.
                Ent* other = holder.caught_by >= 0 ? &ents[holder.caught_by] : nullptr;
                const Cpoint* oc = other && other->live && other->catching == index ? cpoint_of(*other, other->frame) : nullptr;
                if (!oc || oc->kind != 1) {
                    holder.frame = F_JUMP_FRAME;
                    holder.vy = FX(-3.0);
                    if (holder.y > FX(-2.0)) holder.y = FX(-2.0);
                    holder.caught_by = -1;
                }
            }
        }
    }
}

// Places the victim of every catch on its holder's hand.
void World::grab_place() {
    for (int index = 0; index < max_ents; ++index) {
        Ent& holder = ents[index];
        if (!holder.live) continue;
        const Frame& frame = holder.def->frame(holder.frame);
        const Cpoint* hit = frame.cp >= 0 ? &holder.def->cp[frame.cp] : nullptr;
        Ent* target = holder.catching >= 0 ? &ents[holder.catching] : nullptr;
        if (!hit || hit->kind != 1 || frame.state != S_CATCH || !target || !target->live || target->caught_by != index) continue;
        const Cpoint* tc = cpoint_of(*target, target->frame);
        if (!tc || tc->kind != 2) continue;
        if ((target->hit_lag == 0 && hit->hurtable == 1) || hit->hurtable == 0) target->frame = hit->vaction;
        if (target->frame < 0) { target->facing = 1 - target->facing; target->frame = -target->frame; }
        int damage = hit->injury;
        if (damage != 0 && holder.wait == 0) {
            if (damage > 0) {
                damage = scaled_damage(*target, damage);
                count_army_hit(*target, damage);
                target->hp -= damage;
                target->dark_hp -= damage / 3;
                holder.wait = 1;
                holder.hit_lag = 2;
                target->hit_lag = -3;
            } else {
                target->hp += damage;
                target->dark_hp += damage / 3;
                holder.wait = 1;
            }
        }
        int x;
        if (holder.facing == 0) x = to_int(holder.x) - frame.cx + hit->x;
        else x = frame.cx - hit->x + to_int(holder.x);
        const int y = to_int(holder.y) - frame.cy + hit->y;
        const Cpoint* other = cpoint_of(*target, hit->vaction);
        const Frame& tf = target->def->frame(target->frame);
        int tx, ty;
        if (other) {
            if (target->facing == 0) tx = tf.cx - other->x + x; else tx = other->x - tf.cx + x;
            ty = tf.cy - other->y + y;
        } else { tx = to_int(target->x); ty = to_int(target->y); }
        int tz = to_int(holder.z);
        if (hit->cover % 10 == 0) { tz -= 1; ty += 1; } else { tz += 1; ty -= 1; }
        const int direction = hit->cover / 10;
        if (direction == 1) target->facing = holder.facing;
        else if (direction == 2) target->facing = 1 - holder.facing;
        target->x = tx * 256; target->y = ty * 256; target->z = tz * 256;
    }
}

// Objects of a fighter's group by original object number.
const ObjDef* World::find_def(const Group* g, int id) const {
    if (!g) return nullptr;
    for (int i = 0; i < g->count; ++i) if (g->objs[i].hdr->id == id) return &g->objs[i];
    return nullptr;
}

// Steering of self-guided projectiles and the starter objects of the multi-projectile moves (hit_Fa selects it).
void World::ball_update(int index) {
    Ent& me = ents[index];
    const Frame& f = me.def->frame(me.frame);
    const int mode = f.hit_Fa;
    if (mode == 0) return;
    auto ai = [](int v) { return v < 0 ? -v : v; };
    if (mode == 10) {                              // accelerating
        me.vx += me.vx < 0 ? -FX(1.1) : FX(1.1);
        if (me.vx > FX(30.0)) me.vx = FX(30.0);
        if (me.vx < -FX(30.0)) me.vx = -FX(30.0);
        if (me.y > FX(3.0)) me.y = FX(3.0);
        me.facing = me.vx > 0 ? 0 : 1;
        return;
    }
    auto make = [&](int id, int frame) -> Ent* {
        const ObjDef* def = find_def(me.data_group, id);
        if (!def) return nullptr;
        const int slot = spawn(def, me.group, me.owner, frame, me.team, me.x, me.y, me.z, 0, 0, 0, 0);
        if (slot < 0) return nullptr;
        ents[slot].data_group = me.data_group;
        return &ents[slot];
    };
    if (mode == 11) {
        struct Piece { int id, frame, x, y, z, facing, extra_vz; };
        constexpr Piece pieces[] = {
            {211,109,0,0,0,-1,0}, {221,81,0,-100,0,-1,0},
            {212,100,80,-3,0,0,-7}, {212,100,100,-3,0,0,0}, {212,100,80,-3,0,0,7},
            {212,100,-80,-3,0,1,-7}, {212,100,-100,-3,0,1,0}, {212,100,-80,-3,0,1,7},
            {211,50,-30,-1,-5,1,0}, {211,50,30,-1,-5,1,0},
            {211,50,-30,-1,2,0,0}, {211,50,30,-1,2,0,0},
            {211,50,0,-1,-9,1,0}, {211,50,0,-1,6,0,0},
        };
        for (const Piece& piece : pieces) {
            Ent* child = make(piece.id, piece.frame);
            if (!child) continue;
            child->x = (to_int(me.x) + piece.x) * 256;
            child->y = (to_int(me.y) + piece.y) * 256;
            child->z = (to_int(me.z) + piece.z) * 256;
            child->vx = me.vx; child->vy = me.vy; child->vz = me.vz + piece.extra_vz * 256;
            child->facing = piece.facing < 0 ? me.facing : piece.facing;
        }
        me.live = false;
        return;
    }
    if (mode == 5) {                               // one healing star to every living ally
        for (int i = 0; i < max_ents; ++i) {
            const Ent& o = ents[i];
            if (!o.live || o.kind != 0 || o.team != me.team || o.hp <= 0 || i == index) continue;
            Ent* s = make(219, 0);
            if (!s) break;
            s->vx = ((to_int(o.x) - to_int(s->x)) / 50) * 256;
            s->chase = i;
        }
        me.live = false;
        return;
    }
    if (mode == 6) {                               // one homing star per enemy
        int spawned = 0;
        for (int i = 0; i < max_ents; ++i) {
            const Ent& o = ents[i];
            if (!o.live || o.kind != 0 || o.team == me.team || o.hp <= 0) continue;
            if (spawned >= 7) break;
            Ent* s = make(220, 0);
            if (!s) break;
            ++spawned;
            s->vx = ((to_int(o.x) - to_int(s->x)) / 50) * 256;
            s->vy = (-4 - rnd(4)) * 256;
            s->chase = i;
        }
        me.live = false;
        return;
    }
    if (mode == 9) {
        int spawned = 0;
        bool no_slots = false;
        do {
            int before = spawned;
            for (int i = 0; i < max_ents && spawned < 10; ++i) {
                const Ent& enemy = ents[i];
                if (!enemy.live || enemy.kind != 0 || enemy.team == me.team || enemy.hp <= 0) continue;
                if (before != 0 && spawned >= 4) break;
                Ent* s = make(221 + rnd(2), 0);
                if (!s) { no_slots = true; break; }
                ++spawned;
                s->vx = (rnd(21) - 11) * 256;
                s->vy = -FX(2) - rnd(40) * 256 / 6;
                s->vz = 0; s->facing = 0; s->chase = i;
            }
            if (before == spawned) break;
        } while (spawned < 4 && !no_slots);
        me.live = false;
        return;
    }
    if (mode == 7) {
        Ent* copy = make(me.def->hdr->id, 40);
        if (copy) { copy->vx = copy->vy = copy->vz = 0; copy->facing = 0; }
    }
    if (mode == 8 || mode == 13) {                 // bats / a seeker toward a random enemy
        int list[max_ents], n = 0;
        for (int i = 0; i < max_ents; ++i) {
            const Ent& o = ents[i];
            if (o.live && o.kind == 0 && o.team != me.team && o.hp > 0) list[n++] = i;
        }
        int amount = mode == 13 ? 1 : (n > 4 ? (n - 3) / 2 + 3 : 3);
        while (amount-- > 0) {
            Ent* s = make(mode == 13 ? 228 : 225, 0);
            if (!s) break;
            if (mode == 13) {
                // The reference randomizes only its transient integer y coordinate;
                // physics derives that coordinate from y again on the next step.
                rnd(7);
                s->vx = me.vx;
                s->vy = FX(0.1);
                s->vz = FX(3.0) - rnd(24) * 64 + me.vz;
            } else {
                s->vx = (rnd(21) - 11) * 256;
                s->vy = FX(3.0) - rnd(24) * 64;
                s->vz = FX(3.0) - rnd(24) * 64;
            }
            s->facing = me.facing;
            s->chase = n ? list[rnd(n)] : index;
        }
        me.live = false;
        return;
    }
    // Chasing modes. 4 (healing star) and 7 (star that falls on its target) keep the target they were given.
    // Removed objects retain their last data until their slot is reused. Homing
    // projectiles can keep steering toward such a target if no replacement exists.
    Ent* target = (me.chase >= 0 && me.chase < max_ents && ents[me.chase].def) ? &ents[me.chase] : nullptr;
    const int linked_team = me.thrower >= 0 && me.thrower < max_ents && ents[me.thrower].live ? ents[me.thrower].team : -1;
    if (mode == 4 || mode == 7) {
        if (!target) return;
    } else {
        if (target && (!target->live || target->hp <= 0 || target->team == me.team || target->team == linked_team || target->def->frame(target->frame).state == S_LYING || ai(target->blink) > 2)) target = nullptr;
        if (!target) {
            int best = 10000;
            for (int i = 0; i < max_ents; ++i) {
                Ent& o = ents[i];
                if (!o.live || o.kind != 0 || o.team == me.team || o.team == linked_team || o.hp <= 0) continue;
                if (me.chase >= 0 && (o.def->frame(o.frame).state == S_LYING || ai(o.blink) > 2)) continue;
                const int d = ai(to_int(o.z) - to_int(me.z)) + ai(to_int(o.x) - to_int(me.x));
                if (d < best) { best = d; me.chase = i; target = &o; }
            }
            if (!target) {
                if (me.chase < 0) me.hp = 0;
                else if (me.chase < max_ents && ents[me.chase].def) target = &ents[me.chase];
                if (!target) return;
            }
        }
    }
    if (mode == 1) {
        if (to_int(target->x) > to_int(me.x)) me.vx += FX(0.85);
        if (to_int(target->x) < to_int(me.x)) me.vx -= FX(0.85);
        if (to_int(target->z) > to_int(me.z) + 7) me.vz += FX(0.3);
        if (to_int(target->z) < to_int(me.z) - 7) me.vz -= FX(0.3);
        me.vy = me.vy * 183 / 256;
        if (target->kind == 0) {
            if (target->y > me.y + FX(10)) me.y += FX(1.2);
            if (target->y < me.y + FX(10)) me.y -= FX(1.2);
        } else if (me.y > 0) me.y += FX(1);
        if (me.vx > FX(13.0)) me.vx = FX(13.0);
        if (me.vx < -FX(13.0)) me.vx = -FX(13.0);
        if (me.vz > FX(2.0)) me.vz = FX(2.0);
        if (me.vz < -FX(2.0)) me.vz = -FX(2.0);
        if (me.y > FX(1)) me.y = FX(1);
        me.facing = me.vx > 0 ? 0 : 1;
        return;
    }
    if (mode == 4 && target->hp > 0 && to_int(me.x) > to_int(target->x) - 30 && to_int(me.x) < to_int(target->x) + 30 &&
        to_int(me.y) > to_int(target->y) - 80 && to_int(me.y) < to_int(target->y) &&
        to_int(me.z) > to_int(target->z) - 10 && to_int(me.z) < to_int(target->z) + 10) {
        me.vx = me.vy = me.vz = 0;
        me.frame = 60;
        target->heal_timer = 100;
        return;
    }
    if (mode == 3) {
        if (to_int(target->x) > to_int(me.x)) me.vx += FX(0.7);
        if (to_int(target->x) < to_int(me.x)) me.vx -= FX(0.7);
        if (to_int(target->z) > to_int(me.z) + 10) me.vz += FX(0.17);
        if (to_int(target->z) < to_int(me.z) - 10) me.vz -= FX(0.17);
        if (me.vx > FX(16.0)) me.vx = FX(16.0);
        if (me.vx < -FX(16.0)) me.vx = -FX(16.0);
        if (me.vz > FX(2.4)) me.vz = FX(2.4);
        if (me.vz < -FX(2.4)) me.vz = -FX(2.4);
        return;
    }
    if (me.hp > 0) {
        if (to_int(target->x) > to_int(me.x)) me.vx += FX(mode == 7 ? 1.4 : 0.7);
        if (to_int(target->x) < to_int(me.x)) me.vx -= FX(mode == 7 ? 1.4 : 0.7);
        if (to_int(target->z) > to_int(me.z) + 5) me.vz += FX(0.4);
        if (to_int(target->z) < to_int(me.z) - 5) me.vz -= FX(0.4);
        if (mode == 7) {
            const int previous_y = to_int(me.y);
            if (me.vy < FX(4.0)) me.vy += FX(0.4);
            me.y += me.vy;
            if (previous_y > -25) { me.frame = 60; me.vx = me.vy = me.vz = 0; }
        } else if (mode == 2 || mode == 4 || mode == 12) {
            me.vy = me.vy * 183 / 256;
            if (target->kind == 0) {
                if (target->y > me.y + FX(40.0)) me.y += FX(1.0);
                if (target->y < me.y + FX(40.0)) me.y -= FX(1.0);
            } else if (me.y > 0) me.y += FX(1.0);
        }
        if (me.vx > FX(14.0)) me.vx = FX(14.0);
        if (me.vx < -FX(14.0)) me.vx = -FX(14.0);
        if (me.y > FX(1.4)) me.y = FX(1.4);
        const fx lim = mode == 14 ? FX(1.5) : FX(2.2);
        if (me.vz > lim) me.vz = lim;
        if (me.vz < -lim) me.vz = -lim;
        me.facing = me.vx > 0 ? 0 : 1;
        const fx speed = me.vx < 0 ? -me.vx : me.vx;
        if (mode == 2) {
            if (speed <= FX(14.0)) {
                if (speed <= FX(7.0)) { if (me.frame != 1 && me.frame != 2) me.frame = 1; }
                else if (me.frame != 3 && me.frame != 4) me.frame = 3;
            } else if (me.frame != 5 && me.frame != 6) me.frame = 5;
        } else if (mode == 14) {
            if (speed < FX(8.0)) { if (me.frame < 10) me.frame += 50; }
            else if (me.frame > 40) me.frame -= 50;
        }
    } else {
        me.vx += me.vx >= 0 ? FX(2.0) : -FX(2.0);
        if (me.vx > FX(17.0)) me.vx = FX(17.0);
        if (me.vx < -FX(17.0)) me.vx = -FX(17.0);
        if (mode == 7) {
            const int previous_y = to_int(me.y);
            if (me.vy < FX(4.0)) me.vy += FX(0.4);
            me.y += me.vy;
            if (previous_y > -25) { me.frame = 60; me.vx = me.vy = me.vz = 0; }
        } else if (me.y > FX(1.4)) me.y = FX(1.4);
        me.facing = me.vx > 0 ? 0 : 1;
        if (mode == 2) {
            const fx speed = me.vx < 0 ? -me.vx : me.vx;
            if (speed > FX(14.0)) { if (me.frame != 5 && me.frame != 6) me.frame = 5; }
            else if (speed > FX(7.0)) { if (me.frame != 3 && me.frame != 4) me.frame = 3; }
            else if (me.frame != 1 && me.frame != 2) me.frame = 1;
        }
    }
}

}  // namespace lf2
