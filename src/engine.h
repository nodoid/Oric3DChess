/*
 * engine.h - 0x88 mailbox chess engine for the Oric (Oric Atmos/Oric-1).
 *
 * Platform independent: no I/O, no ROM, no dynamic allocation.  The same
 * source is compiled both for the Oric (cl65 -t atmos) and for cc65's
 * sim65 simulator where it is verified with perft tests.
 */
#ifndef ENGINE_H
#define ENGINE_H

/* ---- piece encoding -------------------------------------------------
 * 0        = empty
 * 1..6     = white  pawn knight bishop rook queen king
 * 9..14    = black  (piece | 8)
 * type = p & 7,  color: 0 = white, 8 = black.
 */
#define EMPTY 0
#define PAWN 1
#define KNIGHT 2
#define BISHOP 3
#define ROOK 4
#define QUEEN 5
#define KING 6
#define BLACK 8

#define WHITE_SIDE 0
#define BLACK_SIDE 1

/* ---- 0x88 helpers ---------------------------------------------------- */
#define SQ_VALID(s) (((s) & 0x88) == 0)
#define FILE_OF(s) ((s) & 7)
#define RANK_OF(s) ((s) >> 4)
/* compact 0..63 index for piece-square tables */
#define PST_IDX(s) (((((s) >> 4) & 7) << 3) | ((s) & 7))
/* square from file(0-7) / rank(0-7) */
#define SQ(fr, rk) (((rk) << 4) | (fr))

/* ---- move flags ------------------------------------------------------ */
#define MF_CAP 0x01   /* capture                    */
#define MF_EP 0x02    /* en-passant capture         */
#define MF_CASTLE 0x04/* castling (king moves 2)    */
#define MF_DBL 0x08   /* double pawn push           */
#define MF_PROMO 0x10 /* promotion                  */

typedef struct {
    unsigned char from;  /* 0x88 square              */
    unsigned char to;    /* 0x88 square              */
    unsigned char flags; /* MF_xxx                   */
    unsigned char promo; /* promo type (KNIGHT..QUEEN) */
} Move;

typedef struct {
    unsigned char board[128];
    unsigned char side;      /* WHITE_SIDE / BLACK_SIDE */
    unsigned char castle;    /* bit0 WK bit1 WQ bit2 BK bit3 BQ */
    signed char ep;          /* en-passant 0x88 square or -1    */
    unsigned char halfmove;  /* halfmove clock (clamped at 200) */
} Position;

/* undo information recorded by make_move() */
typedef struct {
    unsigned char moved;    /* piece that left 'from'            */
    unsigned char captured; /* piece removed at 'to' (0 if none) */
    unsigned char castle;
    signed char ep;
    unsigned char halfmove;
} Undo;

/* --- search buffers (global so recursion does not eat the C stack) --- */
#define MAX_TOTMOVES 460
#define MAX_PLY 32
extern Move g_mstack[MAX_TOTMOVES];
extern unsigned int g_mtop;

/* --- API ------------------------------------------------------------- */
void set_startpos(Position *p);
unsigned char parse_fen(Position *p, const char *fen);
void engine_seed(unsigned int s);

/* pseudo-legal move generation; returns count written to out[].
 * captures_only != 0 restricts to captures and promotions (quiescence).
 * room = maximum number of moves that may be stored. */
unsigned char gen_moves(Position *p, Move *out, unsigned char captures_only,
                        unsigned int room);

/* legal move count for the side to move (fully validated) */
unsigned char legal_count(Position *p);

/* legal moves for the side to move, written to out[]; returns count */
unsigned char legal_moves(Position *p, Move *out);

/* is square attacked by pieces of 'side'? */
unsigned char is_attacked(const Position *p, unsigned char sq,
                          unsigned char side);
/* is 'side' in check? */
unsigned char in_check(const Position *p, unsigned char side);
/* locate the king of 'side' (0x88 square) */
unsigned char find_king(const Position *p, unsigned char side);

void make_move(Position *p, const Move *m, Undo *u);
void unmake_move(Position *p, const Move *m, const Undo *u);

/* evaluation, relative to side to move (positive = good for mover) */
int evaluate(const Position *p);

/* negamax alpha-beta search; returns score (relative to side to move).
 * best move is returned in *best.  depth = 1..3 */
int search(Position *p, unsigned char depth, Move *best);

/* perft (for tests) */
unsigned long perft(Position *p, unsigned char depth);

/* formatting helpers */
void sq_to_str(unsigned char sq, char *out); /* writes "e2" + NUL */
unsigned char str_to_sq(const char *s);      /* "e2" -> 0x88, 0xFF if bad */
const char *move_to_str(const Move *m, char *buf); /* "e2e4" + NUL */

#endif
