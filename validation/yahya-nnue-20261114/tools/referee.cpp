// Fast native referee for CodinGame Cultist Wars. Mirrors the official Java referee
// (bitbucket.org/Nixerrr/cultist-wars): same protocol, same action validation and
// pathfinding, same bullet lines, same neutral-move generator (model.Random, seed 42).
// The simulation itself is the bot's (bot.cpp is included), so there is one rule set.
//
//   referee BOT_A BOT_B [-g games] [-j procs] [-t ms] [-f first_ms] [-n nodes] [-nb nodes_b]
//                       [-s seed] [-x grace_ms] [-v]
//   referee --check FILE     replay a recorded CodinGame game (see tools/checklog.py)
//
// Games come in pairs: each random map is played twice with the seats swapped.
// Prints one JSON line per game with -v, then a JSON summary line.
#define CW_LIB
#include "../engine.cpp"
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// ---- official action handling (Board.checkActionValidity + getFinalAction) -----------------
static bool freeTile(const State& s, int c) { return !wallAt[c] && s.at[c] < 0; }

// Board.findPath (through free tiles) and Board.getTileInDirection (through floor, units
// ignored): BFS in the order up, right, down, left; returns the first step, or -1.
static int firstStep(const State& s, int from, int target, bool ignoreUnits) {
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    int q[NC], first[NC], qh = 0, qt = 0;
    bool seen[NC] = {false};
    q[qt++] = from; seen[from] = true; first[from] = -1;
    while (qh < qt) {
        int c = q[qh++];
        for (int d = 0; d < 4; d++) {
            int x = c % W + DX[d], y = c / W + DY[d];
            if (x < 0 || x >= W || y < 0 || y >= H) continue;
            int nb = y * W + x;
            if (nb == target) return first[c];
            bool pass = ignoreUnits ? !wallAt[nb] : freeTile(s, nb);
            if (pass && !seen[nb]) { seen[nb] = true; first[nb] = c == from ? nb : first[c]; q[qt++] = nb; }
        }
    }
    return -1;
}
static Move moveToward(const State& s, int u, int target) {
    int p = s.pos[u];
    if (p == target) return mk(WAIT, 0, 0);
    if (mdist[p][target] == 1) return freeTile(s, target) ? mk(MOVE, u, target) : mk(WAIT, 0, 0);
    int f = firstStep(s, p, target, false);
    if (f >= 0) return mk(MOVE, u, f);
    f = firstStep(s, p, target, true);
    if (f >= 0 && s.at[f] < 0) return mk(MOVE, u, f);
    return mk(WAIT, 0, 0);
}
static bool toInt(const string& t, int& v) {
    if (t.empty()) return false;
    char* e;
    long l = strtol(t.c_str(), &e, 10);
    if (*e) return false;
    v = (int)l;
    return true;
}
// Turns a bot's output line into the action the referee executes. false = invalid (loss).
static bool resolveAction(const State& s, int player, const string& line, Move& m, string& err) {
    vector<string> tok;
    size_t a = line.find_first_not_of(" \t\r\n"), b = line.find_last_not_of(" \t\r\n");
    string t = a == string::npos ? "" : line.substr(a, b - a + 1);
    for (size_t i = 0;;) {
        size_t j = t.find(' ', i);
        tok.push_back(t.substr(i, j == string::npos ? j : j - i));
        if (j == string::npos) break;
        i = j + 1;
    }
    if (tok.size() == 1) {
        if (tok[0] != "WAIT") { err = "not an action"; return false; }
        m = mk(WAIT, 0, 0);
        return true;
    }
    int u, x, y;
    const string& cmd = tok[1];
    if (cmd != "MOVE" && cmd != "SHOOT" && cmd != "CONVERT") { err = "unknown command"; return false; }
    if (tok.size() < (cmd == "MOVE" ? 4u : 3u) || !toInt(tok[0], u) || !toInt(tok[2], x)) { err = "bad arguments"; return false; }
    if (u < 0 || u >= NU) { err = "invalid unit id"; return false; }
    if (s.own[u] != player) { err = "not your unit"; return false; }
    if (cmd == "MOVE") {
        if (!toInt(tok[3], y)) { err = "bad arguments"; return false; }
        if (x < 0 || x >= W || y < 0 || y >= H) { err = "invalid coordinates"; return false; }
        if (s.hp[u] <= 0) { err = "unit not in game"; return false; }
        m = moveToward(s, u, y * W + x);
        return true;
    }
    int tg = x;
    if (tg < 0 || tg >= NU) { err = "invalid target id"; return false; }
    if (cmd == "SHOOT") {
        if (u == tg) { err = "unit cannot shoot itself"; return false; }
        if (u == player) { err = "cult leaders cannot shoot"; return false; }
        if (s.hp[u] <= 0 || s.hp[tg] <= 0) { err = "unit not in game"; return false; }
        m = mdist[s.pos[u]][s.pos[tg]] <= RANGE ? mk(SHOOT, u, tg) : moveToward(s, u, s.pos[tg]);
        return true;
    }
    if (s.own[tg] == player) { err = "can't convert own unit"; return false; }
    if (tg < 2) { err = "cult leaders cannot be converted"; return false; }
    if (u >= 2) { err = "only cult leaders can convert"; return false; }
    if (s.hp[u] <= 0 || s.hp[tg] <= 0) { err = "unit not in game"; return false; }
    m = mdist[s.pos[u]][s.pos[tg]] == 1 ? mk(CONVERT, u, tg) : moveToward(s, u, s.pos[tg]);
    return true;
}

