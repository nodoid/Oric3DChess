/*
 * engine.c - 0x88 mailbox chess engine, alpha-beta AI.
 *
 * Platform independent: only cc65-portable C, no I/O, no allocation.
 * Recursion uses a shared global move stack (g_mstack) so the C stack
 * usage stays tiny (cc65 default stack is only 2K).
 */
#include "engine.h"
#include <stddef.h>

#ifdef ENGINE_DEBUG
#include <stdio.h>
static unsigned long dbg_count;
#define DBG(...)                                                        \
    do {                                                                \
        printf(__VA_ARGS__);                                            \
        fflush(stdout);                                                 \
        ++dbg_count;                                                    \
    } while (0)
#else
#define DBG(...) do { } while (0)
#endif

/* ---------------------------------------------------------------------- */
/* global move stack                                                      */
/* ---------------------------------------------------------------------- */
Move g_mstack[MAX_TOTMOVES];
unsigned int g_mtop;

/* ---------------------------------------------------------------------- */
/* small deterministic rng (for choosing between equal-scoring moves)     */
/* ---------------------------------------------------------------------- */
static unsigned int rng_state = 12345;

void engine_seed(unsigned int s)
{
    if (s)
        rng_state = s;
}

/* ---------------------------------------------------------------------- */
/* position setup                                                         */
/* ---------------------------------------------------------------------- */
static const char start_fen[] =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

void set_startpos(Position *p)
{
    parse_fen(p, start_fen);
    g_mtop = 0;
}

static unsigned char fen_char_to_piece(unsigned char c)
{
    static const char order[] = " PNBRQK";
    unsigned char i;

    for (i = 1; i < 7; ++i)
        if (c == (unsigned char)order[i])
            return i;
    for (i = 1; i < 7; ++i)
        if (c == (unsigned char)(order[i] | 0x20))
            return i | BLACK;
    return 0xFF;
}

unsigned char parse_fen(Position *p, const char *fen)
{
    unsigned char s, rk, fr, c, pc;
    const char *f = fen;

    if (!f || !*f)
        f = start_fen;

    for (s = 0; s < 128; ++s)
        p->board[s] = 0;

    rk = 7;
    fr = 0;
    for (;;) {
        c = (unsigned char)*f;
        if (c == 0 || c == ' ')
            break;
        ++f;
        if (c == '/') {
            if (rk == 0 || fr != 8)
                return 0;
            --rk;
            fr = 0;
        } else if (c >= '1' && c <= '8') {
            fr = (unsigned char)(fr + (c - '0'));
            if (fr > 8)
                return 0;
        } else {
            pc = fen_char_to_piece(c);
            if (pc == 0xFF || fr > 7 || rk > 7)
                return 0;
            p->board[SQ(fr, rk)] = pc;
            ++fr;
        }
    }
    /* side to move */
    while (*f == ' ')
        ++f;
    p->side = (*f == 'b') ? BLACK_SIDE : WHITE_SIDE;
    ++f;
    while (*f == ' ')
        ++f;
    /* castling */
    p->castle = 0;
    while (*f && *f != ' ') {
        switch (*f) {
        case 'K': p->castle |= 1; break;
        case 'Q': p->castle |= 2; break;
        case 'k': p->castle |= 4; break;
        case 'q': p->castle |= 8; break;
        }
        ++f;
    }
    while (*f == ' ')
        ++f;
    /* en passant target */
    p->ep = -1;
    if (*f && *f != '-') {
        unsigned char efile = (unsigned char)(*f - 'a');
        unsigned char erank = (unsigned char)(f[1] - '1');
        if (efile < 8 && erank < 8)
            p->ep = (signed char)SQ(efile, erank);
    }
    while (*f && *f != ' ')
        ++f;
    while (*f == ' ')
        ++f;
    /* halfmove clock (optional) */
    p->halfmove = 0;
    if (*f >= '0' && *f <= '9') {
        unsigned int h = 0;
        while (*f >= '0' && *f <= '9') {
            h = h * 10 + (unsigned int)(*f - '0');
            ++f;
            if (h > 200)
                h = 200;
        }
        p->halfmove = (unsigned char)h;
    }
    return 1;
}

