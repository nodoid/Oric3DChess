/*
 * kbd.c - Oric keyboard matrix scan.
 *
 * Protocol follows the OSDK Keyboard-FullMatrix reference:
 *   - select AY register 14 once (portA=14, PCR=$FF then $DD)
 *   - for each row: write 0 to reg14, put row in PB0-2, test PB3
 *   - per column: write ~column mask to reg14, reselect row, test PB3
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
 * Keymaps indexed by row*8 + col, col0 = matrix bit7 (as in the OSDK
 * reference).  Lowercase table for unshifted, uppercase for shift.
 */
static const unsigned char key_lower[64] = {
    '&', 'n', '%', 'v', K_CTRL, '!', 'x', '#',
    'j', 't', 'r', 'f', 0, K_ESC, 'q', 'd',
    'm', '^', 'b', '$', K_CTRL, 'z', '@', 'c',
    'k', '(', ';', '_', 0, 0, '\\', '\'',
    ' ', '<', '>', K_UP, K_LSHIFT, K_LEFT, K_DOWN, K_RIGHT,
    'u', 'i', 'o', 'p', K_FUNCT, 0x08, ']', '[',
    'y', 'h', 'g', 'e', 0, 'a', 's', 'w',
    '*', 'l', ')', '|', K_RSHIFT, 0x0D, 0, '='
};

static const unsigned char key_upper[64] = {
    '7', 'N', '5', 'V', K_CTRL, '1', 'X', '3',
    'J', 'T', 'R', 'F', 0, K_ESC, 'Q', 'D',
    'M', '6', 'B', '4', K_CTRL, 'Z', '2', 'C',
    'K', '9', ';', '-', 0, 0, '\\', '\'',
    ' ', ',', '.', K_UP, K_LSHIFT, K_LEFT, K_DOWN, K_RIGHT,
    'U', 'I', 'O', 'P', K_FUNCT, 0x08, ']', '[',
    'Y', 'H', 'G', 'E', 0, 'A', 'S', 'W',
    '8', 'L', '0', '\\', K_RSHIFT, 0x0D, 0, '='
};

static unsigned char prev[8];

void kbd_init(void)
{
    VIA_DDRA = 0xFF;       /* port A = output (PSG data)      */
    VIA_DDRB = 0xF7;       /* PB0-2 out (row), PB3 in (sense) */
    VIA_PORTB = 0x00;
    VIA_PORTA = 0x0E;      /* select PSG register 14          */
    VIA_PCR = 0xFF;
    VIA_PCR = 0xDD;
}

static void scan(unsigned char *cur)
{
    unsigned char row, bit, v;

    for (row = 0; row < 8; ++row)
        cur[row] = 0;

    for (row = 0; row < 8; ++row) {
        /* all columns low: is any key down in this row? */
        VIA_PORTA = 0x00;
        VIA_PCR = 0xFD;
        VIA_PCR = 0xDD;
        VIA_PORTB = (unsigned char)((VIA_PORTB & 0xF8) | row);
        /* cc65 CSEs a single volatile re-read after the RMW above (it tests
         * the stale sense bit), so read twice like the column loop does. */
        v = VIA_PORTB;
        v = VIA_PORTB;
        if ((v & 0x08) == 0)
            continue;
        for (bit = 0x80; bit; bit = (unsigned char)(bit >> 1)) {
            VIA_PORTA = (unsigned char)~bit;
            VIA_PCR = 0xFD;
            VIA_PCR = 0xDD;
            VIA_PORTB = (unsigned char)((VIA_PORTB & 0xF8) | row);
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