// ---- bot processes -----------------------------------------------------------------------------
struct Bot {
    pid_t pid = -1;
    int in = -1, out = -1;
    string buf;
    bool start(const string& path, int ms, int firstMs, long long nodes) {
        int a[2], b[2];
        if (pipe(a) || pipe(b)) return false;
        pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            dup2(a[0], 0);
            dup2(b[1], 1);
            int dn = open("/dev/null", O_WRONLY);
            if (dn >= 0) dup2(dn, 2);
            for (int fd = 3; fd < 256; fd++) close(fd);
            setenv("CW_MS", to_string(ms - 6).c_str(), 1);            // same safety margins as the submission
            setenv("CW_FIRST_MS", to_string(firstMs - 150).c_str(), 1);
            if (nodes > 0) setenv("CW_NODES", to_string(nodes).c_str(), 1); else unsetenv("CW_NODES");
            execl(path.c_str(), path.c_str(), (char*)0);
            _exit(127);
        }
        close(a[0]);
        close(b[1]);
        in = a[1];
        out = b[0];
        return true;
    }
    bool send(const string& s) {
        size_t off = 0;
        while (off < s.size()) {
            ssize_t w = write(in, s.data() + off, s.size() - off);
            if (w <= 0) return false;
            off += (size_t)w;
        }
        return true;
    }
    int readLine(string& line, double timeoutMs) {   // 1 = line, 0 = timeout, -1 = closed
        auto end = chrono::steady_clock::now() + chrono::microseconds((long long)(timeoutMs * 1000));
        for (;;) {
            size_t p = buf.find('\n');
            if (p != string::npos) { line = buf.substr(0, p); buf.erase(0, p + 1); return 1; }
            auto now = chrono::steady_clock::now();
            if (now >= end) return 0;
            int left = (int)chrono::duration_cast<chrono::milliseconds>(end - now).count() + 1;
            pollfd pf{out, POLLIN, 0};
            int pr = poll(&pf, 1, left);
            if (pr == 0) return 0;
            if (pr < 0) continue;
            char tmp[512];
            ssize_t r = read(out, tmp, sizeof tmp);
            if (r <= 0) return -1;
            buf.append(tmp, (size_t)r);
        }
    }
    void stop() {
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, 0, 0); }
        in = out = -1; pid = -1;
    }
};

struct Result { int winner; int plies; string reason; double maxMs[2]; int units[2]; };