/* ---------------------------------------------------------------------- */
/* attacks                                                                */
/* ---------------------------------------------------------------------- */
unsigned char is_attacked(const Position *p, unsigned char sq, unsigned char side)
{
    static const signed char kn[8] = { 0x21, 0x1f, 0x12, 0x0e,
                                       -0x0e, -0x12, -0x1f, -0x21 };
    static const signed char kd[8] = { 1, -1, 16, -16, 15, -15, 17, -17 };
    static const signed char rook_d[4] = { 1, -1, 16, -16 };
    static const signed char bish_d[4] = { 15, -15, 17, -17 };
    unsigned char sidebit = (side == BLACK_SIDE) ? BLACK : 0;
    unsigned char target;
    int t;
    unsigned char i;

    /* pawns: white pawn on s attacks s+15 and s+17 => attacker sits at
       sq-15 / sq-17 when sq is attacked by white */
    if (side == WHITE_SIDE) {
        t = (int)sq - 15;
        if (SQ_VALID(t) && p->board[t] == (PAWN | 0))
            return 1;
        t = (int)sq - 17;
        if (SQ_VALID(t) && p->board[t] == PAWN)
            return 1;
    } else {
        t = (int)sq + 15;
        if (SQ_VALID(t) && p->board[t] == (PAWN | BLACK))
            return 1;
        t = (int)sq + 17;
        if (SQ_VALID(t) && p->board[t] == (PAWN | BLACK))
            return 1;
    }

    /* knights */
    target = KNIGHT | sidebit;
    for (i = 0; i < 8; ++i) {
        t = (int)sq + kn[i];
        if (SQ_VALID(t) && p->board[t] == target)
            return 1;
    }

    /* king */
    target = KING | sidebit;
    for (i = 0; i < 8; ++i) {
        t = (int)sq + kd[i];
        if (SQ_VALID(t) && p->board[t] == target)
            return 1;
    }

    /* sliders */
    target = ROOK | sidebit;
    for (i = 0; i < 4; ++i) {
        t = (int)sq;
        for (;;) {
            t += rook_d[i];
            if (!SQ_VALID(t))
                break;
            if (p->board[t]) {
                if (p->board[t] == target || p->board[t] == (QUEEN | sidebit))
                    return 1;
                break;
            }
        }
    }
    target = BISHOP | sidebit;
    for (i = 0; i < 4; ++i) {
        t = (int)sq;
        for (;;) {
            t += bish_d[i];
            if (!SQ_VALID(t))
                break;
            if (p->board[t]) {
                if (p->board[t] == target || p->board[t] == (QUEEN | sidebit))
                    return 1;
                break;
            }
        }
    }
    return 0;
}

unsigned char find_king(const Position *p, unsigned char side)
{
    unsigned char want = (unsigned char)(KING | (side == BLACK_SIDE ? BLACK : 0));
    unsigned char fr, rk, s;

    for (rk = 0; rk < 8; ++rk)
        for (fr = 0; fr < 8; ++fr) {
            s = SQ(fr, rk);
            if (p->board[s] == want)
                return s;
        }
    return 0;
}

unsigned char in_check(const Position *p, unsigned char side)
{
    unsigned char k = find_king(p, side);
    return is_attacked(p, k, side == WHITE_SIDE ? BLACK_SIDE : WHITE_SIDE);
}

/* ---------------------------------------------------------------------- */
/* move generation                                                        */
/* ---------------------------------------------------------------------- */
#define ADD(f, t, fl, pr)                                                    \
    do {                                                                     \
        if (n < room) {                                                      \
            out[n].from = (unsigned char)(f);                                \
            out[n].to = (unsigned char)(t);                                  \
            out[n].flags = (unsigned char)(fl);                              \
            out[n].promo = (unsigned char)(pr);                              \
            ++n;                                                             \
        }                                                                    \
    } while (0)

