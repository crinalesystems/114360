/* Client engine builds: lookups over the generated import tables and the hand-mapped profiles. See engine.h. */
#include <stdio.h>
#include <string.h>

#include "engine.h"
#include "engine_imports.h"

/* Loader entry (LDR_DATA_TABLE_ENTRY) field holding the module's time stamp; the same offset on a console
   and in Xenia (PolCore read back 4E240911 there). */
#define LDR_TIMESTAMP 0x50

/* XEX2 header: "XEX2", module flags, PE offset, reserved, security offset, optional header count, then
   (key, value) pairs. A key whose low byte is above 1 has its data at file offset `value`. */
#define XEX_MAGIC 0x58455832
#define XEX_KEY_CHECKSUM_TIMESTAMP 0x00018002   /* data: u32 checksum, u32 time stamp */
#define XEX_MAX_HEADERS 64

static const POLCORE_PROFILE polcore_profiles[] = {
    /* 20110726_A: interface table at 0x82524210 (the address PolCore passes to FFXi.dll ordinal 2);
       SQINET start 0x82453128 (lock, 0x82453050, unlock), which sets the ready flag 0x824B9BDC. */
    {0x4E240911, "20110726_A", 0x82524210, 0x82453128},
};

static const FFXIMAIN_PROFILE ffximain_profiles[] = {
    /* 40160203_0 (built Feb 2 2016): lobby root global 0x8481FC78, read by the lobby poll 0x841B8D60 and
       world entry 0x841A4388; character list 16 x 0x8C bytes at +0x13844; connection at +0x13690, its state
       word at +0x2F0D4. Game object global 0x84820310; the world receive loop (0x841B4068) calls packet
       `id`'s handlers from *(game + 0x3BFA4 + id * 4), then (normal play state) *(game + 0x3BB28 + id * 4),
       as handler(connection, buffer, packet). Code section 0x84100000-0x84720000 (import thunks follow).
       Equipment model tables: pointer list 0x84792F28 (races 0-8) to 0x168-byte tables from 0x847918A8, read in
       three places (slot = look >> 12, entry = table + slot * 40): each checks the model number against the
       block counts (the first sums only four of the five; the other two all five), then cascades through the
       five blocks, both unrolled. Per place: check start and resume, cascade start, resume (file in r30) and
       skip (no file). The receive loop (0x841B4068) byte-swaps each packet through 0x84185B08, which calls the
       packet's routine from the table at 0x847837F0 and returns the size that layout expects; the loop logs a
       mismatch only into a buffer nobody prints. The title screen's "(c) 2002, <year>" and version line come
       from 0x843ACD08: a 0x100-byte buffer at r1+0xE0 gets "ver." and, at 0x843ACDB4-0x843ACDE8, the strcat of
       patch.ver (*(0x84960EA4) + 0x10, 0x842E2A30); 0x843ACE78 draws it through 0x8430C1D8 (x, y, text,
       colour; scale 1, not right-aligned), whose width measure is 0x84309080 and font global 0x849A1C08. */
    {0x56B0E65D, "40160203_0", 0x8481FC78, 0x13844, 0x8C, 0x13690, 0x2F0D4, 0x84820310, 0x3BFA4, 0x3BB28,
     0x84100000, 0x84720000, 0x84792F28,
     {{0x8423C740, 0x8423C780, 0x8423C788, 0x8423C840, 0x8423C8E8},
      {0x8423D2D0, 0x8423D318, 0x8423D320, 0x8423D3DC, 0x8423D47C},
      {0x8423D560, 0x8423D5A8, 0x8423D620, 0x8423D6DC, 0x8423D780}},
     0x84185B08, 0x847837F0,
     0x843ACDB4, 0x843ACDEC, 0x843ACE78, 0x84960EA4, 0x8430C1D8, 0x84309080, 0x849A1C08},
};

const ENGINE_MODULE *engine_module(const char *name, DWORD timestamp)
{
    int i;
    for (i = 0; i < sizeof(engine_modules) / sizeof(engine_modules[0]); i++) {
        if (engine_modules[i].timestamp == timestamp && _stricmp(engine_modules[i].name, name) == 0) {
            return &engine_modules[i];
        }
    }
    return NULL;
}

DWORD engine_loaded_timestamp(HANDLE module)
{
    return module != NULL ? *(const DWORD *)((const BYTE *)module + LDR_TIMESTAMP) : 0;
}

static int read_at(FILE *f, long offset, DWORD *out, int count)
{
    return fseek(f, offset, SEEK_SET) == 0 && fread(out, sizeof(DWORD), count, f) == (size_t)count;
}

DWORD engine_file_timestamp(const char *path)
{
    DWORD header[6], pair[2], data[2], timestamp = 0;
    DWORD i;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    if (read_at(f, 0, header, 6) && header[0] == XEX_MAGIC && header[5] <= XEX_MAX_HEADERS) {
        for (i = 0; i < header[5]; i++) {
            if (!read_at(f, 0x18 + i * 8, pair, 2)) {
                break;
            }
            if (pair[0] == XEX_KEY_CHECKSUM_TIMESTAMP) {
                if (read_at(f, (long)pair[1], data, 2)) {
                    timestamp = data[1];
                }
                break;
            }
        }
    }
    fclose(f);
    return timestamp;
}

DWORD engine_thunk(const ENGINE_MODULE *m, int library, int ordinal)
{
    int i;
    for (i = 0; m != NULL && i < m->count; i++) {
        if (m->imports[i].library == library && m->imports[i].ordinal == ordinal) {
            return m->imports[i].thunk;
        }
    }
    return 0;
}

const POLCORE_PROFILE *engine_polcore(DWORD timestamp)
{
    int i;
    for (i = 0; i < sizeof(polcore_profiles) / sizeof(polcore_profiles[0]); i++) {
        if (polcore_profiles[i].timestamp == timestamp) {
            return &polcore_profiles[i];
        }
    }
    return NULL;
}

const FFXIMAIN_PROFILE *engine_ffximain(DWORD timestamp)
{
    int i;
    for (i = 0; i < sizeof(ffximain_profiles) / sizeof(ffximain_profiles[0]); i++) {
        if (ffximain_profiles[i].timestamp == timestamp) {
            return &ffximain_profiles[i];
        }
    }
    return NULL;
}
