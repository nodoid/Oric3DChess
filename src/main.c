/*
 * main.c - 3D Chess for the Oric / Oric Atmos.
 *
 * Perspective (true 3D) board and solid-shaded pieces on the 240x200
 * hi-res screen.  Moves are typed in algebraic-ish form (E2E4); the
 * computer replies at a selectable search depth.
 *
 * Commands:  <move>   e.g. E2E4, E7E8 (auto-queen)
 *            U  undo        N  new game
 *            C  toggle computer opponent
 *            L  cycle search level (1 fast / 2 strong / 3 expert)
 *            S  save game to tape    R  restore game from tape
 */
#include <string.h>
#include "engine.h"
#include "gfx.h"
#include "kbd.h"
#include "tape.h"

/* HUD rows (each text row is 8 pixels tall) */
#define HUD_TURN 0
#define HUD_STATUS 8
#define HUD_PROMPT 184
#define HUD_HINT 192

#define MAX_HIST 120

static Position pos;
static Move hmove[MAX_HIST];
static Undo hun[MAX_HIST];
static unsigned char hlen;
static unsigned char vs_computer; /* 1: computer plays black */
static unsigned char ai_depth;    /* 1..3 */
static signed char game_state;    /* 0 on, 1 white wins, 2 black wins, 3 draw */

static char line[20];
static unsigned char linelen;

/* --- tape save/restore ------------------------------------------------
 * One fixed-size block so the saved range never depends on hlen:
 * Position + full history arrays + settings + magic/version + CRC16. */
#define SAVE_MAGIC (0x4300 | 4) /* "C3" + layout version             */
#define SAVE_SIZE (sizeof(Position) + sizeof hmove + sizeof hun + 4 + 2 + 2)
static unsigned char savbuf[SAVE_SIZE];