unsigned char gen_moves(Position *p, Move *out, unsigned char captures_only,
                        unsigned int room)
{
    unsigned char n = 0;
    unsigned char fr, rk, s, pc, sidebit, t, cap;
    unsigned char side = p->side;
    unsigned char i;
    int d, tt;
    static const signed char kn[8] = { 0x21, 0x1f, 0x12, 0x0e,
                                       -0x0e, -0x12, -0x1f, -0x21 };
    static const signed char kd[8] = { 1, -1, 16, -16, 15, -15, 17, -17 };
    static const signed char rook_d[4] = { 1, -1, 16, -16 };
    static const signed char bish_d[4] = { 15, -15, 17, -17 };
    static const signed char pawn_dir[2] = { 16, -16 };
    unsigned char start_rank = (side == WHITE_SIDE) ? 1 : 6;
    unsigned char promo_rank = (side == WHITE_SIDE) ? 7 : 0;

    (void)pawn_dir;

    for (rk = 0; rk < 8; ++rk)
        for (fr = 0; fr < 8; ++fr) {
            s = SQ(fr, rk);
            pc = p->board[s];
            if (!pc || (pc & BLACK) != (side == BLACK_SIDE ? BLACK : 0))
                continue;
            sidebit = pc & BLACK;

            switch (pc & 7) {
            case PAWN: {
                signed char pd = (side == WHITE_SIDE) ? 16 : -16;
                unsigned char one = (unsigned char)((int)s + pd);

                if (SQ_VALID(one) && p->board[one] == 0) {
                    if (RANK_OF(one) == promo_rank) {
                        ADD(s, one, MF_PROMO, QUEEN);
                        ADD(s, one, MF_PROMO, ROOK);
                        ADD(s, one, MF_PROMO, BISHOP);
                        ADD(s, one, MF_PROMO, KNIGHT);
                    } else if (!captures_only) {
                        ADD(s, one, 0, 0);
                        if (RANK_OF(s) == start_rank) {
                            unsigned char two =
                                (unsigned char)((int)one + pd);
                            if (p->board[two] == 0)
                                ADD(s, two, MF_DBL, 0);
                        }
                    }
                }
                /* captures */
                for (i = 0; i < 2; ++i) {
                    signed char dc = (pd == 16)
                                         ? (i == 0 ? 15 : 17)
                                         : (i == 0 ? -17 : -15);
                    tt = (int)s + dc;
                    if (!SQ_VALID(tt))
                        continue;
                    t = (unsigned char)tt;
                    cap = p->board[t];
                    if (cap && (cap & BLACK) != sidebit) {
                        if (RANK_OF(t) == promo_rank) {
                            ADD(s, t, MF_CAP | MF_PROMO, QUEEN);
                            ADD(s, t, MF_CAP | MF_PROMO, ROOK);
                            ADD(s, t, MF_CAP | MF_PROMO, BISHOP);
                            ADD(s, t, MF_CAP | MF_PROMO, KNIGHT);
                        } else {
                            ADD(s, t, MF_CAP, 0);
                        }
                    } else if (!cap && p->ep == (signed char)t &&
                               p->ep != -1) {
                        ADD(s, t, MF_CAP | MF_EP, 0);
                    }
                }
                break;
            }

            case KNIGHT:
                for (i = 0; i < 8; ++i) {
                    tt = (int)s + kn[i];
                    if (!SQ_VALID(tt))
                        continue;
                    t = (unsigned char)tt;
                    cap = p->board[t];
                    if (!cap) {
                        if (!captures_only)
                            ADD(s, t, 0, 0);
                    } else if ((cap & BLACK) != sidebit) {
                        ADD(s, t, MF_CAP, 0);
                    }
                }
                break;

            case KING:
                for (i = 0; i < 8; ++i) {
                    tt = (int)s + kd[i];
                    if (!SQ_VALID(tt))
                        continue;
                    t = (unsigned char)tt;
                    cap = p->board[t];
                    if (!cap) {
                        if (!captures_only)
                            ADD(s, t, 0, 0);
                    } else if ((cap & BLACK) != sidebit) {
                        ADD(s, t, MF_CAP, 0);
                    }
                }
                /* castling */
                if (!captures_only) {
                    if (side == WHITE_SIDE) {
                        if (s == 0x04 && (p->castle & 1) && !p->board[0x05] &&
                            !p->board[0x06] && p->board[0x07] == (ROOK) &&
                            !is_attacked(p, 0x04, BLACK_SIDE) &&
                            !is_attacked(p, 0x05, BLACK_SIDE) &&
                            !is_attacked(p, 0x06, BLACK_SIDE))
                            ADD(0x04, 0x06, MF_CASTLE, 0);
                        if (s == 0x04 && (p->castle & 2) && !p->board[0x01] &&
                            !p->board[0x02] && !p->board[0x03] &&
                            p->board[0x00] == (ROOK) &&
                            !is_attacked(p, 0x04, BLACK_SIDE) &&
                            !is_attacked(p, 0x03, BLACK_SIDE) &&
                            !is_attacked(p, 0x02, BLACK_SIDE))
                            ADD(0x04, 0x02, MF_CASTLE, 0);
                    } else {
                        if (s == 0x74 && (p->castle & 4) && !p->board[0x75] &&
                            !p->board[0x76] && p->board[0x77] == (ROOK | BLACK) &&
                            !is_attacked(p, 0x74, WHITE_SIDE) &&
                            !is_attacked(p, 0x75, WHITE_SIDE) &&
                            !is_attacked(p, 0x76, WHITE_SIDE))
                            ADD(0x74, 0x76, MF_CASTLE, 0);
                        if (s == 0x74 && (p->castle & 8) && !p->board[0x71] &&
                            !p->board[0x72] && !p->board[0x73] &&
                            p->board[0x70] == (ROOK | BLACK) &&
                            !is_attacked(p, 0x74, WHITE_SIDE) &&
                            !is_attacked(p, 0x73, WHITE_SIDE) &&
                            !is_attacked(p, 0x72, WHITE_SIDE))
                            ADD(0x74, 0x72, MF_CASTLE, 0);
                    }
                }
                break;

            case ROOK:
            case BISHOP:
            case QUEEN: {
                unsigned char nd = 0;
                unsigned char di;

                if (pc == (ROOK | sidebit) || pc == (QUEEN | sidebit)) {
                    for (di = 0; di < 4; ++di) {
                        d = rook_d[di];
                        tt = (int)s;
                        for (;;) {
                            tt += d;
                            if (!SQ_VALID(tt))
                                break;
                            t = (unsigned char)tt;
                            cap = p->board[t];
                            if (!cap) {
                                if (!captures_only)
                                    ADD(s, t, 0, 0);
                            } else {
                                if ((cap & BLACK) != sidebit)
                                    ADD(s, t, MF_CAP, 0);
                                break;
                            }
                        }
                    }
                    nd = 1;
                }
                if (pc == (BISHOP | sidebit) || pc == (QUEEN | sidebit)) {
                    if (pc != (QUEEN | sidebit) || nd == 1) {
                        for (di = 0; di < 4; ++di) {
                            d = bish_d[di];
                            tt = (int)s;
                            for (;;) {
                                tt += d;
                                if (!SQ_VALID(tt))
                                    break;
                                t = (unsigned char)tt;
                                cap = p->board[t];
                                if (!cap) {
                                    if (!captures_only)
                                        ADD(s, t, 0, 0);
                                } else {
                                    if ((cap & BLACK) != sidebit)
                                        ADD(s, t, MF_CAP, 0);
                                    break;
                                }
                            }
                        }
                    }
                }
                break;
            }
            }
        }
    return n;
}