// seat0 = index (0/1) of the bot that plays first.
static Result playGame(const string paths[2], int seat0, const Map& mp, int ms, int firstMs,
                       const long long nodes[2], double grace) {
    initTables(mp.grid);
    State s = fromObs(mp.start, 42, 0, nullptr);
    Bot bots[2];
    Result res{-1, 0, "", {0, 0}, {0, 0}};
    for (int k = 0; k < 2; k++)
        if (!bots[k].start(paths[k], ms, firstMs, nodes[k])) {
            res.winner = 1 - k; res.reason = "spawn";
            bots[0].stop(); bots[1].stop();
            return res;
        }
    bool fault = false;
    while (!over(s)) {
        int player = s.turn & 1, who = player == 0 ? seat0 : 1 - seat0;
        string msg;
        bool first = s.turn < 2;
        if (first) {
            msg += to_string(player) + "\n" + to_string(W) + " " + to_string(H) + "\n";
            for (int y = 0; y < H; y++) msg += string(mp.grid[y]) + "\n";
        }
        int alive = 0;
        string units;
        for (int u = 0; u < NU; u++)
            if (s.hp[u] > 0) {
                alive++;
                units += to_string(u) + " " + (u < 2 ? "1 " : "0 ") + to_string(s.hp[u]) + " " + to_string(s.pos[u] % W) + " " +
                         to_string(s.pos[u] / W) + " " + to_string(s.own[u]) + "\n";
            }
        msg += to_string(alive) + "\n" + units;
        double limit = (first ? firstMs : ms) + grace;
        if (nodes[who] > 0) limit = 60000;
        auto t0 = chrono::steady_clock::now();
        string line, err;
        int rc = bots[who].send(msg) ? bots[who].readLine(line, limit) : -1;
        double el = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
        res.maxMs[who] = max(res.maxMs[who], el);
        Move m;
        if (rc != 1) { res.winner = 1 - who; res.reason = rc == 0 ? "timeout" : "crash"; fault = true; break; }
        if (!resolveAction(s, player, line, m, err)) { res.winner = 1 - who; res.reason = "invalid:" + line + " (" + err + ")"; fault = true; break; }
        apply(s, m);
    }
    if (!fault) {
        int d = s.cnt[0] - s.cnt[1];
        res.winner = d == 0 ? -1 : (d > 0 ? seat0 : 1 - seat0);
        res.reason = s.turn >= MAXTURN ? "end" : "wipeout";
    }
    res.units[seat0] = s.cnt[0]; res.units[1 - seat0] = s.cnt[1];
    res.plies = s.turn;
    bots[0].stop();
    bots[1].stop();
    return res;
}

// ---- replay check ----------------------------------------------------------------------------
// FILE lines:  M row0 .. row6 | S turn seed id,hp,x,y,owner ... | A turn player <output line>
static int checkFile(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); return 2; }
    char buf[4096];
    State s;
    bool have = false;
    int states = 0, actions = 0;
    while (fgets(buf, sizeof buf, f)) {
        string l(buf);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.size() < 2) continue;
        if (l[0] == 'M') {
            char grid[H][W + 2];
            const char* p = l.c_str() + 2;
            for (int y = 0; y < H; y++) { memcpy(grid[y], p + y * (W + 1), W); grid[y][W] = 0; }
            initTables(grid);
            have = false;
        } else if (l[0] == 'S') {
            int turn, pos = 0;
            unsigned seed;
            sscanf(l.c_str() + 2, "%d %u%n", &turn, &seed, &pos);
            Obs o;
            memset(&o, 0, sizeof o);
            const char* p = l.c_str() + 2 + pos;
            int id, hp, x, y, ow, n;
            while (sscanf(p, " %d,%d,%d,%d,%d%n", &id, &hp, &x, &y, &ow, &n) == 5) {
                o.hp[id] = (int8_t)hp; o.pos[id] = (int8_t)(y * W + x); o.own[id] = (int8_t)ow;
                p += n;
            }
            if (have) {
                if (s.turn != turn || !matches(s, o) || s.seed != seed) {
                    printf("MISMATCH at turn %d (sim turn %d, sim seed %u, bot seed %u)\n", turn, s.turn, s.seed, seed);
                    for (int u = 0; u < NU; u++)
                        if (s.hp[u] != o.hp[u] || (s.hp[u] > 0 && (s.pos[u] != o.pos[u] || s.own[u] != o.own[u])))
                            printf("  unit %d: sim hp %d (%d,%d) owner %d | real hp %d (%d,%d) owner %d\n", u, s.hp[u], s.pos[u] % W,
                                   s.pos[u] / W, s.own[u], o.hp[u], o.pos[u] % W, o.pos[u] / W, o.own[u]);
                    return 1;
                }
                states++;
            } else { s = fromObs(o, seed, turn, nullptr); have = true; }
        } else if (l[0] == 'A' && have) {
            int turn, player, pos = 0;
            sscanf(l.c_str() + 2, "%d %d %n", &turn, &player, &pos);
            if (turn != s.turn) { printf("turn gap: action for %d, sim at %d\n", turn, s.turn); return 1; }
            Move m;
            string err;
            if (!resolveAction(s, player, l.substr(2 + pos), m, err)) { printf("turn %d: invalid action '%s' (%s)\n", turn, l.c_str(), err.c_str()); return 1; }
            apply(s, m);
            actions++;
        }
    }
    fclose(f);
    printf("OK %d states matched, %d actions, final units %d-%d\n", states, actions, s.cnt[0], s.cnt[1]);
    return 0;
}

