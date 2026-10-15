/*
 * tape.c - save/restore via the ROM's cassette routines.
 *
 * Reconstructed from BASIC 1.1B ($F440 sym file):
 *   $E76A SetupTape        motor + VIA timer for tape output
 *   $E607 WriteFileHeader  leader, $24, 9 header bytes ($02B0..$02A8,
 *                          descending!), name + NUL
 *   $E62E WriteDataBlock   bytes [start..end] inclusive via $E62E,
 *                          using IncTapeCount ($E56C) for the end test
 *   $E4AC TapeSync         hunt $24, fill $02B0..$02A8 from the header
 *                          (so the header *overrides* the range vars),
 *                          read the name into $0293 and compare with
 *                          $027F; mismatch = try the next block
 *   $E4E0 GetTapeData      read the block into the header range
 *   $E93D close            ClrTapeStatus + ResetVIA + timer restore
 *
 * The nine header bytes on tape (reverse memory order):
 *   $02B0 $02AF $02AE $02AD $02AC $02AB $02AA $02A9 $02A8
 *   = ??    ??    err   auto  end.hi end.lo str.hi str.lo type
 * The same layout the ROM writes for CSAVE and cl65 emits for tapes.
 *
 * Flags cleared here are the ones GetTapeParams ($E7B2) clears before
 * parsing: $024D (turbo), $02AD/$02AE (auto/err), $025A/$025B (J/V),
 * $025C/$025D (error counters), $02B1.
 */
#include "tape.h"

#define R8(a)  (*(volatile unsigned char *)(unsigned)(a))
#define R16(a) (*(volatile unsigned int *)(unsigned)(a))

typedef void (*rom_fn)(void);

static void rom_call(unsigned addr)
{
    ((rom_fn)addr)();
}

static void clear_flags(void)
{
    static const unsigned int f[] = {
        0x024D, 0x02AD, 0x02AE, 0x025A,
        0x025B, 0x025C, 0x025D, 0x02B1
    };
    unsigned i;

    for (i = 0; i < sizeof f / sizeof f[0]; ++i)
        R8(f[i]) = 0;
    R8(0x02A8) = 0; /* file type: plain data, no auto-run */
}

static void set_name(const char *name)
{
    unsigned i;

    for (i = 0; i < 16 && name[i]; ++i)
        R8(0x027F + i) = (unsigned char)name[i];
    R8(0x027F + i) = 0;
}

unsigned char tape_save(const char *name, const unsigned char *buf,
                        unsigned len)
{
    clear_flags();
    set_name(name);
    R16(0x02A9) = (unsigned)buf;
    R16(0x02AB) = (unsigned)(buf + len - 1); /* inclusive end */

    rom_call(0xE76A); /* SetupTape            */
    rom_call(0xE607); /* WriteFileHeader      */
    rom_call(0xE62E); /* WriteDataBlock       */
    rom_call(0xE93D); /* ClrTapeStatus+close  */
    return 1;
}

unsigned char tape_load(const char *name, unsigned char *buf,
                        unsigned len)
{
    (void)len; /* the block header supplies the real range */

    clear_flags();
    set_name(name);
    R16(0x02A9) = (unsigned)buf;
    R16(0x02AB) = (unsigned)(buf + len - 1);

    rom_call(0xE76A); /* SetupTape   */
    rom_call(0xE4AC); /* TapeSync: sync + header + name search */
    rom_call(0xE4E0); /* GetTapeData */
    rom_call(0xE93D); /* close       */
    return (unsigned char)((R8(0x025C) | R8(0x025D)) == 0);
}