/* ---------------------------------------------------------------------- */
/* make / unmake                                                          */
/* ---------------------------------------------------------------------- */
void make_move(Position *p, const Move *m, Undo *u)
{
    unsigned char pc = p->board[m->from];
    unsigned char cap;
    unsigned char sidebit = (pc & BLACK);

    u->moved = pc;
    u->castle = p->castle;
    u->ep = p->ep;
    u->halfmove = p->halfmove;

    if (m->flags & MF_EP) {
        unsigned char victim =
            (unsigned char)((m->to & 0x0F) | (m->from & 0x70));
        cap = p->board[victim];
        u->captured = cap;
        p->board[victim] = 0;
    } else {
        cap = p->board[m->to];
        u->captured = cap;
    }

    p->board[m->from] = 0;
    p->board[m->to] =
        (m->flags & MF_PROMO) ? (unsigned char)(m->promo | sidebit) : pc;

    /* castling: move the rook too */
    if (m->flags & MF_CASTLE) {
        if ((m->to & 7) == 6) {
            unsigned char rf = (unsigned char)(m->to + 1);
            unsigned char rt = (unsigned char)(m->to - 1);
            p->board[rt] = p->board[rf];
            p->board[rf] = 0;
        } else {
            unsigned char rf = (unsigned char)(m->to - 2);
            unsigned char rt = (unsigned char)(m->to + 1);
            p->board[rt] = p->board[rf];
            p->board[rf] = 0;
        }
    }

    /* castling rights */
    if ((pc & 7) == KING) {
        /* white king: clear bits 0+1 (mask ~3); black king: bits 2+3 (~12) */
        p->castle = (unsigned char)(p->castle &
                                    (sidebit ? (unsigned char)~12
                                             : (unsigned char)~3));
    }
    if (m->from == 0x07 || m->to == 0x07)
        p->castle &= (unsigned char)~1;
    if (m->from == 0x00 || m->to == 0x00)
        p->castle &= (unsigned char)~2;
    if (m->from == 0x77 || m->to == 0x77)
        p->castle &= (unsigned char)~4;
    if (m->from == 0x70 || m->to == 0x70)
        p->castle &= (unsigned char)~8;

    /* en passant square */
    if (m->flags & MF_DBL)
        p->ep = (signed char)((m->from + m->to) / 2);
    else
        p->ep = -1;

    /* halfmove clock */
    if ((pc & 7) == PAWN || cap)
        p->halfmove = 0;
    else if (p->halfmove < 200)
        ++p->halfmove;

    p->side ^= 1;
}