// ---- export of recorded games as training records --------------------------------------------
// FILE lines per game:  M rows | S 0 42 units | A turn player <output> ... | R result_for_player_0
// Writes one Rec per position reached (search score 0, flag bit 1 = no search score).
// With CW_LABEL=nodes every position is searched instead and gets the search score, as in
// self-play data (flag bit 0 = the best move shoots or converts).
static int exportFile(const char* path, const char* out) {
    FILE* f = fopen(path, "r");
    FILE* o = fopen(out, "wb");
    if (!f || !o) { perror(path); return 2; }
    static char buf[1 << 16];
    State s;
    bool have = false;
    vector<Rec> recs;
    long long games = 0, total = 0, broken = 0;
    long long label = getenv("CW_LABEL") ? atoll(getenv("CW_LABEL")) : 0;
    if (label) { initNet(); initSearch(); nodeLimit = label; }
    // CW_STARTS=file: also write the position every CW_STARTS_EVERY plies (turns 10-110) as an
    // M/S pair, dead units included with their owner, for CW_GEN to play out from (bot.cpp).
    FILE* starts = getenv("CW_STARTS") ? fopen(getenv("CW_STARTS"), "w") : nullptr;
    int every = getenv("CW_STARTS_EVERY") ? atoi(getenv("CW_STARTS_EVERY")) : 10;
    string mline;
    auto writeStart = [&]() {
        if (!starts || s.turn < 10 || s.turn > 110 || s.turn % every) return;
        fprintf(starts, "%s\nS %d %u", mline.c_str(), s.turn, s.seed);
        for (int u = 0; u < NU; u++)
            fprintf(starts, " %d,%d,%d,%d,%d", u, s.hp[u] > 0 ? s.hp[u] : 0, s.pos[u] % W, s.pos[u] / W, s.own[u]);
        fprintf(starts, "\n");
    };
    auto record = [&]() {
        writeStart();
        Rec r;
        memset(&r, 0, sizeof r);
        for (int c = 0; c < NC; c++) if (wallAt[c]) r.walls[c >> 3] |= (uint8_t)(1 << (c & 7));
        for (int u = 0; u < NU; u++) r.unit[u] = (uint16_t)((s.hp[u] > 0 ? s.pos[u] : 0) | s.hp[u] << 7 | s.own[u] << 11);
        r.turn = (uint8_t)s.turn;
        r.flags = 2;
        if (label) {
            Move b = think(s, 1e9);
            r.flags = mType(b) >= SHOOT;
            r.score = (int16_t)max(-32000, min(32000, info.score));
        }
        recs.push_back(r);
    };
    while (fgets(buf, sizeof buf, f)) {
        string l(buf);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.size() < 2) continue;
        if (l[0] == 'M') {
            char grid[H][W + 2];
            const char* p = l.c_str() + 2;
            for (int y = 0; y < H; y++) { memcpy(grid[y], p + y * (W + 1), W); grid[y][W] = 0; }
            initTables(grid);
            mline = l;
            if (label) { memset(tt, 0, sizeof(TTE) << TT_BITS); memset(hist, 0, sizeof hist); }
            have = false; recs.clear();
        } else if (l[0] == 'S') {
            int turn, pos = 0;
            unsigned seed;
            sscanf(l.c_str() + 2, "%d %u%n", &turn, &seed, &pos);
            Obs ob;
            memset(&ob, 0, sizeof ob);
            const char* p = l.c_str() + 2 + pos;
            int id, hp, x, y, ow, n;
            while (sscanf(p, " %d,%d,%d,%d,%d%n", &id, &hp, &x, &y, &ow, &n) == 5) {
                ob.hp[id] = (int8_t)hp; ob.pos[id] = (int8_t)(y * W + x); ob.own[id] = (int8_t)ow;
                p += n;
            }
            s = fromObs(ob, seed, turn, nullptr);
            if (turn) { for (int u = 0; u < NU; u++) s.own[u] = ob.own[u]; rehash(s); }   // mid-game start: dead units keep their owner
            have = true;
        } else if (l[0] == 'A' && have) {
            int turn, player, pos = 0;
            sscanf(l.c_str() + 2, "%d %d %n", &turn, &player, &pos);
            Move m;
            string err;
            if (turn != s.turn || over(s) || !resolveAction(s, player, l.substr(2 + pos), m, err)) { have = false; broken++; continue; }
            record();
            apply(s, m);
        } else if (l[0] == 'R') {
            int d = atoi(l.c_str() + 2);
            for (Rec& r : recs) {
                int sd = (r.turn & 1) ? -d : d;
                r.result = (int8_t)(sd > 0) - (int8_t)(sd < 0);
                r.margin = (int8_t)sd;
            }
            fwrite(recs.data(), sizeof(Rec), recs.size(), o);
            total += (long long)recs.size(); games++;
            recs.clear(); have = false;
        }
    }
    fclose(f); fclose(o);
    if (starts) fclose(starts);
    printf("%lld games, %lld positions -> %s (%lld games cut short by an unreadable action)\n", games, total, out, broken);
    return 0;
}

