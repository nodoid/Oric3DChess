/*
 * perft_main.c - verification harness for the chess engine, built for
 * cc65's sim65 simulator.
 *
 * Usage: engine_test <suite>
 *   fen     - FEN + legal move counts (fast sanity)
 *   perft   - perft known positions (slow)
 *   rules   - mate/stalemate/castling detection
 *   search  - AI finds mate / free material
 *   play    - short AI self-play smoke test
 *   all     - everything (default)
 */
#include <stdio.h>
#include <string.h>
#include "engine.h"

static int fails = 0;

/* printf + flush (sim65 stdout may be buffered; we must see progress
   even when the simulator is killed by its cycle cap) */
#define P(...)                      \
    do {                            \
        printf(__VA_ARGS__);        \
        fflush(stdout);             \
    } while (0)

static void expect_perft(const char *name, const char *fen,
                         unsigned char depth, unsigned long expected)
{
    Position p;
    unsigned long n;

    if (!parse_fen(&p, fen)) {
        P("%s: FEN PARSE FAIL\n", name);
        ++fails;
        return;
    }
    g_mtop = 0;
    n = perft(&p, depth);
    if (n == expected)
        P("%s d%u OK (%lu)\n", name, depth, n);
    else {
        P("%s d%u FAIL got %lu want %lu\n", name, depth, n, expected);
        ++fails;
    }
}

static void expect_check(const char *name, const char *fen, int want_check,
                         int want_legal)
{
    Position p;
    int chk, cnt;

    if (!parse_fen(&p, fen)) {
        printf("%s: FEN PARSE FAIL\n", name);
        ++fails;
        return;
    }
    chk = in_check(&p, p.side);
    cnt = legal_count(&p);
    if (chk == want_check && (want_legal < 0 || cnt == want_legal)) {
        P("%s OK (check=%d legal=%d)\n", name, chk, cnt);
    } else {
        P("%s FAIL check=%d(want %d) legal=%d(want %d)\n", name, chk,
          want_check, cnt, want_legal);
        ++fails;
    }
}

static void expect_search(const char *name, const char *fen,
                          unsigned char depth, int min_score)
{
    Position p;
    Move best;
    int score;

    if (!parse_fen(&p, fen)) {
        printf("%s: FEN PARSE FAIL\n", name);
        ++fails;
        return;
    }
    g_mtop = 0;
    score = search(&p, depth, &best);
    if (score >= min_score && best.to != 0) {
        char buf[8];
        move_to_str(&best, buf);
        P("%s OK score=%d move=%s\n", name, score, buf);
    } else {
        P("%s FAIL score=%d (want >= %d) move=%d->%d\n", name, score,
          min_score, best.from, best.to);
        ++fails;
    }
}

static void self_play(unsigned char plies)
{
    Position p;
    Move best;
    unsigned char i, lm;

    set_startpos(&p);
    engine_seed(7);
    for (i = 0; i < plies; ++i) {
        lm = legal_count(&p);
        if (lm == 0) {
            P("selfplay: game over at ply %u (check=%d)\n", i,
              in_check(&p, p.side));
            return;
        }
        g_mtop = 0;
        search(&p, (i & 1) ? 1 : 2, &best);
        if (best.to == 0) {
            P("selfplay: NO MOVE at ply %u\n", i);
            ++fails;
            return;
        }
        {
            Undo u;
            char buf[8];
            move_to_str(&best, buf);
            P("selfplay ply %u depth %u move %s\n", i,
              (unsigned)((i & 1) ? 1 : 2), buf);
            make_move(&p, &best, &u);
        }
    }
    P("selfplay %u plies OK\n", plies);
}

static const char *kiwipete =
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
static const char *pos3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
static const char *pos4 =
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
static const char *pos5 =
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";

static void suite_fen(void)
{
    Position p;

    P("-- fen --\n");
    if (!parse_fen(&p, "")) {
        P("start FEN FAIL\n");
        ++fails;
        return;
    }
    P("start legal=%u side=%u castle=%u\n", legal_count(&p), p.side,
      p.castle);
    if (legal_count(&p) != 20) {
        ++fails;
        P("start legal FAIL\n");
    }
    if (!parse_fen(&p, kiwipete)) {
        P("kiwipete FEN FAIL\n");
        ++fails;
        return;
    }
    P("kiwipete legal=%u (want 48)\n", legal_count(&p));
    if (legal_count(&p) != 48)
        ++fails;
}