static unsigned crc16(const unsigned char *p, unsigned n)
{
    unsigned c = 0xFFFF;
    unsigned i;

    while (n--) {
        c = (unsigned)(c ^ ((unsigned)*p++ << 8));
        for (i = 0; i < 8; ++i)
            c = (unsigned)((c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
}

static void status(const char *s)
{
    gfx_box(0, HUD_STATUS, 239, HUD_STATUS + 6, FILL_B);
    gfx_text(0, HUD_STATUS, s, 1);
}

static void draw_hud(void)
{
    gfx_box(0, HUD_TURN, 239, HUD_TURN + 6, FILL_B);
    gfx_text(0, HUD_TURN,
             pos.side == WHITE_SIDE ? "WHITE TO MOVE" : "BLACK TO MOVE", 1);
    if (vs_computer)
        gfx_text(162, HUD_TURN, "CPU", 1);
    else
        gfx_text(162, HUD_TURN, "HOT", 1);
    {
        char lv[3];
        lv[0] = 'L';
        lv[1] = (char)('0' + ai_depth);
        lv[2] = 0;
        gfx_text(198, HUD_TURN, lv, 1);
    }

    gfx_box(0, HUD_HINT, 239, HUD_HINT + 7, FILL_B);
    gfx_text(0, HUD_HINT, "U UNDO N NEW C CPU L LEVEL S SAVE R LOAD", 1);

    gfx_box(0, HUD_PROMPT, 239, HUD_PROMPT + 7, FILL_B);
    gfx_text(0, HUD_PROMPT, ">", 1);
    if (linelen)
        gfx_text(6, HUD_PROMPT, line, 1);
}

static void redraw(void)
{
    scene_draw(&pos);
    draw_hud();
}

static void post_move(void)
{
    if (legal_count(&pos) == 0) {
        if (in_check(&pos, pos.side)) {
            game_state = (pos.side == WHITE_SIDE) ? 2 : 1;
            status(game_state == 1 ? "CHECKMATE 1-0" : "CHECKMATE 0-1");
        } else {
            game_state = 3;
            status("STALEMATE 1/2-1/2");
        }
        return;
    }
    if (in_check(&pos, pos.side))
        status("CHECK");
    else
        status("");
}

static void do_move(const Move *m)
{
    if (hlen < MAX_HIST) {
        hmove[hlen] = *m;
        make_move(&pos, m, &hun[hlen]);
        ++hlen;
    } else {
        Undo u;
        make_move(&pos, m, &u);
    }
}

static void computer_turn(void)
{
    Move best;

    if (game_state)
        return;
    if (pos.side != BLACK_SIDE || !vs_computer)
        return;
    status("COMPUTER THINKING");
    search(&pos, ai_depth, &best);
    if (best.to != 0)
        do_move(&best);
}

static void new_game(void)
{
    set_startpos(&pos);
    hlen = 0;
    game_state = 0;
    linelen = 0;
    status("3D CHESS - TYPE E2E4");
    redraw();
}

static unsigned char try_move(const char *s)
{
    unsigned char from, to, i, n, found = 0;
    Move chosen, m;

    from = str_to_sq(s);
    to = str_to_sq(s + 2);
    if (from == 0xFF || to == 0xFF) {
        status("BAD SQUARE - TRY E2E4");
        return 0;
    }
    n = legal_moves(&pos, &g_mstack[0]);
    for (i = 0; i < n; ++i) {
        m = g_mstack[i];
        if (m.from != from || m.to != to)
            continue;
        if ((m.flags & MF_PROMO) && m.promo != QUEEN)
            continue; /* auto-queen */
        chosen = m;
        found = 1;
        break;
    }
    if (!found) {
        status("ILLEGAL MOVE");
        return 0;
    }
    do_move(&chosen);
    return 1;
}

static void exec_line(void)
{
    char cmd;

    line[linelen] = 0;
    if (linelen == 0)
        return;
    cmd = line[0];
    if (cmd >= 'A' && cmd <= 'Z')
        cmd = (char)(cmd + 32);

    if (linelen >= 4 && line[0] >= 'a' && line[0] <= 'h' &&
        line[1] >= '1' && line[1] <= '8') {
        if (game_state)
            status("GAME OVER - N FOR NEW");
        else if (try_move(line)) {
            if (vs_computer && pos.side == BLACK_SIDE) {
                computer_turn();
                redraw();
                post_move();
                if (!game_state && in_check(&pos, pos.side) == 0)
                    status("");
            } else {
                redraw();
                post_move();
            }
        }
        return;
    }

    switch (cmd) {
    case 'u': /* undo */
        if (hlen == 0) {
            status("NOTHING TO UNDO");
            return;
        }
        unmake_move(&pos, &hmove[hlen - 1], &hun[hlen - 1]);
        --hlen;
        if (vs_computer && hlen > 0 && pos.side == WHITE_SIDE) {
            unmake_move(&pos, &hmove[hlen - 1], &hun[hlen - 1]);
            --hlen;
        }
        game_state = 0;
        status("UNDONE");
        redraw();
        break;
    case 'n':
        new_game();
        break;
    case 'c':
        vs_computer ^= 1;
        status(vs_computer ? "COMPUTER PLAYS BLACK" : "TWO PLAYERS");
        redraw();
        break;
    case 'l':
        ++ai_depth;
        if (ai_depth > 3)
            ai_depth = 1;
        status("LEVEL 1 FAST / 2 STRONG / 3 EXPERT");
        redraw();
        break;
    case 's':
    {
        unsigned char *q = savbuf;
        unsigned c;

        memcpy(q, &pos, sizeof pos);
        q += sizeof pos;
        memcpy(q, hmove, sizeof hmove);
        q += sizeof hmove;
        memcpy(q, hun, sizeof hun);
        q += sizeof hun;
        *q++ = hlen;
        *q++ = vs_computer;
        *q++ = ai_depth;
        *q++ = (unsigned char)game_state;
        *q++ = (unsigned char)(SAVE_MAGIC & 0xFF);
        *q++ = (unsigned char)(SAVE_MAGIC >> 8);
        c = crc16(savbuf, (unsigned)(q - savbuf));
        *q++ = (unsigned char)(c & 0xFF);
        *q++ = (unsigned char)(c >> 8);

        status("SAVING TO TAPE...");
        tape_save("CHESS3D", savbuf, (unsigned)SAVE_SIZE);
        status("GAME SAVED");
        break;
    }
    case 'r':
    {
        unsigned ok = 0;

        status("LOADING FROM TAPE...");
        if (tape_load("CHESS3D", savbuf, (unsigned)SAVE_SIZE)) {
            unsigned c = crc16(savbuf, SAVE_SIZE - 2);
            unsigned want = (unsigned)savbuf[SAVE_SIZE - 2] |
                            ((unsigned)savbuf[SAVE_SIZE - 1] << 8);
            unsigned mag = (unsigned)savbuf[sizeof(Position) +
                            sizeof hmove + sizeof hun + 2] |
                            ((unsigned)savbuf[sizeof(Position) +
                             sizeof hmove + sizeof hun + 3] << 8);

            if (mag == SAVE_MAGIC && c == want) {
                const unsigned char *q = savbuf;

                memcpy(&pos, q, sizeof pos);
                q += sizeof pos;
                memcpy(hmove, q, sizeof hmove);
                q += sizeof hmove;
                memcpy(hun, q, sizeof hun);
                q += sizeof hun;
                hlen = *q++;
                vs_computer = *q++;
                ai_depth = *q++;
                game_state = (signed char)*q;
                linelen = 0;
                ok = 1;
            }
        }
        if (ok) {
            redraw(); /* the ROM prints on screen during the load */
            post_move();
            status("GAME LOADED");
        } else {
            status("NO VALID SAVE ON TAPE");
        }
        break;
    }
    default:
        status("U N C L OR MOVE E2E4");
        break;
    }
}

int main(void)
{
    kbd_init(); /* also configures VIA; IRQs stay masked (SEI below) */
    __asm__("sei");
    gfx_init();
    ai_depth = 2;
    vs_computer = 1;
    new_game();

    for (;;) {
        unsigned char c = kbd_get();

        if (c == 0)
            continue;
        if (c == 0x0D) { /* return */
            exec_line();
            linelen = 0;
            gfx_box(0, HUD_PROMPT, 239, HUD_PROMPT + 7, FILL_B);
            gfx_text(0, HUD_PROMPT, ">", 1);
        } else if (c == 0x08) { /* backspace */
            if (linelen) {
                --linelen;
                gfx_box(6 + linelen * 6, HUD_PROMPT,
                        6 + linelen * 6 + 5, HUD_PROMPT + 6, FILL_B);
            }
        } else if (c == 0x1B) { /* escape: clear line */
            linelen = 0;
            gfx_box(0, HUD_PROMPT, 239, HUD_PROMPT + 7, FILL_B);
            gfx_text(0, HUD_PROMPT, ">", 1);
        } else if (c >= 0x20 && c < 0x7F && linelen < 18) {
            char cell[2];
            cell[0] = (char)c;
            cell[1] = 0;
            gfx_text(6 + linelen * 6, HUD_PROMPT, cell, 1);
            line[linelen] = (char)c;
            ++linelen;
        }
    }
}