// ---- analysis of recorded games --------------------------------------------------------------
// Same FILE format as --export, plus `P seat game_id` before each game: our seat. Searches every
// position where we were to move (CW_NODES nodes) and prints: game turn score units0 units1.
static int analyzeFile(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); return 2; }
    static char buf[1 << 16];
    initNet();
    initSearch();
    nodeLimit = getenv("CW_NODES") ? atoll(getenv("CW_NODES")) : 100000;
    State s;
    bool have = false;
    int seat = 0;
    long long game = 0;
    while (fgets(buf, sizeof buf, f)) {
        string l(buf);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.size() < 2) continue;
        if (l[0] == 'P') sscanf(l.c_str() + 2, "%d %lld", &seat, &game);
        else if (l[0] == 'M') {
            char grid[H][W + 2];
            const char* p = l.c_str() + 2;
            for (int y = 0; y < H; y++) { memcpy(grid[y], p + y * (W + 1), W); grid[y][W] = 0; }
            initTables(grid);
            memset(tt, 0, sizeof(TTE) << TT_BITS);
            have = false;
        } else if (l[0] == 'S') {
            int turn, pos = 0;
            unsigned seed;
            sscanf(l.c_str() + 2, "%d %u%n", &turn, &seed, &pos);
            Obs ob;
            memset(&ob, 0, sizeof ob);
            const char* p = l.c_str() + 2 + pos;
            int id, hp, x, y, ow, n;
            while (sscanf(p, " %d,%d,%d,%d,%d%n", &id, &hp, &x, &y, &ow, &n) == 5) {
                ob.hp[id] = (int8_t)hp; ob.pos[id] = (int8_t)(y * W + x); ob.own[id] = (int8_t)ow;
                p += n;
            }
            s = fromObs(ob, seed, turn, nullptr);
            have = true;
        } else if (l[0] == 'A' && have) {
            int turn, player, pos = 0;
            sscanf(l.c_str() + 2, "%d %d %n", &turn, &player, &pos);
            Move m;
            string err;
            if (turn != s.turn || over(s) || !resolveAction(s, player, l.substr(2 + pos), m, err)) { have = false; continue; }
            if (player == seat) {
                Move best = think(s, 1e9);
                printf("%lld %d %d %d %d %s | %s\n", game, s.turn, info.score, s.cnt[seat], s.cnt[seat ^ 1], moveStr(s, m).c_str(), moveStr(s, best).c_str());
            }
            apply(s, m);
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc >= 3 && string(argv[1]) == "--analyze") return analyzeFile(argv[2]);
    if (argc >= 3 && string(argv[1]) == "--check") return checkFile(argv[2]);
    if (argc >= 4 && string(argv[1]) == "--export") return exportFile(argv[2], argv[3]);
    if (argc < 3) {
        fprintf(stderr, "usage: referee A B [-g games] [-j procs] [-t ms] [-f first_ms] [-n nodes] [-nb nodes_b] [-s seed] [-x grace_ms] [-v]\n");
        return 2;
    }
    string paths[2] = {argv[1], argv[2]};
    int games = 100, procs = 4, ms = 50, firstMs = 1000;
    long long nodes[2] = {0, -1};
    unsigned seed = (unsigned)chrono::system_clock::now().time_since_epoch().count();
    double grace = 15;
    bool verbose = false;
    for (int i = 3; i < argc; i++) {
        string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? argv[++i] : (char*)"0"; };
        if (a == "-g") games = atoi(next());
        else if (a == "-j") procs = atoi(next());
        else if (a == "-t") ms = atoi(next());
        else if (a == "-f") firstMs = atoi(next());
        else if (a == "-s") seed = (unsigned)strtoul(next(), 0, 10);
        else if (a == "-n") nodes[0] = atoll(next());
        else if (a == "-nb") nodes[1] = atoll(next());
        else if (a == "-x") grace = atof(next());
        else if (a == "-v") verbose = true;
    }
    if (nodes[1] < 0) nodes[1] = nodes[0];
    mt19937 rng(seed);
    vector<Map> maps((games + 1) / 2);
    for (auto& m : maps) m = makeMap(rng);

    // One worker process per slot (the simulator's tables are global, one map per process).
    int fd[2];
    if (pipe(fd)) return 2;
    procs = max(1, min(procs, games));
    for (int w = 0; w < procs; w++) {
        pid_t pid = fork();
        if (pid == 0) {
            close(fd[0]);
            for (int g = w; g < games; g += procs) {
                int seat0 = g % 2;
                Result r = playGame(paths, seat0, maps[g / 2], ms, firstMs, nodes, grace);
                char line[512];
                int n = snprintf(line, sizeof line, "%d %d %d %d %.1f %.1f %d %d %d %s\n", g, seat0, r.winner, r.plies, r.maxMs[0], r.maxMs[1],
                                 r.units[0], r.units[1], (r.reason == "end" || r.reason == "wipeout") ? 0 : 1, r.reason.c_str());
                if (write(fd[1], line, (size_t)n) < 0) _exit(1);
            }
            _exit(0);
        }
    }
    close(fd[1]);
    FILE* in = fdopen(fd[0], "r");
    int wins[2] = {0, 0}, draws = 0, firstWins = 0, faults[2] = {0, 0}, done = 0;
    double maxMs[2] = {0, 0};
    long long totalPlies = 0;
    char line[512];
    while (fgets(line, sizeof line, in)) {
        int g, seat0, winner, plies, ua, ub, fault, off = 0;
        double ma, mb;
        if (sscanf(line, "%d %d %d %d %lf %lf %d %d %d %n", &g, &seat0, &winner, &plies, &ma, &mb, &ua, &ub, &fault, &off) < 9) continue;
        string reason(line + off);
        if (!reason.empty() && reason.back() == '\n') reason.pop_back();
        done++;
        totalPlies += plies;
        maxMs[0] = max(maxMs[0], ma); maxMs[1] = max(maxMs[1], mb);
        if (winner < 0) draws++;
        else {
            wins[winner]++;
            if (winner == seat0) firstWins++;
            if (fault) faults[1 - winner]++;
        }
        if (verbose) {
            printf("{\"game\":%d,\"first\":\"%s\",\"winner\":\"%s\",\"plies\":%d,\"units_a\":%d,\"units_b\":%d,\"reason\":\"%s\"}\n", g,
                   seat0 == 0 ? "A" : "B", winner == 0 ? "A" : (winner == 1 ? "B" : "-"), plies, ua, ub, reason.c_str());
            fflush(stdout);
        }
    }
    while (wait(0) > 0) {}
    printf("{\"summary\":true,\"games\":%d,\"wins_a\":%d,\"wins_b\":%d,\"draws\":%d,\"first_wins\":%d,\"faults_a\":%d,\"faults_b\":%d,"
           "\"avg_plies\":%.1f,\"max_ms_a\":%.1f,\"max_ms_b\":%.1f,\"ms\":%d,\"first_ms\":%d,\"nodes\":%lld,\"nodes_b\":%lld,\"seed\":%u}\n",
           done, wins[0], wins[1], draws, firstWins, faults[0], faults[1], done ? (double)totalPlies / done : 0.0, maxMs[0], maxMs[1],
           ms, firstMs, nodes[0], nodes[1], seed);
    return 0;
}