static void suite_perft(void)
{
    P("-- perft --\n");
    expect_perft("start", "", 1, 20);
    expect_perft("start", "", 2, 400);
    expect_perft("start", "", 3, 8902);
    expect_perft("kiwipete", kiwipete, 1, 48);
    expect_perft("kiwipete", kiwipete, 2, 2039);
    expect_perft("pos3", pos3, 1, 14);
    expect_perft("pos3", pos3, 2, 191);
    expect_perft("pos3", pos3, 3, 2812);
    expect_perft("pos4", pos4, 1, 6);
    expect_perft("pos4", pos4, 2, 264);
    expect_perft("pos5", pos5, 1, 44);
    expect_perft("pos5", pos5, 2, 1486);
    expect_perft("castle", "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1",
                 1, 25);
}

static void suite_perft_deep(void)
{
    P("-- perft deep --\n");
    expect_perft("start", "", 4, 197281);
    expect_perft("kiwipete", kiwipete, 3, 97862);
    expect_perft("pos3", pos3, 4, 43238);
    expect_perft("pos4", pos4, 3, 9467);
    expect_perft("pos5", pos5, 3, 62379);
}

static void suite_rules(void)
{
    P("-- rules --\n");
    expect_check("foolsmate",
                 "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - "
                 "1 3",
                 1, 0);
    expect_check("stalemate", "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 0, 0);
    expect_check("opening", "", 0, 20);
}

static void suite_search(void)
{
    P("-- search --\n");
    expect_search("mate1", "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", 2, 29900);
    expect_search("grab", "rnb1kbnr/ppp1pppp/8/3q4/4P3/8/PPPP1PPP/RNBQKBNR w "
                          "KQkq - 0 1",
                  2, 500);
}

static void suite_play(void)
{
    P("-- play --\n");
    self_play(8);
}

static void divide(const char *tag, const char *fen, unsigned char child_depth)
{
    Position p;
    unsigned char n, i, mover;
    unsigned long total = 0;

    if (!parse_fen(&p, fen)) {
        P("%s: FEN PARSE FAIL\n", tag);
        ++fails;
        return;
    }
    mover = p.side;
    g_mtop = 0;
    n = gen_moves(&p, &g_mstack[0], 0, MAX_TOTMOVES);
    g_mtop = n; /* reserve: perft must not clobber the root list */
    P("== divide %s (children d%u)\n", tag, child_depth);
    for (i = 0; i < n; ++i) {
        Move mv;
        Undo u;
        unsigned char k;
        unsigned long cnt = 0;

        mv = g_mstack[i];
        make_move(&p, &mv, &u);
        k = find_king(&p, mover);
        if (is_attacked(&p, k, p.side)) {
            unmake_move(&p, &mv, &u);
            continue;
        }
        cnt = perft(&p, child_depth);
        total += cnt;
        {
            char b[8];
            move_to_str(&mv, b);
            P("  %s %lu%s\n", b, cnt, (mv.flags & MF_CASTLE) ? " C" : "");
        }
        unmake_move(&p, &mv, &u);
    }
    g_mtop = 0;
    P("  TOTAL %lu\n", total);
}

static void suite_divide(void)
{
    P("-- divide --\n");
    divide("kiwi", kiwipete, 1);
    divide("pos4", pos4, 1);
    divide("pos5", pos5, 2);
}

int main(int argc, char **argv)
{
    const char *which = (argc > 1) ? argv[1] : "all";

    P("engine tests: %s\n", which);
    if (!strcmp(which, "all") || !strcmp(which, "fen"))
        suite_fen();
    if (!strcmp(which, "all") || !strcmp(which, "rules"))
        suite_rules();
    if (!strcmp(which, "all") || !strcmp(which, "search"))
        suite_search();
    if (!strcmp(which, "all") || !strcmp(which, "play"))
        suite_play();
    if (!strcmp(which, "divide"))
        suite_divide();
    if (!strcmp(which, "all") || !strcmp(which, "perft")) {
        suite_perft();
        suite_perft_deep();
    }

    if (fails == 0)
        P("ALL TESTS PASSED\n");
    else
        P("%d TEST(S) FAILED\n", fails);
    return fails;
}