void unmake_move(Position *p, const Move *m, const Undo *u)
{
    p->side ^= 1;

    p->board[m->from] = u->moved;
    if (m->flags & MF_EP) {
        unsigned char victim =
            (unsigned char)((m->to & 0x0F) | (m->from & 0x70));
        p->board[m->to] = 0;
        p->board[victim] = u->captured;
    } else {
        p->board[m->to] = u->captured;
    }

    if (m->flags & MF_CASTLE) {
        if ((m->to & 7) == 6) {
            unsigned char rf = (unsigned char)(m->to + 1);
            unsigned char rt = (unsigned char)(m->to - 1);
            p->board[rf] = p->board[rt];
            p->board[rt] = 0;
        } else {
            unsigned char rf = (unsigned char)(m->to - 2);
            unsigned char rt = (unsigned char)(m->to + 1);
            p->board[rf] = p->board[rt];
            p->board[rt] = 0;
        }
    }

    p->castle = u->castle;
    p->ep = u->ep;
    p->halfmove = u->halfmove;
}

/* is the move legal? (does not leave own king in check) */
static unsigned char move_is_legal(Position *p, const Move *m, unsigned char mover)
{
    Undo u;
    unsigned char k, ok;

    make_move(p, m, &u);
    k = find_king(p, mover);
    ok = (unsigned char)!is_attacked(p, k, p->side);
    unmake_move(p, m, &u);
    return ok;
}

unsigned char legal_count(Position *p)
{
    unsigned char n, i, cnt = 0, mover = p->side;
    unsigned int start = g_mtop;

    n = gen_moves(p, &g_mstack[start], 0, MAX_TOTMOVES - start);
    g_mtop = start + n;
    for (i = 0; i < n; ++i) {
        if (move_is_legal(p, &g_mstack[start + i], mover))
            ++cnt;
    }
    g_mtop = start;
    return cnt;
}

