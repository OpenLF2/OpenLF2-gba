// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// The computer opponents: they approach the nearest enemy, attack, block, run and perform the
// defend-direction-attack special moves by pressing keys like a player does.

#include "world.hpp"

namespace lf2 {

namespace {
enum : std::uint8_t { K_U = 1, K_D = 2, K_L = 4, K_R = 8, K_A = 16, K_J = 32, K_DEF = 64 };

Keys decode(std::uint8_t k) {
    Keys r;
    r.up = k & K_U; r.down = k & K_D; r.left = k & K_L; r.right = k & K_R;
    r.attack = k & K_A; r.jump = k & K_J; r.defend = k & K_DEF;
    return r;
}
int absi(int v) { return v < 0 ? -v : v; }

}  // namespace

void World::rebuild_scan() {
    scan_count = 0; scan_objects = 0;
    for (int i = 0; i < max_ents; ++i) {
        if (!ents[i].live) continue;
        if (i < max_fighters) scan_objects = scan_count + 1;
        scan_list[scan_count++] = std::uint8_t(i);
    }
    scan_valid = true;
}

void World::ai_control(int index) {
    if (!scan_valid) rebuild_scan();
    ai_control_body(index);
    if (!in_tick) scan_valid = false;
}

void World::ai_control_body(int index) {
    Ent& e = ents[index];
    Ai& ai = e.ai;
    const ObjDef& d = *e.def;
    const Frame& f = d.frame(e.frame);
    if (f.state == 9 && e.catching >= 0) {
        // Holding an enemy: a few punches, then a throw.
        ai.timer++;
        std::uint8_t k = 0;
        if (ai.timer < 40) { if ((ai.timer - 8 * sdiv(ai.timer, 8)) == 0) k = K_A; }
        else { k = std::uint8_t(K_A | (e.facing == 0 ? K_R : K_L)); ai.timer = 0; }
        e.keys = decode(k);
        return;
    }
    if (e.hp <= 0 || f.state == 9 || f.state == 10 || f.state == 11 || f.state == 12 || f.state == 13 || f.state == 14 || f.state == 16 || f.state == 18) {
        e.keys = Keys();
        return;
    }
    const int id = d.hdr->id;
    const bool stage_friendly = ai_stage_mode && e.team == 1 && (index == 0 || id < 30);
    const int skill = ai_random_game || stage_friendly || ai_difficulty < 0 ? 0 : ai_difficulty;
    if (ai_stage_mode && stage_exit) {
        e.keys = Keys(); e.keys.right = true; e.prev.right = false;
        if (e.blocked_right) { e.keys.attack = true; e.prev.attack = false; }
        return;
    }
    if (e.order_x > -1000) {
        const int dx = e.order_x - to_int(e.x), dz = e.order_z - to_int(e.z);
        e.keys = Keys();
        if (dx > 6) e.keys.right = true; else if (dx < -6) e.keys.left = true;
        if (dx > 250 && rnd(skill * 3 + 3) == 0) e.prev.right = false;
        if (dx < -250 && rnd(skill * 3 + 3) == 0) e.prev.left = false;
        if (f.state == 2 && dx > 6 && dx < 100 && e.facing == 0) e.keys.left = true;
        if (f.state == 2 && dx < -6 && dx > -100 && e.facing == 1) e.keys.right = true;
        if (dz > 3) e.keys.down = true; else if (dz < -3) e.keys.up = true;
        if (e.blocked_right || e.blocked_left) { e.keys.attack = true; e.prev.attack = false; }
        if (absi(dx) <= 90 && absi(dz) <= 90) e.order_x = e.order_z = -1000;
        return;
    }
    // Stage allies only fight the scripted enemy team; stage enemies can also
    // target independent objects. Other modes use the usual different-team rule.
    const auto hostile = [&](int team) {
        return team != e.team && (!ai_stage_mode || e.team == 2 || team == 2);
    };
    // Stage fighters that get too far ahead of the leading fighter stop advancing (1) or turn back (2).
    int edge_flag = 0;
    if (ai_stage_mode && e.team != 2) {
        int best_x = -1, best_z = 0;
        for (int i = 0; i < max_fighters; ++i) {
            const Ent& a = ents[i];
            if (i != index && a.live && a.kind == 0 && a.hp > 0 && to_int(a.x) > best_x) { best_x = to_int(a.x); best_z = to_int(a.z); }
        }
        if (best_x > -1) {
            const int x = to_int(e.x);
            if (x > best_x && absi(to_int(e.z) - best_z) / 2 - best_x + x > 200) edge_flag = 1;
            if (x > best_x + 400) edge_flag = 2;
        }
    }
    // Pick the nearest living enemy.
    int best = 100000, healing_threat_distance = 10000, target = -1;
    int waiting = -1, waiting_distance = 10000;
    bool close_target = false;
    for (int n = 0; n < scan_count; ++n) {
        const int i = scan_list[n];
        const Ent& o = ents[i];
        if (i == index || !o.live || o.kind != 0 || o.hp <= 0 || !hostile(o.team)) continue;
        const int horizontal = absi(to_int(o.x) - to_int(e.x));
        const int depth = absi(to_int(o.z) - to_int(e.z));
        if (o.def->frame(o.frame).state == 14 || absi(o.blink) > 2) {
            if (horizontal < 250 && depth < 40 && horizontal + depth < waiting_distance) {
                waiting = i; waiting_distance = horizontal + depth;
            }
            continue;
        }
        const int dist = horizontal + depth;
        if (horizontal + depth < healing_threat_distance) healing_threat_distance = horizontal + depth;
        if (dist < best) { best = dist; target = i; close_target = depth < 15; }
    }
    const int previous = ai.target;
    bool keep_previous = false;
    if (previous >= 0 && previous < max_ents) {
        const Ent& old = ents[previous];
        keep_previous = old.live && old.hp > 0 && old.kind == 0 && rnd(30) > 0;
    }
    if (keep_previous) {
        target = previous;
        const Ent& old = ents[previous];
        waiting = (old.def->frame(old.frame).state == 14 || absi(old.blink) > 2) ? previous : -1;
    }
    bool follow_drink = false;
    if (e.follow_order && e.held_item >= 0 && e.held_item < max_ents && ents[e.held_item].live) {
        const int held_id = ents[e.held_item].def->hdr->id;
        follow_drink = held_id == 122 || held_id == 123;
    }
    if (waiting >= 0 && e.follow_order && (follow_drink ||
        absi(to_int(ents[waiting].x) - to_int(e.x)) > 240 || absi(to_int(ents[waiting].z) - to_int(e.z)) > 150)) {
        target = waiting; waiting = -1;
    }
    if (waiting >= 0) {
        const Ent& other = ents[waiting];
        const int ox = to_int(other.x), oz = to_int(other.z);
        const int limit = stage_bound > 0 ? stage_bound : stage.width;
        e.keys = Keys(); ai.target = waiting;
        if (ox > limit - 30) e.keys.left = true;
        else if (ox < 30) e.keys.right = true;
        else if (absi(oz - to_int(e.z)) > 45 && absi(ox - to_int(e.x)) > 350) return;
        else {
            if (ox > to_int(e.x)) e.keys.left = true; else e.keys.right = true;
            if (oz < to_int(e.z)) e.keys.down = true;
            else if (oz >= stage.z1 + 10) e.keys.up = true;
            else e.keys.down = true;
        }
        return;
    }
    // Flying balls can be the nearest hostile target even when their owner is
    // gone. Turn toward one and defend if it is moving toward this fighter.
    int incoming = -1, incoming_distance = healing_threat_distance;
    for (int n = scan_objects; !keep_previous && n < scan_count; ++n) {
        const int i = scan_list[n];
        const Ent& ball = ents[i];
        if (!ball.live || ball.kind != 3 || !hostile(ball.team) || ball.hp <= 0 || ball.blink > 2 || ball.blink < -2 ||
            ball.def->frame(ball.frame).state != 3000) continue;
        const int bx = to_int(ball.x) - to_int(e.x);
        if (!((bx > 0 && ball.vx < 0) || (bx < 0 && ball.vx > 0))) continue;
        const int distance = absi(bx) + absi(to_int(ball.z) - to_int(e.z));
        if (distance < incoming_distance) { incoming = i; incoming_distance = distance; }
    }
    if (incoming >= 0) {
        const Ent& ball = ents[incoming];
        const int bx = to_int(ball.x) - to_int(e.x);
        const int chance = skill * 3;
        e.keys = Keys(); ai.target = incoming;
        if (bx > 0 && e.facing == 1) e.keys.right = true;
        if (bx < 0 && e.facing == 0) e.keys.left = true;
        if (absi(bx) < 200 && f.state != 7 && e.defend_lock == 0 && (chance == 0 || rnd(chance) == 0)) e.keys.defend = true;
        return;
    }
    if (target < 0) {
        e.keys = Keys(); ai.target = -1;
        if (edge_flag == 1 && !e.follow_order) e.keys.left = true;
        if ((id == 7 && e.frame >= 255 && e.frame <= 261) ||
            (id == 9 && e.frame >= 280 && e.frame <= 290) ||
            (id == 32 && e.frame >= 240 && e.frame <= 245)) e.keys.defend = true;
        return;
    }
    ai.target = target;
    const int dx = to_int(ents[target].x) - to_int(e.x);
    const int dz = to_int(ents[target].z) - to_int(e.z);
    const int adx = absi(dx), adz = absi(dz);
    const int toward = dx > 0 ? 0 : 1;
    if (follow_drink) {
        e.keys = Keys(); e.keys.attack = true; e.prev.attack = false;
        return;
    }
    std::uint8_t k = 0;
    const bool ordered_far = e.follow_order && (adx > 240 || adz > 150);
    if (e.follow_order) {
        if (dx > 0 && e.facing == 1) k |= K_R;
        else if (dx < 0 && e.facing == 0) k |= K_L;
        if (f.state == 2) k |= e.facing == 0 ? K_L : K_R;
    }
    // Pushing against an obstacle (stage barriers, shutters) is answered with an attack now and then.
    if (rnd(skill * 5 + 8) == 0 && (e.blocked_up || e.blocked_down || e.blocked_left || e.blocked_right)) {
        k |= K_A; e.prev.attack = false;
    }
    ai.timer++;
    bool danger = false;
    for (int n = 0; n < scan_count; ++n) {
        const int i = scan_list[n];
        const Ent& o = ents[i];
        if (!o.live || i == index || !hostile(o.team)) continue;
        const Frame& of = o.def->frame(o.frame);
        if (of.nitr == 0 || absi(to_int(o.z) - to_int(e.z)) > 20) continue;
        const int ox = to_int(o.x) - to_int(e.x);
        const bool facing_me = (ox < 0 && o.facing == 0) || (ox > 0 && o.facing == 1);
        if (facing_me && absi(ox) < (o.kind == 0 ? 70 : 110)) { danger = true; break; }
    }

    // Injured fighters move into a friendly John's healing field. A hostile
    // field, or a healthy healer's own field, suppresses this diversion.
    int healing_field = -1;
    bool field_danger = false;
    bool hazard_left = false, hazard_right = false, hazard_up = false, hazard_down = false;
    bool hazard_aligned = false;
    const bool wants_field = (e.hp < e.dark_hp - 70 && e.hp < 140) ||
                             (e.hp < sdiv(e.dark_hp * 3, 5) && e.hp >= 140);
    const bool healthy_healer = (id == 2 || id == 34) &&
        !(e.hp < e.stage_max_hp - 70 && e.hp < e.stage_max_hp - 200) &&
        !(e.hp < sdiv(e.stage_max_hp * 3, 5) && e.hp >= e.stage_max_hp - 200);
    for (int n = scan_objects; n < scan_count; ++n) {
        const int i = scan_list[n];
        const Ent& ball = ents[i];
        if (!ball.live) continue;
        const int object_id = ball.def->hdr->id;
        const int bx = to_int(ball.x) - to_int(e.x), bz = to_int(ball.z) - to_int(e.z);
        if ((object_id == 211 && ball.def->frame(ball.frame).state == 18) ||
            (object_id == 212 && ball.frame >= 150 && ball.frame <= 170)) {
            if (absi(bx) < 80) {
                if (bz > 20) hazard_down = true; else if (bz < -20) hazard_up = true;
            }
            if (absi(bz) < 20) {
                if (bx > 100) hazard_right = true; else if (bx < -100) hazard_left = true;
            }
        }
        if (object_id != 200) continue;
        const int decade = sdiv(ball.frame, 10);
        if ((decade == 6 && ball.team != e.team) ||
            (decade == 5 && healthy_healer && ball.team == e.team)) {
            field_danger = true;
            if (absi(bz) < 25 && absi(bx) < 150) {
                hazard_aligned = true;
                if (absi(bz) < 20) {
                    if (bz > 0) hazard_down = true; else hazard_up = true;
                    if (bx > 0) hazard_right = true; else hazard_left = true;
                }
            }
        }
        if (decade == 5 && ball.team == e.team && wants_field &&
            absi(to_int(ball.x) - to_int(e.x)) < 300 &&
            absi(to_int(ball.z) - to_int(e.z)) < 90) healing_field = i;
    }
    if (!field_danger && healing_field >= 0) {
        const Ent& ball = ents[healing_field];
        const int bx = to_int(ball.x) - to_int(e.x), bz = to_int(ball.z) - to_int(e.z);
        e.keys = Keys();
        if (bx > 7) e.keys.right = true; else if (bx < -7) e.keys.left = true;
        if (bz > 2) e.keys.down = true; else if (bz < -2) e.keys.up = true;
        return;
    }

    // Continue or end character-specific sustained moves according to target position.
    if ((id == 1 || id == 7 || id == 51) && rnd(skill * 5 + 1) == 0) {
        const Ent& other = ents[target];
        const int other_state = other.def->frame(other.frame).state;
        const bool forward = (dx > 0 && e.facing == 0) || (dx < 0 && e.facing == 1);
        if (id == 1 && e.frame >= 260 && e.frame <= 289 && adx < 100 && adz < 7) {
            e.keys = Keys();
            if (other.y != 0 || e.y != 0 || rnd(3) != 0) {
                if (other.y >= 0 || e.y >= 0 || rnd(7) != 0) {
                    if ((other.y < 0 && rnd(5) == 0) || rnd(30) == 0) {
                        if (forward) e.keys.jump = true;
                        e.prev.jump = false;
                    }
                    return;
                }
            }
            e.keys.attack = true; e.prev.attack = false;
            return;
        }
        bool stop = id == 51 && e.frame > 265 && e.frame < 280 && (adz > 13 || other.kind != 0);
        if (id == 7 && e.frame > 267 && e.frame < 283 &&
            (other_state == 12 || other_state == 11 || adx > 150 || adz > 25 || forward)) stop = true;
        if (id == 7 && e.frame >= 255 && e.frame <= 261) {
            const int self_x = to_int(e.x), limit = stage_bound > 0 ? stage_bound : stage.width;
            if ((e.facing == 0 && (dx < -120 || self_x > limit - 30)) ||
                (e.facing == 1 && (dx > 120 || self_x < 30)) || adz > 70 || edge_flag == 1) stop = true;
            else if ((dz <= 0) == forward) k |= K_U; else k |= K_D;
        }
        if (stop) { e.keys = Keys(); e.keys.defend = true; e.prev.defend = false; return; }
    }
    if ((id == 10 || id == 11) && rnd(skill * 5 + 1) == 0 && ents[target].y < 0) {
        if (id == 10 && e.frame == 271 && ents[target].def->frame(ents[target].frame).state == 12) {
            e.keys = Keys(); e.seq[2] = 3;
            return;
        }
        if (id == 11 && f.hit_j == 290) { k |= K_J; e.prev.jump = false; }
    }
    // Jan's healing stars are chosen when any member of her team is badly hurt.
    if (id == 36 && e.mp > 200 && rnd(skill * 5 + 1) == 0 && rnd(5) == 0) {
        e.keys = Keys();
        for (int n = 0; n < scan_count; ++n) {
            const int i = scan_list[n];
            const Ent& ally = ents[i];
            if (!ally.live || ally.kind != 0 || ally.team != e.team) continue;
            if (ally.hp < ally.dark_hp - 200 || (ally.hp < 200 && ally.hp < ally.dark_hp - 100)) {
                e.seq[6] = 3;
                break;
            }
        }
        return;
    }
    // Fighter-specific special choices use the same distance, mana and target
    // state thresholds as the reference before the generic move selection.
    if ((id == 4 || id == 5 || id == 6 || id == 7 || id == 8 || id == 9 || id == 32) &&
        rnd(skill * 5 + 1) == 0) {
        const Ent& other = ents[target];
        const int state = other.def->frame(other.frame).state;
        const bool forward = (dx > 0 && e.facing == 0) || (dx < 0 && e.facing == 1);
        const int direction_jump = dx > 0 ? 4 : 5;
        int sequence = -1;
        if (id == 4) {
            if (e.mp > 360 && adx < 100 && adz < 70 && rnd(sdiv(e.hp, 5) + 10) == 0) sequence = 6;
            else if (rnd(45) == 0 && adx > 100 && adx < 550 && adz < 20 && e.mp > 170) sequence = direction_jump;
            else if (rnd(30) == 0 && e.mp > 200 && adx > 100 && adx < 160 && adz < 55 && forward) sequence = 8;
        } else if (id == 5) {
            if (e.mp > 450 && adx > 100 && adz > 50 && rnd(3) == 0) sequence = rnd(2) == 0 ? 6 : 7;
            else if (e.mp > 70 && adx > 100 && adx < 160 && adz < 8 && rnd(10) == 0) sequence = direction_jump;
            else if (rnd(30) == 0 && e.mp > 200 && adx > 100 && adx < 160 && adz < 55 && forward) sequence = dx > 0 ? 0 : 1;
        } else if (id == 6) {
            if (e.mp >= 101 && adx >= 81 && adx <= 129 && adz <= 29 && rnd(10) == 0) sequence = direction_jump;
            else if (e.mp >= 101 && adx <= 44 && adz <= 4 && rnd(3) == 0) sequence = 6;
        } else if (id == 7) {
            if (state != 18 && state != 14 && state != 12 && e.hp > 70 && e.mp > 320 &&
                (adx > 50 || adz > 10) && adx < 85 && other.hp < e.hp && adz < 35 && rnd(5) == 0) sequence = 6;
            else if (state != 18 && state != 14 && state != 12 && e.mp > 200 && adx > 100 && adx < 370 && adz < 60 && rnd(20) == 0) sequence = direction_jump;
            else if (rnd(100) == 0 && adx > 240 && adx < 400) sequence = direction_jump;
            else if (state != 18 && state != 14 && state != 12 && e.mp > 200 && adx > 60 && adx < 280 && adz < 60 && rnd(15) == 0 && forward) sequence = 7;
        } else if (id == 8) {
            if (state != 13 && e.mp >= 201 && adx <= 399 && adz <= 169 && rnd(250) == 0) sequence = direction_jump;
            else if (state != 13 && state != 14 && e.mp >= 201 && adx >= 61 && adx <= 279 && adz <= 64 && rnd(15) == 0) sequence = direction_jump;
            else if (state != 14 && e.mp > 320 && (adx > 50 || adz > 7 || state == 13) && adx < 125 && adz < 25 && rnd(3) == 0 && forward) sequence = 6;
            else if (rnd(50) == 0 && e.held_item < 0 && e.mp > 200 && adx > 200 && adz > 50) sequence = 7;
        } else if (id == 9) {
            if ((rnd(10) == 0 || state == 16 || state == 8) && absi(to_int(e.vx) + dx) < 120 && adz < 7 && forward) sequence = 3;
            else if ((state != 18 && state != 14 && state != 12 && e.mp > 200 && adx > 75 && adx < 370 && adz < 60 && rnd(13) == 0) ||
                     (rnd(sdiv(other.hp, 4) + 40) == 0 && adx > 150 && adx < 400)) sequence = direction_jump;
            else if (healing_threat_distance < 10000 && rnd(30) == 0 && e.mp > 150) sequence = 2;
        } else {
            if ((state != 18 && state != 14 && state != 12 && e.mp > 200 && adx < 270 && adz < 60 && rnd(60) == 0) ||
                (rnd(sdiv(other.hp, 4) + 40) == 0 && adx > 150 && adx < 400)) sequence = direction_jump;
            else if (adx < 150 && adz < 40 && rnd(15) == 0) sequence = dx > 0 ? 0 : 1;
        }
        if (sequence >= 0) { e.keys = decode(k); e.seq[sequence] = 3; return; }
    }
    if ((id == 35 || id == 36 || id == 38 || id == 39 || id == 50 || id == 51 || id == 52) &&
        rnd(skill * 5 + 1) == 0) {
        const Ent& other = ents[target];
        const int state = other.def->frame(other.frame).state;
        const bool forward = (dx > 0 && e.facing == 0) || (dx < 0 && e.facing == 1);
        const int direction_attack = dx > 0 ? 0 : 1, direction_jump = dx > 0 ? 4 : 5;
        int sequence = -1;
        if (id == 35) {
            if (rnd(7) == 0 && adx < 650 && adx > 40 && adz < 4 && e.mp > 120) sequence = direction_attack;
        } else if (id == 36) {
            if (e.mp > 260 && rnd(10) == 0 && adx < 650 && adz < 240) sequence = 2;
        } else if (id == 38) {
            if (e.mp > 150 && rnd(5) == 0 && adx < 250 && adx > 130 && adz < 10) sequence = direction_jump;
            else if (e.mp > 200 && rnd(10) == 0 && adz < 10) sequence = direction_attack;
            else if (e.mp > 200 && rnd(10) == 0 && (adx > 200 || adz < 250)) sequence = 6;
        } else if (id == 39) {
            if (e.mp > 100 && rnd(3) == 0 && adx < 120 && forward && adz < 10) sequence = 3;
            else if (e.mp > 100 && rnd(7) == 0 && adx < 250 && adz < 10) sequence = direction_attack;
        } else if (id == 50) {
            if (rnd(7) == 0 && adx >= 91 && adx <= 499 && adz <= 3 && e.mp >= 151) {
                if (other.frame == 263 || other.frame == 264) { k |= K_A; e.prev.attack = false; }
                else sequence = direction_attack;
            }
            if (sequence < 0 && rnd(7) == 0 && adx < 100 && adz < 7 && e.mp > 75) sequence = 3;
        } else if (id == 51) {
            if (e.mp > 300 && rnd(10) == 0 && adx < 300 && adz < 200) sequence = 6;
            else if (e.mp > 300 && rnd(10) == 0 && adx < 950) sequence = 2;
            else if (rnd(5) == 0 && e.mp >= 251 && adx >= 41 && adx <= 1199 && adz <= 12) sequence = direction_jump;
        } else {
            if (state == 3 && e.mp > 125 && rnd(10) == 0 && adx < 120 && adz < 10) sequence = 8;
            else if (e.mp > 125 && rnd(5) == 0 && adx < 100 && adz < 30) {
                if (dx <= 0) { e.keys = decode(k); return; }
                sequence = 6;
            } else if (e.mp > 125 && rnd(14) == 0 && adx < 700 && adz < 150) sequence = direction_attack;
            else if (e.mp > 125 && rnd(5) == 0 && adz < 20) sequence = direction_jump;
            else if ((rnd(5) == 0 || state == 16 || state == 8) && absi(to_int(e.vx) + dx) < 100 && adz < 7 && e.mp < 100 && forward) sequence = 2;
        }
        if (sequence >= 0) { e.keys = decode(k); e.seq[sequence] = 3; return; }
    }

    // John and Sorcerer help an injured teammate when that teammate is much
    // closer than the enemy and the healer is not in immediate danger.
    const bool healer_healthy = (e.hp >= e.dark_hp - 70 || e.hp >= 140) &&
                                (e.hp >= sdiv(e.dark_hp * 3, 5) || e.hp < 140);
    const bool healer_injured = (e.hp < e.dark_hp - 70 && e.hp < 140) ||
                                (e.hp < sdiv(e.dark_hp * 3, 5) && e.hp >= 140);
    const bool healer_turn = (id == 2 || id == 34) && !danger && !field_danger && e.mp > 350 &&
                             rnd(skill * 5 + 1) == 0;
    if (healer_turn && healer_injured &&
        (f.state == 0 || f.state == 1) && rnd(10) == 0) {
        e.keys = Keys(); e.seq[7] = 3;
        return;
    }
    if (healer_turn && healer_healthy &&
        (e.held_item < 0 || e.frame < 9)) {
        for (int i = 0; i < max_fighters; ++i) {
            if (stage_mode && i > 0) continue;
            const Ent& ally = ents[i];
            if (i == index || !ally.live || ally.kind != 0 || ally.team == 0 || ally.team != e.team || ally.hp <= 0) continue;
            const int ax = to_int(ally.x) - to_int(e.x), az = absi(to_int(ally.z) - to_int(e.z));
            const int distance = absi(ax);
            const bool injured = (ally.hp < ally.dark_hp - 90 && ally.hp < 140) ||
                                 (ally.hp < sdiv(ally.dark_hp * 3, 5) && ally.hp >= 140);
            if (!injured || distance >= 250 || az >= 60 || distance + az >= sdiv(healing_threat_distance, 3)) continue;
            e.keys = Keys();
            if (ax > 0) e.keys.right = true; else e.keys.left = true;
            if ((ax > 0 && e.facing == 0) || (ax < 0 && e.facing == 1) || distance < 5) e.seq[6] = 3;
            return;
        }
    }

    if ((id == 1 || id == 2 || id == 10 || id == 11 || id == 33) && rnd(skill * 5 + 1) == 0) {
        const Ent& other = ents[target];
        const int state = other.def->frame(other.frame).state;
        const bool forward = (dx > 0 && e.facing == 0) || (dx < 0 && e.facing == 1);
        const int direction_jump = dx > 0 ? 4 : 5;
        int sequence = -1;
        if (id == 2) {
            if (healing_threat_distance < 10000 && rnd(30) == 0 && e.mp > 250) sequence = 2;
            else {
                const int other_id = other.def->hdr->id;
                const bool projectile_user = other_id == 2 || other_id == 9 || other_id == 10 ||
                                             other_id == 11 || other_id == 33 || other_id == 34;
                if (rnd(15) == 0 && adx > 100 && adx < (projectile_user ? 500 : 250) && adz < 30 &&
                    e.mp > 100 && other.mp > (projectile_user ? 220 : 170) && !hazard_aligned) sequence = direction_jump;
            }
        } else if (id == 1) {
            const bool first = rnd(7) == 0 && adx < 150 && adz < 8 && e.mp > 150 &&
                ((rnd(10) == 0 && state != 3) || (rnd(3) > 0 && (state == 16 || state == 8 || state == 11)));
            if (first) sequence = direction_jump;
            else if (rnd(7) == 0 && adx < 100 && adz < 7 && e.mp > 75) {
                if (e.mp > 150 && ((rnd(10) == 0 && state != 3) || (rnd(3) > 0 && state == 16))) sequence = direction_jump;
                else sequence = 3;
            }
        } else if (id == 10 || id == 11) {
            if (e.mp > (id == 10 ? 100 : 150) && adx < 280 && adz < (id == 10 ? 25 : 30) && rnd(10) == 0 && forward) {
                if (dx < 0) { e.keys = decode(k); return; }
                sequence = 3;
            } else if ((rnd(id == 10 ? 10 : 5) == 0 || state == 16 || state == 8) &&
                       absi(to_int(e.vx) + dx) < (id == 10 ? 80 : 100) && adz < 7 &&
                       (id == 10 || e.mp > 200) && forward) sequence = 2;
            else if (id == 10) {
                if (e.mp >= 201 && adx >= 61 && adx <= 279 && adz <= 64 &&
                    (rnd(15) == 0 || (rnd(4) == 0 && (state == 16 || state == 8 || (state == 12 && other.y <= -FX(41)))))) sequence = direction_jump;
                else {
                    if (e.hp < 250 && e.hp < other.hp + 50 && rnd(20) == 0 && e.mp > 75 && e.held_item < 0) {
                        for (int n = 0; n < scan_count; ++n) {
                            const int i = scan_list[n];
                            const Ent& ally = ents[i];
                            if (i != index && ally.live && ally.kind == 0 && ally.team == e.team && other.hp < ally.hp &&
                                absi(to_int(ally.x) - to_int(e.x)) + absi(to_int(ally.z) - to_int(e.z)) > 300) { e.seq[7] = 3; break; }
                        }
                    }
                    if (other.hp < e.hp && rnd(70) == 0 && e.mp > 500) e.seq[6] = 3;
                }
            }
        } else if ((rnd(5) == 0 || state == 16 || state == 8) &&
                   absi(to_int(e.vx) + dx) < 60 && adz < 7 && e.mp > 150 && forward) sequence = 2;
        if (sequence >= 0) { e.keys = decode(k); e.seq[sequence] = 3; return; }
    }

    if (e.held_item >= 0 && e.held_item < max_ents && ents[e.held_item].live) {
        const Ent& item = ents[e.held_item];
        const int item_id = item.def->hdr->id;
        if ((item_id == 122 || item_id == 123) && rnd(skill * 3 + 1) == 0) {
            e.keys = Keys();
            if (f.state == 17 && close_target && !hazard_aligned && e.blink != 0) {
                e.keys.defend = true;
                return;
            }
            if (ordered_far) return;
            const int target_z = to_int(ents[target].z), self_z = to_int(e.z);
            if (stage.z1 + 30 <= target_z && (target_z < stage.z2 - 30 || self_z < target_z)) e.keys.up = true;
            else e.keys.down = true;
            const int target_x = to_int(ents[target].x), self_x = to_int(e.x);
            const int limit = stage_bound > 0 ? stage_bound : stage.width;
            if (target_x < 400 && self_x < 200) e.keys.right = true;
            else if (target_x > limit - 400 && self_x > limit - 200) e.keys.left = true;
            else if (adx < 350 && adz < 70) {
                if (self_x < target_x) e.keys.left = true; else e.keys.right = true;
            } else if (f.state == 2) {
                if (e.facing == 0) e.keys.left = true; else e.keys.right = true;
            } else if (rnd(5) == 0) {
                if (!hazard_aligned && (id == 2 || id == 34) && e.mp > 150 && rnd(skill * 3 + 3) > 0)
                    e.seq[dx <= 0 ? 5 : 4] = 3;
                else e.keys.attack = true;
            }
            return;
        }
    } else if (!danger && !field_danger && e.held_item < 0) {
        bool needs_help = e.follow >= 0;
        bool strongest = false;
        const bool cautious = e.follow >= 0 || e.mp > 250 ||
                              (ai_stage_mode && e.team == 1) ||
                              (stage_mode && !ai_stage_mode && e.army_side >= 0);
        if (stage_mode && (!ai_stage_mode || e.team != 2)) {
            strongest = e.hp <= sdiv(e.stage_max_hp * 4, 5) && e.hp <= e.stage_max_hp - 130;
            int teammates = 0;
            if (e.hp > 430 || e.hp > e.stage_max_hp - 130) needs_help = true;
            for (int n = 0; n < scan_count; ++n) {
                const int i = scan_list[n];
                const Ent& ally = ents[i];
                if (i == index || !ally.live || ally.kind != 0 || ally.team != e.team || ally.hp <= 0) continue;
                ++teammates;
                if (ally.hp < e.hp) strongest = false;
                if (ally.hp < e.hp - 200) needs_help = true;
            }
            if (!teammates) strongest = false;
        }
        int nearest = -1, distance = 10000;
        for (int n = scan_objects; n < scan_count; ++n) {
            const int i = scan_list[n];
            const Ent& item = ents[i];
            if (!item.live || item.holder >= 0 || (item.kind != 1 && item.kind != 2 && item.kind != 4 && item.kind != 6)) continue;
            const int state = item.def->frame(item.frame).state;
            if (state != 1004 && state != 2004) continue;
            if (strongest && item.def->hdr->id == 122 && state == 1004) { nearest = i; break; }
            if (close_target || (e.follow_order && item.def->hdr->id != 122)) continue;
            if ((needs_help && item.def->hdr->id == 122) || (cautious && item.def->hdr->id == 123)) continue;
            const int item_dx = absi(to_int(item.x) - to_int(e.x)), item_dz = absi(to_int(item.z) - to_int(e.z));
            const int score = item_dx + item_dz;
            if (score < healing_threat_distance * 2 && score < distance) { nearest = i; distance = score; }
        }
        if (nearest >= 0) {
            const Ent& item = ents[nearest];
            e.keys = Keys();
            const int item_dx = to_int(item.x) - to_int(e.x), item_dz = to_int(item.z) - to_int(e.z);
            if (item_dx > 6) e.keys.right = edge_flag == 0;
            else if (item_dx < -6) e.keys.left = true;
            if (f.state == 2 && ((item_dx > 6 && item_dx < 100 && e.facing == 0) ||
                                 (item_dx < -6 && item_dx > -100 && e.facing == 1))) {
                if (e.facing == 0) e.keys.left = true; else e.keys.right = true;
            }
            if (item_dz > 3) e.keys.down = true;
            else if (item_dz < -3) e.keys.up = true;
            if (absi(item_dx) <= 6 && absi(item_dz) <= 3) { e.keys.attack = true; e.prev.attack = false; }
            return;
        }
    }

    // Movement and melee.
    const bool keep_distance = id == 4 || id == 5 || id == 31 ||
        (ai_stage_mode && index >= max_fighters && e.team != 2 &&
         (ents[target].hp > e.hp * 2 || (e.hp <= 100 && e.stage_max_hp > 100)));
    if (!ordered_far && f.state != 19 && !hazard_aligned && adz > 3) {
        if (dz > 0 && !hazard_down) k |= K_D;
        else if (dz < 0 && !hazard_up) k |= K_U;
    }
    if (!ordered_far && f.state != 19 && (hazard_left || hazard_right)) {
        if (hazard_up && !hazard_down) k |= K_D;
        if (hazard_down && !hazard_up) k |= K_U;
    }
    const bool wrong_facing = e.facing != toward;
    const bool approach = keep_distance ? (adx > 170 || (adx > 150 && wrong_facing) ||
                                            (f.state == 7 && wrong_facing))
                                        : (adx > 60 || (adx > 0 && wrong_facing));
    if (!ordered_far && f.state != 19 && approach) {
        if (toward == 0 && !hazard_right && (edge_flag == 0 || (e.facing == 1 && !keep_distance))) k |= K_R;
        else if (toward == 1 && !hazard_left) k |= K_L;
        // Run: tap the direction twice quickly, then hold it.
        if (adx > 190 && (f.state == 1 || f.state == 0)) {
            const int phase = (ai.timer - 40 * sdiv(ai.timer, 40));
            if (phase == 2 || phase == 3) k &= std::uint8_t(~(K_L | K_R));
        }
    }
    if (edge_flag == 1 && f.state == 2 && e.facing == 0 && !ordered_far) k |= K_L;
    if (e.held_item >= 0 && e.held_item < max_ents && ents[e.held_item].live &&
        rnd(skill * 3 + 1) == 0) {
        const int weapon_id = ents[e.held_item].def->hdr->id;
        const bool light_weapon = weapon_id == 100 || weapon_id == 101 || weapon_id == 120 ||
                                  weapon_id == 121 || weapon_id == 124;
        const bool heavy_weapon = weapon_id == 150 || weapon_id == 151;
        const int predicted_dx = absi(dx - 2 * to_int(e.vx));
        if (f.state == 2 && rnd(skill * 3 + 5) == 0) k |= K_A;
        if (light_weapon && predicted_dx < 115 && adz < 6 && rnd(skill * 3 + 3) == 0) k |= K_A;
        if (weapon_id == 124 && rnd(skill * 15 + 30) == 0) k |= K_A;
        if (heavy_weapon && predicted_dx < 300 && adz < 6 && rnd(skill * 5 + 7) == 0) k |= K_A;
        if (light_weapon && adx < 600 && adz < 20 &&
            (!e.follow_order || (adx <= 240 && adz <= 150)) && rnd(skill * 3 + 5) == 0) {
            if (dx > 0) { if (edge_flag == 0) { k |= K_R; e.prev.right = false; } }
            else if (dx < 0) { k |= K_L; e.prev.left = false; }
        }
    }
    const int target_state = ents[target].def->frame(ents[target].frame).state;
    const bool ranged_fighter = id == 4 || id == 5 || id == 31;
    if (rnd(skill * 7 + 10) == 0 && (target_state == 3 || sdiv(target_state, 100) == 3) && adz < 9 &&
        ((dx < 0 && ents[target].facing == 0) || (dx > 0 && ents[target].facing == 1))) k |= K_DEF;
    if (target_state != 14 && adz < 5 && absi(dx - 2 * to_int(e.vx)) < 80) {
        if ((!ranged_fighter || target_state == 16) && rnd(skill * 3 + 3) == 0) k |= K_A;
        if (rnd(skill * 3 + 3) == 0) k |= K_A;
    }
    const bool fragile_stage_ally = ai_stage_mode && index >= max_fighters && e.team != 2 &&
        (ents[target].hp > e.hp * 2 || (e.hp <= 100 && e.stage_max_hp > 100));
    const bool evade = ((e.held_item < 0 && target_state != 16 && (ranged_fighter || id == 36)) ||
                        fragile_stage_ally) && dx < 100 && adz < 80 && f.state != 7 &&
                       rnd(skill * 3 + 2) == 0;
    if (evade && !ordered_far) {
        const int self_x = to_int(e.x), target_x = to_int(ents[target].x);
        const int limit = stage_bound > 0 ? stage_bound : stage.width;
        if ((target_x < 250 || target_x < self_x) && target_x <= limit - 250) {
            k |= K_R; e.prev.right = false;
        } else if (target_x > limit - 250 || target_x > self_x) {
            k |= K_L; e.prev.left = false;
        }
        if (rnd(17) == 0) k |= K_J;
    }
    if (e.held_item < 0 && (ranged_fighter || id == 36) && adz < 5 &&
        !evade && target_state != 14 && e.facing == toward) {
        const int limit = ranged_fighter && target_state == 16 ? 350 : 300;
        if (absi(dx - 2 * to_int(e.vx)) < limit && rnd(skill * 3 + 3) == 0) k |= K_A;
    }
    const bool projectile_fighter = id == 2 || id == 4 || id == 6 || id == 7 || id == 8 ||
                                    id == 9 || id == 10 || id == 11 || id == 33 || id == 34;
    const int projected_target = absi(dx + 2 * to_int(ents[target].vx));
    if (!(hazard_right && dx > 0) && !(hazard_left && dx < 0) && target_state != 14 &&
        rnd(skill * 3 + 1) == 0) {
        if ((projectile_fighter || id == 1) && projected_target > 100 &&
            projected_target < (id == 1 ? 300 : 900) && adz < 5 &&
            rnd((id == 1 ? skill * 5 : skill * 3) + 10) == 0) k |= K_DEF;
        if ((projectile_fighter || id == 1) && projected_target > 90 && e.facing == toward &&
            (e.frame == 110 || e.frame > 234) && adz < (id == 1 ? 7 : 13)) {
            e.prev.left = e.prev.right = e.prev.attack = false;
            k |= toward == 0 ? K_R : K_L;
            k |= id == 34 && rnd(2) == 0 ? K_J : K_A;
        }
    }
    if (!ordered_far && target_state != 14 && rnd((skill * 5 + 10) * 2) < 3 && rnd(20) < 3) k |= K_J;
    e.keys = decode(k);
}

}  // namespace lf2
