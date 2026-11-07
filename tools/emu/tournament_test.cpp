// SPDX-License-Identifier: MIT
// g++ -std=c++20 -fsanitize=address,undefined -Igame tools/emu/tournament_test.cpp game/tournament.cpp -o /tmp/tournament_test && /tmp/tournament_test
#include "tournament.hpp"
#include <cassert>
#include <initializer_list>
using namespace lf2;
int main() {
    Tournament t; for(int i=0;i<8;++i) { t.entrants[i].fighter=i; t.entrants[i].original_id=i; t.entrants[i].hp=500; }
    t.entrants[0].human=true; t.entrants[0].transform_id=8; t.start(false,17);
    int games=0;
    while(!t.finished()) {
        TournamentMatch m=t.current(); assert(m.count==2);
        if(!t.simulate()) { int hp[]={300,200}; bool alive[]={true,false}; t.result(1,hp,alive); }
        assert(++games<=7);
    }
    assert(games==7 && t.round()==3 && t.champion()>=0);
    assert(t.entrants[0].transform_id==8);
    for(int i=0;i<8;++i) { t.entrants[i].human=false; t.entrants[i].hp=500; }
    t.entrants[0].human=true; t.start(true,18); games=0;
    while(!t.finished()) {
        TournamentMatch m=t.current(); assert(m.count==4);
        if(!t.simulate()) { int hp[]={300,200,100,50}; bool alive[]={true,false,false,false}; t.result(1,hp,alive); assert(t.entrants[m.entrants[1]].hp==100); }
        assert(++games<=3);
    }
    assert(games==3 && t.round()==2 && t.champion()>=0);
    for (int difficulty=-1; difficulty<=2; ++difficulty) {
        for (bool team : {false,true}) {
            Tournament c;
            for (int i=0;i<8;++i) { c.entrants[i].fighter=i; c.entrants[i].original_id=i; c.entrants[i].hp=500; }
            c.start(team,9,difficulty);
            auto m=c.current(); assert(c.simulate());
            int changed=0;
            for (int i=0;i<m.count;++i) {
                int hp=c.entrants[m.entrants[i]].hp;
                if (hp != 500) {
                    ++changed;
                    int factor=team ? 18 : 12;
                    int spread=difficulty*factor+15;
                    int low=(85-difficulty*factor)*5;
                    int high=(85-difficulty*factor+(spread>0 ? spread-1 : 0))*5;
                    assert(hp>=low && hp<=high);
                }
            }
            assert(changed==(team ? 2 : 1));
        }
    }
}