unsigned char legal_moves(Position *p, Move *out)
{
    unsigned char n, i, cnt = 0, mover = p->side;
    unsigned int start = g_mtop;

    n = gen_moves(p, &g_mstack[start], 0, MAX_TOTMOVES - start);
    g_mtop = start + n;
    for (i = 0; i < n; ++i) {
        if (move_is_legal(p, &g_mstack[start + i], mover)) {
            if (cnt != i || out != &g_mstack[start])
                out[cnt] = g_mstack[start + i];
            ++cnt;
        }
    }
    g_mtop = start;
    return cnt;
}

/* ---------------------------------------------------------------------- */
/* evaluation                                                             */
/* ---------------------------------------------------------------------- */
static const int piece_val[7] = { 0, 100, 320, 330, 500, 900, 0 };

/* piece-square tables, white perspective, a1..h1 first (rank1-first) */
static const signed char pst_pawn[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10,-20,-20, 10, 10,  5,
      5, -5,-10,  0,  0,-10, -5,  5,
      0,  0,  0, 20, 20,  0,  0,  0,
      5,  5, 10, 25, 25, 10,  5,  5,
     10, 10, 20, 30, 30, 20, 10, 10,
     50, 50, 50, 50, 50, 50, 50, 50,
      0,  0,  0,  0,  0,  0,  0,  0
};
static const signed char pst_knight[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};
static const signed char pst_bishop[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};
static const signed char pst_rook[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10, 10, 10, 10, 10,  5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      0,  0,  0,  5,  5,  0,  0,  0
};
static const signed char pst_queen[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
      0,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};
static const signed char pst_king[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};

int evaluate(const Position *p)
{
    int score = 0;
    unsigned char fr, rk, s, pc, idx;
    const signed char *tab;

    for (rk = 0; rk < 8; ++rk)
        for (fr = 0; fr < 8; ++fr) {
            s = SQ(fr, rk);
            pc = p->board[s];
            if (!pc)
                continue;
            idx = PST_IDX(s);
            if (pc & BLACK) {
                score -= piece_val[pc & 7];
                tab = NULL;
                switch (pc & 7) {
                case PAWN:   tab = pst_pawn;   idx ^= 56; break;
                case KNIGHT: tab = pst_knight; idx ^= 56; break;
                case BISHOP: tab = pst_bishop; idx ^= 56; break;
                case ROOK:   tab = pst_rook;   idx ^= 56; break;
                case QUEEN:  tab = pst_queen;  idx ^= 56; break;
                case KING:   tab = pst_king;   idx ^= 56; break;
                }
                if (tab)
                    score -= tab[idx];
            } else {
                score += piece_val[pc & 7];
                tab = NULL;
                switch (pc & 7) {
                case PAWN:   tab = pst_pawn;   break;
                case KNIGHT: tab = pst_knight; break;
                case BISHOP: tab = pst_bishop; break;
                case ROOK:   tab = pst_rook;   break;
                case QUEEN:  tab = pst_queen;  break;
                case KING:   tab = pst_king;   break;
                }
                if (tab)
                    score += tab[idx];
            }
        }
    return (p->side == WHITE_SIDE) ? score : -score;
}

/* ---------------------------------------------------------------------- */
/* search                                                                 */
/* ---------------------------------------------------------------------- */
#define MATE 30000
#define INF 31000

