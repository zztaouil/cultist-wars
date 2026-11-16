#define LOCAL
#define CW_LIB
#define NN_H 64
#include "engine.cpp"
#include <cassert>

int main() {
    initNet();
    assert(nnLoaded);
    std::mt19937 rng(941100);
    int transitions = 0;
    for (int game = 0; game < 16; ++game) {
        Map map = makeMap(rng);
        initTables(map.grid);
        State state = fromObs(map.start, 42, 0, nullptr);
        while (!over(state)) {
            Move moves[256]; int scores[256];
            int n = gen(state, moves, scores, 0);
            assert(n > 0 && n <= 256);
            for (int i = 0; i < n; ++i) {
                State child = state;
                apply(child, moves[i]);
                State rebuilt = child;
                rehash(rebuilt);
                assert(memcmp(child.acc, rebuilt.acc, sizeof child.acc) == 0);
                assert(child.hash == rebuilt.hash);
                assert(child.occ == rebuilt.occ);
                assert(memcmp(child.at, rebuilt.at, sizeof child.at) == 0);
                assert(nnEval(child) == nnEval(rebuilt));
                ++transitions;
            }
            apply(state, moves[rng() % n]);
        }
    }
    printf("PASS: %d legal transitions, incremental NNUE/cache parity\n", transitions);
}
