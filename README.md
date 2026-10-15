The Oric 3D Chess Game

(c) 2026 Paul F. Johnson

Released under the ultra liberal DILLIGAF licence.

There are lots of chess games for the Oric computer. Well, about 5 of them, but given this machine was only out for about 2 years, that was not a bad number.

My little sister loves chess so I thought, why not write one myself. The problem is that there are chess games for the Oric and I wanted something special for her- and this is it. A playable 3D chess game!

Given the limitations of the machine (1MHz 6502 processor and 37K of available memory), getting this in and having a game that plays at something approaching vaguely reasonable is actually really good.

You will need cc65 and an emulator to run this on (though you may be able to run this on a real machine using one of those SD card things you can find on eBay)

The game is written in C

## Controls

Moves are typed as from-square then to-square, e.g. E2E4. Pawn promotion
auto-queens.

    U   undo (takes back the computer's reply too)
    N   new game
    C   toggle the computer opponent (CPU plays black / two players)
    L   cycle search level: 1 fast, 2 strong, 3 expert
    S   save the game to tape
    R   restore the game from tape

## Building

You need the cc65 tool suite. From the project root:

    cl65 -t atmos -C oric.cfg -O -o chess3d.tap -m chess3d.map -Ln chess3d.lbl src/*.c

The -O matters: without it the binary outgrows the memory window in
oric.cfg. The result is a normal Oric tape file that autostarts. In
Oricutron use F1 -> "Insert tape..." (or the T key), pick chess3d.tap,
then RUN - or start Oricutron with the tape pre-inserted.

## Saving and restoring

S writes the whole game - board position, move history, side to move,
search level and mode - to tape as a single block named CHESS3D,
protected by a CRC16. R reads it back; if the tape has no valid CHESS3D
block the game says so and carries on. Both sides use the Oric ROM's own
tape routines (the same ones CSAVE/CLOAD use), so it works unchanged on
a real machine with a datasette.

To watch the round trip in Oricutron: play a move or two, press S, then
press R to bring the position back. The ROM's tape output is a standard
CSAVE-style signal, so Oricutron records it like any other tape output
(F1 -> "Save tape output..." / F9 if it does not capture it
automatically); use F1 -> "Insert tape..." to play the saved file back
before pressing R.

## Boot time

The first 3D redraw takes about 107 million 6502 cycles - roughly 1
minute 45 on a real 1MHz Oric. Before the renderer was reworked (byte-wise
span fills, precomputed face depths, incremental Bresenham lines and an
integer DDA polygon filler with no per-row divides) it took 170 million.