static int qsearch(Position *p, int alpha, int beta, unsigned char ply)
{
    unsigned char n, i, mover;
    unsigned int start;
    int stand, v;
    Undo u;

    stand = evaluate(p);
    if (stand >= beta)
        return beta;
    if (stand > alpha)
        alpha = stand;
    if (ply >= MAX_PLY - 1)
        return alpha;

    start = g_mtop;
    if (start > MAX_TOTMOVES - 16)
        return alpha;
    mover = p->side;
    n = gen_moves(p, &g_mstack[start], 1, MAX_TOTMOVES - start);
    g_mtop = start + n;
    DBG("q ply=%u d? top=%u n=%u\n", ply, start, n);

    for (i = 0; i < n; ++i) {
        Move *mv = &g_mstack[start + i];
        Undo *up = &u;
        unsigned char k;

        make_move(p, mv, up);
        k = find_king(p, mover);
        if (is_attacked(p, k, p->side)) {
            unmake_move(p, mv, up);
            continue;
        }
        v = -qsearch(p, -beta, -alpha, (unsigned char)(ply + 1));
        unmake_move(p, mv, up);
        if (v >= beta) {
            g_mtop = start;
            return beta;
        }
        if (v > alpha)
            alpha = v;
    }
    g_mtop = start;
    return alpha;
}

static int negamax(Position *p, unsigned char depth, int alpha, int beta,
                   unsigned char ply)
{
    unsigned char n, i, mover, legal = 0;
    unsigned int start;
    int best, v;

    if (ply >= MAX_PLY - 1)
        return evaluate(p);
    if (depth == 0)
        return qsearch(p, alpha, beta, ply);

    start = g_mtop;
    if (start > MAX_TOTMOVES - 16)
        return evaluate(p);
    mover = p->side;
    n = gen_moves(p, &g_mstack[start], 0, MAX_TOTMOVES - start);
    g_mtop = start + n;
    DBG("neg depth=%u ply=%u top=%u n=%u\n", depth, ply, start, n);

    best = -INF;
    for (i = 0; i < n; ++i) {
        Move *mv = &g_mstack[start + i];
        Undo u;
        unsigned char k;

        make_move(p, mv, &u);
        k = find_king(p, mover);
        if (is_attacked(p, k, p->side)) {
            unmake_move(p, mv, &u);
            continue;
        }
        legal = 1;
        v = -negamax(p, (unsigned char)(depth - 1), -beta, -alpha,
                     (unsigned char)(ply + 1));
        unmake_move(p, mv, &u);
        if (v > best)
            best = v;
        if (v > alpha)
            alpha = v;
        if (alpha >= beta)
            break;
    }
    g_mtop = start;
    if (!legal)
        return in_check(p, mover) ? -(MATE - ply) : 0;
    return best;
}

/* move ordering score: captures first (MVV-LVA), then quiet moves
 * ordered by how much the move improves the mover's piece-square score.
 * At depth 2 many moves genuinely tie in evaluation, so this ordering is
 * what decides the visible 'personality' of the AI. */
static const signed char *pst_for(unsigned char typ)
{
    switch (typ) {
    case PAWN:   return pst_pawn;
    case KNIGHT: return pst_knight;
    case BISHOP: return pst_bishop;
    case ROOK:   return pst_rook;
    case QUEEN:  return pst_queen;
    case KING:   return pst_king;
    }
    return NULL;
}

static unsigned char order_score(const Position *p, const Move *m)
{
    unsigned char pc = p->board[m->from];

    if (m->flags & (MF_CAP | MF_PROMO)) {
        unsigned char victim = PAWN;
        if (!(m->flags & MF_EP))
            victim = p->board[m->to] & 7;
        return (unsigned char)(128 + (victim << 3) +
                               ((m->flags & MF_PROMO) ? 15 : 0) -
                               (pc & 7));
    }
    {
        const signed char *tab = pst_for(pc & 7);
        int d;

        if (!tab)
            return 60;
        if (pc & BLACK)
            d = tab[PST_IDX(m->to) ^ 56] - tab[PST_IDX(m->from) ^ 56];
        else
            d = tab[PST_IDX(m->to)] - tab[PST_IDX(m->from)];
        if (d < -60)
            d = -60;
        if (d > 59)
            d = 59;
        return (unsigned char)(60 + d);
    }
}

