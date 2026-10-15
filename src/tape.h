/*
 * tape.h - cassette save/restore for the game state.
 *
 * Both directions drive the ROM's own tape routines ($E6xx/$E4xx), so
 * the blocks are exactly what CSAVE/CLOAD would produce: leader, $24
 * sync, the 9-byte address header in reverse order, a NUL-terminated
 * name and the data.  Oricutron records/serves them like any tape.
 */
#ifndef TAPE_H
#define TAPE_H

/* Write buf[0..len-1] to tape under NUL-terminated name.
   Returns 1 (the ROM's save path reports no usable error flags). */
unsigned char tape_save(const char *name, const unsigned char *buf,
                        unsigned len);

/* Read the first block whose name matches into buf.  The byte range
   comes from the block header itself, so len is the expected size and
   is not trusted for reading.  Returns 0 if the ROM flagged a
   read/verify error.  May spin forever if the tape has no matching
   block (same as a real CLOAD). */
unsigned char tape_load(const char *name, unsigned char *buf,
                        unsigned len);

#endif
