/* Client engine builds the launcher knows how to patch.

   A revision folder (game:\rev\<revision>\) holds data plus the program files that read it: PolCore.xex,
   0001\FFXi.dll and 0001\FFXiMain.dll. The data can come from any year; what the launcher must recognise is
   each program file's build, because every fix it makes lands on addresses inside one build. A build is
   identified by its XEX time stamp (XEX_HEADER_CHECKSUM_TIMESTAMP), which the loader also keeps in the
   module's loader entry.

   Two kinds of per-build facts:
     - import thunks: which thunk calls which xam/xboxkrnl export. Generated from Xenia's import listings
       (tools/gen-engine-imports.py -> engine_imports.h), so a new build needs one Xenia run, no reverse
       engineering.
     - profiles: addresses found by hand (Ghidra) in PolCore and FFXiMain, in engine.c. */
#ifndef ENGINE_H
#define ENGINE_H

#include <xtl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENGINE_XAM 0      /* xam.xex */
#define ENGINE_KERNEL 1   /* xboxkrnl.exe */

typedef struct _ENGINE_IMPORT {
    BYTE library;     /* ENGINE_XAM or ENGINE_KERNEL */
    WORD ordinal;
    DWORD thunk;      /* the module's 16-byte import thunk for that export */
} ENGINE_IMPORT;

typedef struct _ENGINE_MODULE {
    const char *name;   /* file name: PolCore.xex, PolViewer.xex, FFXi.dll, FFXiMain.dll */
    DWORD timestamp;
    DWORD checksum;
    const ENGINE_IMPORT *imports;
    int count;
} ENGINE_MODULE;

/* PolCore: the interface table it hands FFXi.dll (slot offsets such as +0x374 or +0x1834 are the table's
   layout, the same in every build seen so far) and its SQINET start-up routine. */
typedef struct _POLCORE_PROFILE {
    DWORD timestamp;
    const char *label;     /* PlayOnline version, e.g. 20110726_A */
    DWORD table;
    DWORD sqinet_start;
} POLCORE_PROFILE;

/* FFXiMain: where the lobby keeps its character list and connection state, and where the world packet
   handlers are. */
typedef struct _FFXIMAIN_PROFILE {
    DWORD timestamp;
    const char *label;     /* client version, e.g. 40160203_0 */
    DWORD lobby_root;      /* global holding the lobby root object */
    DWORD lobby_chars;     /* root + this: character list, lobby_char_size bytes per slot */
    DWORD lobby_char_size;
    DWORD lobby_object;    /* root + this: pointer to the lobby connection */
    DWORD lobby_state;     /* connection + this: state word (6 = closed) */
    DWORD game_root;       /* global holding the game object */
    DWORD packet_first;    /* game object + this: first incoming packet handler per id (0x000-0x1FF) */
    DWORD packet_handlers; /* game object + this: main incoming packet handler per id (0x000-0x11E) */
    DWORD code_start;      /* FFXiMain's code section [code_start, code_end): handler pointers point here */
    DWORD code_end;
    DWORD model_races;     /* list of 9 pointers (race 0-8) to each race's equipment model table: 9 slots of
                              5 (first file ID, model count) blocks; race 0 shares race 1's */
    DWORD model_sites[3][5]; /* where the model tables are read (launcher.c, install_model_lookup): check start,
                              check resume, cascade start, cascade resume, cascade skip; 0 = none */
    DWORD swap_dispatch;   /* receive loop's per-packet byte-swap dispatcher: (id, packet, size, flag) ->
                              expected size; calls *(swap_table + id * 4)(packet, size, flag) for ids below
                              0x11F (launcher.c, install_size_check) */
    DWORD swap_table;
    /* Title screen version line (launcher.c, install_title): the stub site over the "ver." + patch.ver strcat
       (14 instructions: the stub takes 6, the draw trampoline the next 4), where that block resumes, the call
       that draws the line, the object whose +0x10 holds patch.ver, the text drawer (x, y, text, colour), its
       width measure (text -> units) and the global holding the font; 0 = leave the title alone. */
    DWORD title_fill;
    DWORD title_resume;
    DWORD title_draw_call;
    DWORD title_version;
    DWORD text_draw;
    DWORD text_width;
    DWORD text_font;
} FFXIMAIN_PROFILE;

/* The known build of `name` with that time stamp, or NULL. */
const ENGINE_MODULE *engine_module(const char *name, DWORD timestamp);

/* Time stamp of a loaded module (XexLoadImage/XexGetModuleHandle handle), or 0. */
DWORD engine_loaded_timestamp(HANDLE module);

/* Time stamp from an XEX file's header, or 0 when the file is missing or not XEX2. */
DWORD engine_file_timestamp(const char *path);

/* Thunk address of export `ordinal` of `library` in module build m, or 0 when the module does not import it. */
DWORD engine_thunk(const ENGINE_MODULE *m, int library, int ordinal);

const POLCORE_PROFILE *engine_polcore(DWORD timestamp);
const FFXIMAIN_PROFILE *engine_ffximain(DWORD timestamp);

#ifdef __cplusplus
}
#endif

#endif
