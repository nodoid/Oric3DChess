/*
 * kbd.c - Oric keyboard matrix scan.
 *
 * Protocol matches the Atmos ROM's own ReadKbdCol ($F561):
 *   - AY port A (register 14) drives the 8 column lines
 *   - VIA PB0-2 select the row, VIA PB3 is the key-down sense line
 *   - the AY register write uses the ROM's exact PCR bus sequence
 *     ($F590): BDIR = CB2, BC1 = CA2; $EE = latch address,
 *     $EC = write data, $CC = idle.  Earlier code pulsed $FF/$DD,
 *     which on a real 6522 never enters the write state, so reg14
 *     was never updated and no key was ever seen (verified under
 *     Oricutron, which implements the VIA/AY faithfully).
 * Key-down is edge detected between calls; shift selects the upper table.
 */
#include "kbd.h"

#define VIA_PORTB (*(volatile unsigned char *)0x0300)
#define VIA_DDRB  (*(volatile unsigned char *)0x0302)
#define VIA_DDRA  (*(volatile unsigned char *)0x0303)
#define VIA_PCR   (*(volatile unsigned char *)0x030C)
#define VIA_PORTA (*(volatile unsigned char *)0x030F)

/* special codes >= 0xE0 in the key tables */
#define K_LSHIFT 0xE1
#define K_RSHIFT 0xE2
#define K_UP 0xE3
#define K_DOWN 0xE4
#define K_LEFT 0xE5
#define K_RIGHT 0xE6
#define K_FUNCT 0xE7
#define K_CTRL 0xE8
#define K_ESC 0x1B

/*
 * Keymaps indexed by row*8 + col.  Column c is selected by driving
 * AY port B bit c low (mask ~(1<<c)); this matches the ROM's own
 * FindKey mask chain ($7F/$BF/$DF/$EF/$F7/$FB/$FD/$FE pairs with
 * col7..col0 in its column-major table) and Oricutron's qwktab.
 * The earlier bit7-first scheme read every column mirrored, so
 * e.g. ESC echoed 'r' and '2' echoed '^' (verified live).
 * Digit/punct split follows the ROM KeyCodeTab: digits unshifted,
 * &%^ etc. shifted.  Lowercase table for unshifted, uppercase for
 * shift.
 */
static const unsigned char key_lower[64] = {
    '7', 'n', '5', 'v', K_CTRL, '1', 'x', '3',
    'j', 't', 'r', 'f', 0, K_ESC, 'q', 'd',
    'm', '6', 'b', '4', K_CTRL, 'z', '2', 'c',
    'k', '9', ';', '-', 0, 0, '\\', '\'',
    ' ', ',', '.', K_UP, K_LSHIFT, K_LEFT, K_DOWN, K_RIGHT,
    'u', 'i', 'o', 'p', K_FUNCT, 0x08, ']', '[',
    'y', 'h', 'g', 'e', 0, 'a', 's', 'w',
    '8', 'l', '0', '/', K_RSHIFT, 0x0D, 0, '='
};

static const unsigned char key_upper[64] = {
    '&', 'N', '%', 'V', K_CTRL, '!', 'X', '#',
    'J', 'T', 'R', 'F', 0, K_ESC, 'Q', 'D',
    'M', '^', 'B', '$', K_CTRL, 'Z', '@', 'C',
    'K', '(', ';', '_', 0, 0, '|', '"',
    ' ', '<', '>', K_UP, K_LSHIFT, K_LEFT, K_DOWN, K_RIGHT,
    'U', 'I', 'O', 'P', K_FUNCT, 0x08, '}', '{',
    'Y', 'H', 'G', 'E', 0, 'A', 'S', 'W',
    '*', 'L', ')', '?', K_RSHIFT, 0x0D, 0, '+'
};

static unsigned char prev[8];

/* AY-3-8912 register write, ROM-exact bus protocol ($F590). */
static void ay_write(unsigned char reg, unsigned char val)
{
    unsigned char p = VIA_PCR;

    VIA_PORTA = reg;
    VIA_PCR = (unsigned char)(p | 0xEE);           /* latch address */
    VIA_PCR = (unsigned char)((p & 0x11) | 0xCC);  /* idle          */
    VIA_PORTA = val;
    VIA_PCR = (unsigned char)((p & 0x11) | 0xEC);  /* write data    */
    VIA_PCR = (unsigned char)((p & 0x11) | 0xCC);  /* idle          */
}

void kbd_init(void)
{
    VIA_DDRA = 0xFF;       /* port A = output (PSG data)      */
    VIA_DDRB = 0xF7;       /* PB0-2 out (row), PB3 in (sense) */
    VIA_PORTB = 0xB8;      /* idle rows, AY control lines set */
    ay_write(0x0E, 0x00);  /* all column lines low            */
}

static void scan(unsigned char *cur)
{
    unsigned char row, bit, col, v;

    for (row = 0; row < 8; ++row)
        cur[row] = 0;

    for (row = 0; row < 8; ++row) {
        /* all columns low: is any key down in this row? */
        ay_write(0x0E, 0x00);
        VIA_PORTB = (unsigned char)(row | 0xB8);
        /* cc65 CSEs a single volatile re-read after the RMW above (it tests
         * the stale sense bit), so read twice like the column loop does. */
        v = VIA_PORTB;
        v = VIA_PORTB;
        if ((v & 0x08) == 0)
            continue;
        for (bit = 0x80, col = 0x01; bit;
             bit = (unsigned char)(bit >> 1),
             col = (unsigned char)(col << 1)) {
            /* table col c <-> AY column bit c (mask ~(1<<c)) */
            ay_write(0x0E, (unsigned char)~col);
            VIA_PORTB = (unsigned char)(row | 0xB8);
            v = VIA_PORTB;
            v = VIA_PORTB; /* settle */
            if (v & 0x08)
                cur[row] |= bit;
        }
    }
}

unsigned char kbd_get(void)
{
    unsigned char cur[8];
    unsigned char row, bit, idx, code;
    unsigned char shifted;

    scan(cur);

    /* shift state: left shift row4 col4, right shift row7 col4 */
    shifted = (unsigned char)(((cur[4] | cur[7]) & 0x08) != 0);

    for (row = 0; row < 8; ++row) {
        unsigned char news = (unsigned char)(cur[row] & (unsigned char)~prev[row]);
        if (!news)
            continue;
        for (bit = 0x80, idx = 0; bit; bit = (unsigned char)(bit >> 1), ++idx) {
            if ((news & bit) == 0)
                continue;
            code = shifted ? key_upper[row * 8 + idx] : key_lower[row * 8 + idx];
            if (code >= K_LSHIFT && code != 0)
                code = 0; /* modifiers/unused produce nothing */
            for (row = 0; row < 8; ++row)
                prev[row] = cur[row];
            return code;
        }
    }
    for (row = 0; row < 8; ++row)
        prev[row] = cur[row];
    return 0;
}
