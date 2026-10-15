/*
 * kbd.h - direct Oric keyboard matrix scanning (no ROM, no IRQs).
 *
 * The matrix is read through the VIA ($0300) and the AY PSG register 14
 * exactly like the OSDK full-matrix example: PB0-2 select the row, the
 * PSG selects the column (negative logic), PB3 is the sense input.
 */
#ifndef KBD_H
#define KBD_H

void kbd_init(void);            /* configures VIA, call once (with IRQs off) */
unsigned char kbd_get(void);    /* next key press (ASCII), 0 if none */

#endif