int search(Position *p, unsigned char depth, Move *best)
{
    unsigned char n, i, j, legal_n = 0, mover = p->side;
    int alpha = -INF, bestv = -INF, v;

    g_mtop = 0;
    n = gen_moves(p, &g_mstack[0], 0, MAX_TOTMOVES);

    /* keep only legal moves, compacted to the front */
    for (i = 0; i < n; ++i) {
        if (move_is_legal(p, &g_mstack[i], mover)) {
            if (legal_n != i)
                g_mstack[legal_n] = g_mstack[i];
            ++legal_n;
        }
    }
    if (legal_n == 0) {
        best->from = best->to = best->flags = best->promo = 0;
        return in_check(p, mover) ? -MATE : 0; /* mate or stalemate */
    }

    /* selection sort, best ordering score first */
    for (i = 0; i < legal_n; ++i) {
        unsigned char bi = i;
        unsigned char bs = order_score(p, &g_mstack[i]);
        for (j = (unsigned char)(i + 1); j < legal_n; ++j) {
            unsigned char sc = order_score(p, &g_mstack[j]);
            if (sc > bs) {
                bs = sc;
                bi = j;
            }
        }
        if (bi != i) {
            Move t;
            t = g_mstack[i];
            g_mstack[i] = g_mstack[bi];
            g_mstack[bi] = t;
        }
    }

    g_mtop = legal_n;
    *best = g_mstack[0];
    DBG("search depth=%u legal=%u\n", depth, legal_n);
    for (i = 0; i < legal_n; ++i) {
        Move mv;
        Undo u;

        mv = g_mstack[i];
        {
            char dbgbuf[8];
            move_to_str(&mv, dbgbuf);
            DBG("root %s i=%u/%u\n", dbgbuf, i, legal_n);
        }
        make_move(p, &mv, &u);        v = -negamax(p, (unsigned char)(depth - 1), -INF, -alpha, 1);
        unmake_move(p, &mv, &u);
        DBG("  score=%d best=%d\n", v, bestv);
        /* first move with the best score wins (ties are broken by the
           ordering heuristic, so play stays purposeful) */
        if (v > bestv) {
            bestv = v;
            *best = mv;
        }
        if (v > alpha)
            alpha = v;
    }
    g_mtop = 0;
    return bestv;
}

void sq_to_str(unsigned char sq, char *out)
{
    out[0] = (char)('a' + FILE_OF(sq));
    out[1] = (char)('1' + RANK_OF(sq));
    out[2] = 0;
}

unsigned char str_to_sq(const char *s)
{
    unsigned char f, r;
    char c0 = s[0];

    if (c0 >= 'A' && c0 <= 'H')
        c0 = (char)(c0 + 32);
    if (c0 < 'a' || c0 > 'h' || s[1] < '1' || s[1] > '8')
        return 0xFF;
    f = (unsigned char)(c0 - 'a');
    r = (unsigned char)(s[1] - '1');
    return SQ(f, r);
}

const char *move_to_str(const Move *m, char *buf)
{
    sq_to_str(m->from, buf);
    buf[2] = (char)('a' + FILE_OF(m->to));
    buf[3] = (char)('1' + RANK_OF(m->to));
    buf[4] = 0;
    if (m->flags & MF_PROMO) {
        static const char pt[] = "???nbrqk";
        buf[4] = pt[m->promo];
        buf[5] = 0;
    }
    return buf;
}

unsigned long perft(Position *p, unsigned char depth)
{
    unsigned long nodes = 0;
    unsigned char n, i, mover;
    unsigned int start;

    if (depth == 0)
        return 1;

    start = g_mtop;
    mover = p->side;
    n = gen_moves(p, &g_mstack[start], 0, MAX_TOTMOVES - start);
    g_mtop = start + n;
    for (i = 0; i < n; ++i) {
        Move *mv = &g_mstack[start + i];
        Undo u;
        unsigned char k;

        make_move(p, mv, &u);
        k = find_king(p, mover);
        if (is_attacked(p, k, p->side)) {
            unmake_move(p, mv, &u);
            continue;
        }
        nodes += (depth == 1) ? 1 : perft(p, (unsigned char)(depth - 1));
        unmake_move(p, mv, &u);
    }
    g_mtop = start;
    return nodes;
}
