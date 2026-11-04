// 114360 installer: one small program (SDL3 + preSDL; polp.c talks to PlayOnline's update server, ftp.c to a
// console's FTP server) with a window and a command line (command_line below). <install> is a local folder (for
// Xenia, a USB drive, or a copy with Xbox 360 Neighborhood) or a folder on the console over FTP (dest_*). It lays
// out
//
//   <install>/default.xex, launcher.ini         the launcher (payload/default.xex next to this program)
//   <install>/area.dat, <install>/PlayOnline/   PlayOnline's data (a disc image or a PlayOnline folder)
//   <install>/rev/XB2-40160203_0/               the 2016 client: PolCore.xex, 0001/ (required)
//   <install>/rev/PS2-<version>/                the 2016 client with 2010 data (optional; PS2-20100904_2)
//   <install>/data/                             with "share": every distinct data file once (pool_plan)
//   <install>/pol/X2U-20100601_1/PolCore.xex    the 2010 PolCore (optional): Project Crystal's era of PlayOnline,
//                                               kept for the launcher (it runs only 20110726_A for now)
//
// Revisions are named after their data set: Square Enix's product code on the patch servers and the data's
// version, XB2 for the Xbox 360 (XB2.0001), W20 for the PC (W20.0001), PS2 for the PS2 (PS2.0001). Installs made
// before that used rev/40160203_0 and rev/sep2010, and until 2026-11-04 rev/P20-<version> for the PS2; an install
// into such a folder renames them and the presets naming them (migrate_revisions).
//
// PolCore (PlayOnline 20110726_A, and 20100601_1 when asked) and the PlayOnline files changed after the disc can
// come from PlayOnline's update server. Copies are incremental: a file already there at the same size is skipped.

// The window's colours: white and black, with the logo's teal (logo.placeholder.png: 0, 123, 115) as the accent.
#define DG_PRESDL_COL_BG            { 255, 255, 255, 255 }
#define DG_PRESDL_COL_PANEL         { 255, 255, 255, 255 }
#define DG_PRESDL_COL_ACCENT        {   0, 123, 115, 255 }
#define DG_PRESDL_COL_ACCENT_HOV    {   0, 150, 140, 255 }
#define DG_PRESDL_COL_ON_ACCENT     { 255, 255, 255, 255 }
#define DG_PRESDL_COL_TEXT          {   0,   0,   0, 255 }
#define DG_PRESDL_COL_TEXT_DIM      {  90,  90,  90, 255 }
#define DG_PRESDL_COL_BORDER        { 200, 200, 200, 255 }
#define DG_PRESDL_COL_CHECK_ON      {   0, 123, 115, 255 }
#define DG_PRESDL_COL_HOVER         { 240, 245, 245, 255 }
#define DG_PRESDL_COL_FIELD         { 248, 248, 248, 255 }
#define DG_PRESDL_COL_DROPDOWN      { 248, 248, 248, 255 }
#define DG_PRESDL_COL_DROPDOWN_HOV  { 228, 242, 240, 255 }
#define DG_PRESDL_COL_SELECTED      { 210, 232, 229, 255 }
#define DG_PRESDL_COL_SECTION_TITLE { 242, 242, 242, 255 }
#define DG_PRESDL_COL_SCROLL_TRACK  { 235, 235, 235, 255 }
#define DG_PRESDL_COL_TAB_ACTIVE    {   0, 123, 115, 255 }
#define DG_PRESDL_COL_TAB_IDLE      { 245, 245, 245, 255 }
#define DG_PRESDL_COL_TAB_HOV       { 228, 228, 228, 255 }
#define DG_PRESDL_COL_TAB_MARK      {   0,  80,  75, 255 }
#define DG_PRESDL_COL_TAB_TEXT      { 255, 255, 255, 255 }
#include "dg_presdl.h"
#include "ftp.h"
#include "pack.h"
#include "polp.h"
#include "simple.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md5.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/statvfs.h>
#endif

#define PATH_MAX_ 1024
#define POLCORE_SIZE 892928                                     // the same for both builds below
#define POLCORE_2010 "20100601_1"
#define POLCORE_2010_DEST "pol/X2U-" POLCORE_2010 "/PolCore.xex"

// PolCore builds the installer downloads, each checked against its MD5 (PolCoreContent.xex in Square Enix's
// X2U-1000 tree; the same digests as the polarchive's manifests).
static const struct {
    const char *version, *md5;
} POLCORES[] = {
    {POLP_CAP, "410adcb506bf14e2135725854452df31"},
    {POLCORE_2010, "e7a49c27e2b7dbecab759b1eacf58709"},
};

#define REV_2016 "XB2-40160203_0"
#define REV_2010_DEFAULT "PS2-20100904_2"
// No server presets: the launcher makes a "local" one (127.0.0.1) and the player adds servers in its menu.
static const char LAUNCHER_INI_HEAD[] =
    "# 114360 launcher options. Add your servers in the menu (New server), with your PlayOnline ID and password.\n"
    "# Passwords are kept in this file as plain text.\n"
    "language  = english\nautostart = 0\n";
static const char AREA_DAT[] = "1 \r\n";
static const char SEP2010_VERSION[] = "20100904_2\r\n";
// CONFIG.SYS of the FFXI disc (byte for byte; the same on every 360 disc the launcher runs). PolCore reads
// BOOT:FFXi.dll from it. It sits in the disc's root, not in the installed data, so dumps of the hard-disk
// install (0001\) have none; the installer then writes this one.
static const char CONFIG_SYS[] =
    "ID:1\r\n"
    "INSTALL:INSTALL.xex\r\n"
    "BOOT:FFXi.dll\r\n"
    "TITLE:\r\n"
    "\tJP:\x83""t\x83""@\x83""C\x83""i\x83""\x8B""\x83""t\x83""@\x83""\x93""\x83""^\x83""W\x81""[XI\r\n"
    "\tEN_US:FINAL FANTASY XI\r\n"
    "\tEN_UK:FINAL FANTASY XI\r\n"
    "\tFR:FINAL FANTASY XI\r\n"
    "\tDE:FINAL FANTASY XI\r\n"
    "RULE:\r\n"
    "\tJP:LICENSE_JP.TXT\r\n"
    "\tEN_US:LICENSE_EN_US.TXT\r\n"
    "\tEN_UK:LICENSE_EN_UK.TXT\r\n"
    "\tFR:LICENSE_FR.TXT\r\n"
    "\tDE:LICENSE_DE.TXT";

// ─── Plan ──────────────────────────────────────────────────────────────────────────────────────────────────────

enum { JOB_FILE, JOB_ISO, JOB_BYTES, JOB_POLCORE, JOB_SKIP };

typedef struct job {
    char *dest;            // relative, '/' separated
    int kind;
    char *src;             // JOB_FILE / JOB_ISO path; JOB_BYTES data (not owned)
    uint64_t offset, size;
} job;

#define MAX_MESSAGES 24
typedef struct plan {
    job *jobs;
    size_t count, cap;
    char messages[MAX_MESSAGES][320];
    int codes[MAX_MESSAGES];   // 0, or the ZZ error code of that message (docs/installer-simple.md)
    int message_count;
    bool ok, update_pol;
    int skipped_unsafe, skipped_logs;
    void *owned[16];       // JOB_BYTES buffers made by pool_plan (files.tbl, pool.txt)
    int owned_count;
    char rev2010[64];      // the 2010 data's revision ("PS2-20100904_2"), "" when none
    // The game data the revisions hold (ROM*, sound*, FTABLE/VTABLE): as plain folders (each revision its own
    // copy) and pooled (each file once, estimated by path and size; pool_plan then merges by content too).
    uint64_t data_plain, data_pooled;
} plan;

// ─── Log ───────────────────────────────────────────────────────────────────────────────────────────────────────
//
// file-114360-installer.log next to the installer (else in the user's preference folder): every check message,
// every file written or skipped, every download and error, with the time since start. Sent with bug reports.

static SDL_Mutex *log_lock;
static FILE *log_file;
static char log_path[PATH_MAX_];

// The last lines also stay in memory for the window's live log (Log tab, or the Install tab in a Simple install).
#define LOG_LIVE 30
static char log_live[LOG_LIVE][256];
static int log_live_next, log_live_count;
static uint32_t log_live_seq;          // bumped per line: the window rebuilds its text only when it changed

static void ilog(const char *fmt, ...)
{
    char line[1024];
    uint64_t ms = SDL_GetTicks();
    int n = snprintf(line, sizeof(line), "%5llu.%03llu  ", (unsigned long long)(ms / 1000),
                     (unsigned long long)(ms % 1000));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
    va_end(ap);
    SDL_LockMutex(log_lock);
    if (log_file) {
        fputs(line, log_file);
        fputc('\n', log_file);
        fflush(log_file);
    }
    SDL_strlcpy(log_live[log_live_next], line, sizeof(log_live[0]));
    log_live_next = (log_live_next + 1) % LOG_LIVE;
    log_live_count += log_live_count < LOG_LIVE;
    log_live_seq++;
    SDL_UnlockMutex(log_lock);
}

// The live log lines, oldest first, one per line; returns false when nothing was logged since `*seq`.
static bool log_live_text(char *out, size_t size, uint32_t *seq)
{
    SDL_LockMutex(log_lock);
    bool changed = *seq != log_live_seq;
    if (changed) {
        *seq = log_live_seq;
        size_t len = 0;
        out[0] = 0;
        for (int i = 0; i < log_live_count && len + 1 < size; i++) {
            const char *l = log_live[(log_live_next - log_live_count + i + LOG_LIVE) % LOG_LIVE];
            len += (size_t)snprintf(out + len, size - len, "%s%s", i ? "\n" : "", l);
        }
    }
    SDL_UnlockMutex(log_lock);
    return changed;
}

static void log_open(const char *base)
{
    log_lock = SDL_CreateMutex();
    snprintf(log_path, sizeof(log_path), "%sfile-114360-installer.log", base ? base : "");
    log_file = fopen(log_path, "a");
    if (!log_file) {
        char *pref = SDL_GetPrefPath("CrinaleSystems", "114360");
        snprintf(log_path, sizeof(log_path), "%sfile-114360-installer.log", pref ? pref : "");
        SDL_free(pref);
        log_file = fopen(log_path, "a");
    }
    ilog("---- 114360 installer started (%s) ----", SDL_GetPlatform());
}

// A check message. code: 0, or the ZZ code of what is wrong (an error: the install cannot start).
static void say(plan *p, int code, const char *fmt, ...)
{
    if (code) {
        p->ok = false;
    }
    char text[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (code) {
        ilog("check: ZZ%03d %s", code, text);
    } else {
        ilog("check: ok %s", text);
    }
    if (p->message_count == MAX_MESSAGES) {
        return;
    }
    SDL_strlcpy(p->messages[p->message_count], text, sizeof(p->messages[0]));
    p->codes[p->message_count++] = code;
}

// The game's own error logs (rev/<revision>/0001/SYS/errNNN.bin) are never copied: a data folder that was played
// from already holds some, and the game writes its own.
//
// Files the console cannot hold are left out on every target, so that any install can be copied to the console
// as it is (Neighborhood refuses empty files, FATX takes names of at most 42 characters; installs that kept them
// failed when copied over with Neighborhood):
//   - empty files: the empty voice-chat help (data/doc/vchat_toolhelp_win.txt) and the PlayOnline browser's
//     empty cache index (usr/all/url/cache/dcfat0.bin; none of PolCore, PolPatch or the PC pol.exe names it);
//   - files with any path part longer than 42 characters: four PlayOnline Viewer menu tracks such as
//     "Gin_no_kaichudokei_(by_Noriko_Matsueda).104.scd". The disc's XDVDFS allows the long names. Only the Viewer
//     plays them, and the launcher starts FFXI without it.
// The PlayOnline files from the update server take the same rule (playonline_rel).
// Advanced options: keep the empty files and/or the long names anyway (they break Neighborhood and FATX; useful
// only for Xenia or a folder that never goes to a console). Set from the choices before every check and install.
static bool keep_empty_files, keep_long_names;

static bool console_unsafe(const char *path, uint64_t size)
{
    if (size == 0 && !keep_empty_files) {
        return true;
    }
    for (const char *part = path; *part && !keep_long_names;) {
        size_t n = strcspn(part, "/\\");
        if (n > 42) {
            return true;
        }
        part += n + (part[n] != 0);
    }
    return false;
}

static void add(plan *p, const char *dest, int kind, const char *src, uint64_t offset, uint64_t size)
{
    const char *name = strrchr(dest, '/');
    name = name ? name + 1 : dest;
    if (kind != JOB_POLCORE) {
        const char *sys = strstr(dest, "/0001/SYS/");
        if ((sys && name == sys + 10 && SDL_strncasecmp(name, "err", 3) == 0) || strstr(dest, "/0001/USER/")
            || strstr(dest, "/0001/TEMP/")) {
            p->skipped_logs++;     // written by the game as it runs: error logs, per-character settings, logs
            return;
        }
        if (kind != JOB_BYTES && console_unsafe(dest, size)) {
            p->skipped_unsafe++;
            return;
        }
    }
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 1024;
        p->jobs = realloc(p->jobs, sizeof(job) * p->cap);
    }
    p->jobs[p->count++] = (job){SDL_strdup(dest), kind,
                                kind == JOB_BYTES ? (char *)src : (src ? SDL_strdup(src) : nullptr), offset, size};
}

static void plan_free(plan *p)
{
    for (size_t i = 0; i < p->count; i++) {
        SDL_free(p->jobs[i].dest);
        if (p->jobs[i].kind != JOB_BYTES) {
            SDL_free(p->jobs[i].src);
        }
    }
    free(p->jobs);
    for (int i = 0; i < p->owned_count; i++) {
        free(p->owned[i]);
    }
    *p = (plan){0};
}

// ─── Files ─────────────────────────────────────────────────────────────────────────────────────────────────────

static bool path_info(const char *path, SDL_PathInfo *info)
{
    return path && path[0] && SDL_GetPathInfo(path, info);
}

static bool is_dir(const char *path)
{
    SDL_PathInfo i;
    return path_info(path, &i) && i.type == SDL_PATHTYPE_DIRECTORY;
}

static bool is_file(const char *path, uint64_t *size)
{
    SDL_PathInfo i;
    if (!path_info(path, &i) || i.type != SDL_PATHTYPE_FILE) {
        return false;
    }
    if (size) {
        *size = i.size;
    }
    return true;
}

static void join(char *out, const char *a, const char *b)
{
    size_t n = strlen(a);
    snprintf(out, PATH_MAX_, "%s%s%s", a, n && (a[n - 1] == '/' || a[n - 1] == '\\') ? "" : "/", b);
}

typedef struct {
    const char *want;
    char *found;
} find_ctx;

static SDL_EnumerationResult find_cb(void *ud, const char *dir, const char *name)
{
    find_ctx *f = ud;
    if (SDL_strcasecmp(name, f->want) == 0) {
        join(f->found, dir, name);
        return SDL_ENUM_SUCCESS;
    }
    return SDL_ENUM_CONTINUE;
}

// `name` in `dir`, any letter case (discs and HDD dumps differ), into out; false if absent.
static bool find_ci(const char *dir, const char *name, char *out)
{
    out[0] = 0;
    if (!is_dir(dir)) {
        return false;
    }
    find_ctx f = {name, out};
    SDL_EnumerateDirectory(dir, find_cb, &f);
    return out[0] != 0;
}

// The folder holding FFXiMain.dll and ROM/: root/0001 or root.
static bool data_folder(const char *root, char *out)
{
    char sub[PATH_MAX_], probe[PATH_MAX_];
    if (find_ci(root, "0001", sub) && (find_ci(sub, "FFXiMain.dll", probe) || find_ci(sub, "ROM", probe))) {
        SDL_strlcpy(out, sub, PATH_MAX_);
        return true;
    }
    if (find_ci(root, "FFXiMain.dll", probe) || find_ci(root, "ROM", probe)) {
        SDL_strlcpy(out, root, PATH_MAX_);
        return true;
    }
    return false;
}

static bool find_polcore(const char *root, char *out)
{
    char parent[PATH_MAX_];
    SDL_strlcpy(parent, root, sizeof(parent));
    size_t n = strlen(parent);
    while (n > 1 && (parent[n - 1] == '/' || parent[n - 1] == '\\')) {
        parent[--n] = 0;
    }
    char *slash = SDL_strrchr(parent, '/');
#ifdef _WIN32
    char *bslash = SDL_strrchr(parent, '\\');
    if (bslash > slash) {
        slash = bslash;
    }
#endif
    if (slash) {
        *slash = 0;
    }
    const char *dirs[2] = {root, slash ? parent : nullptr};
    for (int d = 0; d < 2; d++) {
        if (dirs[d] && (find_ci(dirs[d], "PolCore.xex", out) || find_ci(dirs[d], "PolCoreContent.xex", out))) {
            return true;
        }
    }
    return false;
}

typedef struct {
    plan *p;
    const char *prefix;   // destination prefix
    const char *top;      // walk root
    bool skip_xex;
} tree_ctx;

static SDL_EnumerationResult tree_cb(void *ud, const char *dir, const char *name)
{
    tree_ctx *t = ud;
    char path[PATH_MAX_], rel[PATH_MAX_], dest[PATH_MAX_];
    uint64_t size;
    join(path, dir, name);
    if (is_dir(path)) {
        SDL_EnumerateDirectory(path, tree_cb, t);
        return SDL_ENUM_CONTINUE;
    }
    if (!is_file(path, &size)) {
        return SDL_ENUM_CONTINUE;
    }
    size_t n = strlen(name);
    if (t->skip_xex && n > 4 && SDL_strcasecmp(name + n - 4, ".xex") == 0) {
        return SDL_ENUM_CONTINUE;
    }
    SDL_strlcpy(rel, path + strlen(t->top), sizeof(rel));
    for (char *c = rel; *c; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
    snprintf(dest, sizeof(dest), "%s/%s", t->prefix, rel[0] == '/' ? rel + 1 : rel);
    add(t->p, dest, JOB_FILE, path, 0, size);
    return SDL_ENUM_CONTINUE;
}

static void add_tree(plan *p, const char *prefix, const char *top, bool skip_xex)
{
    tree_ctx t = {p, prefix, top, skip_xex};
    SDL_EnumerateDirectory(top, tree_cb, &t);
}

// ─── Builds the launcher runs (launcher/src/engine/engine.c), by XEX time stamp ─────────────────────────────────

static uint32_t xex_timestamp(const char *path)
{
    unsigned char h[0x8000];
    SDL_IOStream *f = SDL_IOFromFile(path, "rb");
    if (!f) {
        return 0;
    }
    size_t n = SDL_ReadIO(f, h, sizeof(h));
    SDL_CloseIO(f);
    if (n < 0x18 || memcmp(h, "XEX2", 4) != 0) {
        return 0;
    }
    uint32_t count = SDL_Swap32BE(*(uint32_t *)(h + 0x14));
    for (uint32_t i = 0; i < count && i < 64 && 0x20 + i * 8 <= n; i++) {
        uint32_t key = SDL_Swap32BE(*(uint32_t *)(h + 0x18 + i * 8)), value = SDL_Swap32BE(*(uint32_t *)(h + 0x1C + i * 8));
        if (key == 0x00018002 && value + 8 <= n) {
            return SDL_Swap32BE(*(uint32_t *)(h + value + 4));
        }
    }
    return 0;
}

static void check_build(plan *p, const char *what, uint32_t want, const char *want_name, const char *path)
{
    uint32_t stamp = xex_timestamp(path);
    int code = SDL_strncasecmp(what, "PolCore", 7) == 0 ? 405 : 402;
    if (stamp == want) {
        say(p, 0, "%s: %s", what, want_name);
    } else if (!stamp) {
        say(p, code, "%s: %s is not an Xbox 360 executable", what, path);
    } else {
        say(p, code, "%s: build %08X; the launcher runs only %s", what, stamp, want_name);
    }
}

static const struct {
    const char *name;
    uint32_t stamp;
    const char *label;
} PROGRAM[] = {{"FFXi.dll", 0x554CA1CB, "FFXi.dll 2016"}, {"FFXiMain.dll", 0x56B0E65D, "FFXiMain 40160203_0"}};

// CONFIG.SYS: the data folder's own when it has one, else the disc's (CONFIG_SYS).
static void add_config_sys(plan *p, const char *rev, const char *data)
{
    char path[PATH_MAX_], dest[PATH_MAX_];
    if (!find_ci(data, "CONFIG.SYS", path)) {
        snprintf(dest, sizeof(dest), "rev/%s/0001/CONFIG.SYS", rev);
        add(p, dest, JOB_BYTES, CONFIG_SYS, 0, sizeof(CONFIG_SYS) - 1);
        say(p, false, "%s: no CONFIG.SYS in the data; the disc's is written", rev);
    }
}

// ─── Xbox 360 disc images (XDVDFS) ─────────────────────────────────────────────────────────────────────────────

static bool iso_walk(SDL_IOStream *f, uint64_t base, uint32_t sector, uint32_t size, const char *prefix, plan *p,
                     const char *iso, int *count)
{
    unsigned char *d = malloc(size);
    if (SDL_SeekIO(f, (Sint64)(base + (uint64_t)sector * 2048), SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(f, d, size) != size) {
        free(d);
        return false;
    }
    uint32_t stack[512];
    int top = 0;
    stack[top++] = 0;
    while (top) {
        uint32_t off = stack[--top] * 4;
        if (off + 14 > size) {
            continue;
        }
        uint16_t left = d[off] | d[off + 1] << 8, right = d[off + 2] | d[off + 3] << 8;
        uint32_t start = SDL_Swap32LE(*(uint32_t *)(d + off + 4)), length = SDL_Swap32LE(*(uint32_t *)(d + off + 8));
        uint8_t attr = d[off + 12], nlen = d[off + 13];
        if (left == 0xFFFF || off + 14 + nlen > size) {
            continue;
        }
        char name[256], path[PATH_MAX_];
        memcpy(name, d + off + 14, nlen);
        name[nlen] = 0;
        snprintf(path, sizeof(path), "%s%s%s", prefix, prefix[0] ? "/" : "", name);
        if (left && top < 511) {
            stack[top++] = left;
        }
        if (right && top < 511) {
            stack[top++] = right;
        }
        if (attr & 0x10) {
            if (length && !iso_walk(f, base, start, length, path, p, iso, count)) {
                free(d);
                return false;
            }
        } else if (SDL_strncasecmp(path, "PlayOnline/", 11) == 0) {
            size_t n = strlen(path);
            char dest[PATH_MAX_];
            uint64_t at = base + (uint64_t)start * 2048;
            if (n > 4 && SDL_strcasecmp(path + n - 4, ".xex") == 0) {
                continue;
            }
            snprintf(dest, sizeof(dest), "PlayOnline/%s", path + 11);
            add(p, dest, JOB_ISO, iso, at, length);
            if (SDL_strcasecmp(path + 11, "area.dat") == 0) {
                add(p, "area.dat", JOB_ISO, iso, at, length);
            }
            (*count)++;
        }
    }
    free(d);
    return true;
}

static int iso_playonline(plan *p, const char *iso)
{
    static const uint64_t bases[] = {0x0, 0xFD90000, 0x2080000, 0x18300000};
    SDL_IOStream *f = SDL_IOFromFile(iso, "rb");
    unsigned char sec[2048];
    int count = 0;
    if (!f) {
        say(p, 404, "disc image: cannot open %s", iso);
        return -1;
    }
    for (int b = 0; b < 4; b++) {
        if (SDL_SeekIO(f, (Sint64)(bases[b] + 0x10000), SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(f, sec, 2048) == 2048
            && memcmp(sec, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
            uint32_t root = SDL_Swap32LE(*(uint32_t *)(sec + 20)), size = SDL_Swap32LE(*(uint32_t *)(sec + 24));
            if (!iso_walk(f, bases[b], root, size, "", p, iso, &count)) {
                say(p, 404, "disc image: truncated");
            }
            SDL_CloseIO(f);
            return count;
        }
    }
    SDL_CloseIO(f);
    say(p, 404, "disc image: not an Xbox 360 disc image");
    return -1;
}

// ─── The plan ──────────────────────────────────────────────────────────────────────────────────────────────────

// Where the install goes: a folder (also the folder Neighborhood copies from) or the console over FTP. Every
// target leaves out the files the console cannot hold (console_unsafe).
enum { TARGET_FOLDER, TARGET_NEIGHBORHOOD, TARGET_FTP };

typedef struct choices {
    char dest[PATH_MAX_], root2016[PATH_MAX_], root2010[PATH_MAX_], iso[PATH_MAX_], pol_folder[PATH_MAX_],
        polcore[PATH_MAX_], area[PATH_MAX_];
    char work[PATH_MAX_];        // where downloads, staging and generated packs go (Game files > Work location)
    char root_ps2[PATH_MAX_];    // the 20100904_2 PS2 patch set (ROM*/, FTABLE/VTABLE), converted for 2010
    char root_uc2010[PATH_MAX_], root_pc2011[PATH_MAX_];   // Advanced: extra 2010 sources
    char root2026[PATH_MAX_];    // FFXI-360 compatible legacy DATs (2026; not finished)
    uint32_t target;
    char ftp_host[256], ftp_user[64], ftp_password[64], ftp_dir[PATH_MAX_];   // TARGET_FTP; host may be ip:port
    bool download_polcore, download_polcore2010, update_pol, share;
    bool keep_empty, keep_long;  // Advanced: keep the files the console cannot hold (console_unsafe)
    bool use_uc2010, use_pc2011;
} choices;

static char payload_xex[PATH_MAX_];

// Free bytes on the disk holding `path`. A folder the install has not made yet counts as the nearest folder
// above it that exists (the disk it will be made on); a bare name counts as the current folder.
static uint64_t free_space(const char *path)
{
    char at[PATH_MAX_];
    SDL_strlcpy(at, path[0] ? path : ".", sizeof(at));
    for (;;) {
#ifdef _WIN32
        ULARGE_INTEGER avail;
        if (GetDiskFreeSpaceExA(at, &avail, nullptr, nullptr)) {
            return avail.QuadPart;
        }
#else
        struct statvfs s;
        if (statvfs(at, &s) == 0) {
            return (uint64_t)s.f_bavail * s.f_frsize;
        }
#endif
        size_t len = strlen(at);
        char *cut = strrchr(at, '/'), *back = strrchr(at, '\\');
        if (back && (!cut || back > cut)) {
            cut = back;
        }
        if (!cut) {
            SDL_strlcpy(at, ".", sizeof(at));
        } else if (cut == at || (cut == at + 2 && at[1] == ':')) {
            cut[1] = 0;                    // up to the root: "/" or "C:\"
        } else {
            *cut = 0;
        }
        if (strlen(at) >= len) {           // nothing above it exists
            return 0;
        }
    }
}

// ─── Destination ───────────────────────────────────────────────────────────────────────────────────────────────
//
// Every write, size check, rename and removal in the install goes through dest_*, with paths relative to the
// install root ('/' separated): a local folder (TARGET_FOLDER, TARGET_NEIGHBORHOOD), or a folder on a console's
// FTP server (TARGET_FTP). make_plan and install_thread open it and close it again; only one runs at a time.
// Over FTP each folder is listed once and the listing kept current as files are written and removed, so the
// size check that makes installs incremental costs one LIST per folder, not a command per file. A transfer that
// fails is retried on a new connection (the console's server drops one now and then).

static bool make_parent(const char *path);
static bool write_all(const char *out, const void *data, size_t len);
static bool copy_range(const char *src, uint64_t offset, uint64_t size, const char *out);
static bool remove_tree(const char *path);

#define DEST_BUCKETS 4096
#define FTP_TRIES 3

typedef struct listing {
    char *folder;              // remote, absolute
    ftp_entry *entries;
    int count, cap;
    struct listing *next;
} listing;

static struct {
    bool remote;
    char root[PATH_MAX_];      // the local folder, or the remote one ("/Hdd1/114360")
    char where[PATH_MAX_];     // for messages: the folder, or ftp://host/folder
    ftp *ftp;
    listing *buckets[DEST_BUCKETS];
    char error[512];
} dest;

static unsigned bucket_of(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (unsigned char)*s) * 16777619u;
    }
    return h % DEST_BUCKETS;
}

static void dest_path(char *out, const char *rel)
{
    if (dest.remote) {
        snprintf(out, PATH_MAX_, "%s/%s", dest.root, rel);
    } else {
        join(out, dest.root, rel);
    }
}

static void forget_listings(void)
{
    for (int b = 0; b < DEST_BUCKETS; b++) {
        for (listing *l = dest.buckets[b], *next; l; l = next) {
            next = l->next;
            ftp_entries_free(l->entries, l->count);
            free(l->folder);
            free(l);
        }
        dest.buckets[b] = nullptr;
    }
}

// Open the destination `c` names. False with dest.error filled.
static bool dest_open(const choices *c)
{
    dest.error[0] = 0;
    dest.remote = c->target == TARGET_FTP;
    if (!dest.remote) {
        SDL_strlcpy(dest.root, c->dest, sizeof(dest.root));
        SDL_strlcpy(dest.where, c->dest, sizeof(dest.where));
        return c->dest[0] != 0;
    }
    char host[256];
    int port = 21;
    SDL_strlcpy(host, c->ftp_host, sizeof(host));
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = 0;
        port = atoi(colon + 1);
    }
    SDL_strlcpy(dest.root, c->ftp_dir, sizeof(dest.root));
    for (size_t n = strlen(dest.root); n > 1 && dest.root[n - 1] == '/'; n--) {
        dest.root[n - 1] = 0;
    }
    snprintf(dest.where, sizeof(dest.where), "ftp://%s%s", c->ftp_host, dest.root);
    if (!host[0] || dest.root[0] != '/') {
        snprintf(dest.error, sizeof(dest.error), "FTP: give the console's address and a folder such as /Hdd1/114360");
        return false;
    }
    dest.ftp = ftp_open(host, port, c->ftp_user[0] ? c->ftp_user : "xboxftp",
                        c->ftp_password[0] ? c->ftp_password : "xboxftp", dest.error, sizeof(dest.error));
    return dest.ftp != nullptr;
}

static void dest_close(void)
{
    forget_listings();
    ftp_close(dest.ftp);
    dest.ftp = nullptr;
}

// One retry on a fresh connection after a connection failure.
static bool ftp_again(void)
{
    return ftp_reopen(dest.ftp, dest.error, sizeof(dest.error));
}

// The cached listing of remote `folder` (listed now if needed), or nullptr when the connection failed.
static listing *listing_of(const char *folder)
{
    unsigned b = bucket_of(folder);
    for (listing *l = dest.buckets[b]; l; l = l->next) {
        if (strcmp(l->folder, folder) == 0) {
            return l;
        }
    }
    ftp_entry *entries = nullptr;
    int n = -1;
    for (int t = 0; t < FTP_TRIES && n < 0; t++) {
        n = ftp_list(dest.ftp, folder, &entries, dest.error, sizeof(dest.error));
        if (n < 0 && !ftp_again()) {
            break;
        }
    }
    if (n < 0) {
        return nullptr;
    }
    listing *l = calloc(1, sizeof(*l));
    *l = (listing){strdup(folder), entries, n, n, dest.buckets[b]};
    dest.buckets[b] = l;
    return l;
}

static void split_remote(const char *path, char *folder, const char **name)
{
    SDL_strlcpy(folder, path, PATH_MAX_);
    char *slash = strrchr(folder, '/');
    *slash = 0;
    if (!folder[0]) {
        strcpy(folder, "/");
    }
    *name = path + (slash - folder) + 1;
}

static ftp_entry *remote_entry(const char *path, listing **in)
{
    char folder[PATH_MAX_];
    const char *name;
    split_remote(path, folder, &name);
    listing *l = listing_of(folder);
    if (in) {
        *in = l;
    }
    for (int i = 0; l && i < l->count; i++) {
        if (strcmp(l->entries[i].name, name) == 0) {
            return &l->entries[i];
        }
    }
    return nullptr;
}

// Record a file just written (or a folder just made) in its folder's listing.
static void remember(const char *path, uint64_t size, bool dir)
{
    listing *l;
    ftp_entry *e = remote_entry(path, &l);
    if (e) {
        e->size = size;
        e->dir = dir;
        return;
    }
    if (!l) {
        return;
    }
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->entries = realloc(l->entries, sizeof(ftp_entry) * (size_t)l->cap);
    }
    l->entries[l->count++] = (ftp_entry){strdup(strrchr(path, '/') + 1), size, dir};
}

static void forget(const char *path)
{
    listing *l;
    ftp_entry *e = remote_entry(path, &l);
    if (e) {
        free(e->name);
        *e = l->entries[--l->count];
    }
}

// Is `rel` a file, and how big.
static bool dest_size(const char *rel, uint64_t *size)
{
    char path[PATH_MAX_];
    dest_path(path, rel);
    if (!dest.remote) {
        return is_file(path, size);
    }
    ftp_entry *e = remote_entry(path, nullptr);
    if (e && !e->dir && size) {
        *size = e->size;
    }
    return e && !e->dir;
}

static bool dest_same_size(const char *rel, uint64_t size)
{
    uint64_t have;
    return dest_size(rel, &have) && have == size;
}

static bool dest_is_dir(const char *rel)
{
    char path[PATH_MAX_];
    dest_path(path, rel);
    if (!dest.remote) {
        return is_dir(path);
    }
    ftp_entry *e = remote_entry(path, nullptr);
    return e && e->dir;
}

// Make the remote folder `path` and every folder above it that is missing, the install root included. A drive
// root such as /Hdd1 cannot be made; the console refuses MKD there, which ftp_mkdir takes as "already there".
static bool remote_mkdirs(const char *path)
{
    if (!path[0] || strcmp(path, "/") == 0) {
        return true;
    }
    char parent[PATH_MAX_];
    const char *name;
    split_remote(path, parent, &name);
    ftp_entry *e = remote_entry(path, nullptr);
    if (e && e->dir) {
        return true;
    }
    if (!remote_mkdirs(parent)) {
        return false;
    }
    for (int t = 0; t < FTP_TRIES; t++) {
        if (ftp_mkdir(dest.ftp, path, dest.error, sizeof(dest.error))) {
            remember(path, 0, true);
            return true;
        }
        if (!ftp_again()) {
            break;
        }
    }
    return false;
}

static bool dest_mkdirs(const char *rel)
{
    char path[PATH_MAX_];
    dest_path(path, rel);
    if (!dest.remote) {
        char probe[PATH_MAX_];
        snprintf(probe, sizeof(probe), "%s/x", path);
        return make_parent(probe);
    }
    return remote_mkdirs(path);
}

typedef struct source {
    SDL_IOStream *file;       // a file (from `offset`), or
    const unsigned char *data;
    uint64_t offset, pos;
} source;

static bool read_source(void *ud, void *buf, size_t want)
{
    source *s = ud;
    if (s->data) {
        memcpy(buf, s->data + s->pos, want);
        s->pos += want;
        return true;
    }
    return SDL_ReadIO(s->file, buf, want) == want;
}

// Store `size` bytes from `data`, or from local file `src` at `offset`, at `rel`.
static bool remote_store(const char *rel, const void *data, const char *src, uint64_t offset, uint64_t size)
{
    char path[PATH_MAX_], folder[PATH_MAX_];
    const char *name;
    dest_path(path, rel);
    split_remote(path, folder, &name);
    if (!remote_mkdirs(folder)) {
        return false;
    }
    for (int t = 0; t < FTP_TRIES; t++) {
        source s = {nullptr, data, offset, 0};
        if (src) {
            s.file = SDL_IOFromFile(src, "rb");
            if (!s.file || SDL_SeekIO(s.file, (Sint64)offset, SDL_IO_SEEK_SET) < 0) {
                if (s.file) {
                    SDL_CloseIO(s.file);
                }
                snprintf(dest.error, sizeof(dest.error), "cannot read %s", src);
                return false;
            }
        }
        bool ok = ftp_store(dest.ftp, path, size, read_source, &s, dest.error, sizeof(dest.error));
        if (s.file) {
            SDL_CloseIO(s.file);
        }
        if (ok) {
            remember(path, size, false);
            return true;
        }
        if (!ftp_again()) {
            break;
        }
    }
    return false;
}

static bool dest_write(const char *rel, const void *data, size_t len)
{
    if (dest.remote) {
        return remote_store(rel, data, nullptr, 0, len);
    }
    char path[PATH_MAX_];
    dest_path(path, rel);
    return make_parent(path) && write_all(path, data, len);
}

static bool dest_copy(const char *rel, const char *src, uint64_t offset, uint64_t size)
{
    if (dest.remote) {
        return remote_store(rel, nullptr, src, offset, size);
    }
    char path[PATH_MAX_];
    dest_path(path, rel);
    return make_parent(path) && copy_range(src, offset, size, path);
}

// A small file of the install (launcher.ini), malloc'd and NUL-terminated, or nullptr when there is none.
static char *dest_read(const char *rel, size_t *len)
{
    char path[PATH_MAX_];
    dest_path(path, rel);
    if (!dest.remote) {
        void *data = SDL_LoadFile(path, len);
        char *copy = data ? malloc(*len + 1) : nullptr;
        if (copy) {
            memcpy(copy, data, *len);
            copy[*len] = 0;
        }
        SDL_free(data);
        return copy;
    }
    if (!dest_size(rel, nullptr)) {
        return nullptr;
    }
    bool missing = false;
    unsigned char *data = ftp_fetch(dest.ftp, path, len, &missing, dest.error, sizeof(dest.error));
    return (char *)data;
}

static bool dest_rename(const char *from_rel, const char *to_rel)
{
    char from[PATH_MAX_], to[PATH_MAX_];
    dest_path(from, from_rel);
    dest_path(to, to_rel);
    if (!dest.remote) {
        return SDL_RenamePath(from, to);
    }
    bool ok = ftp_rename(dest.ftp, from, to, dest.error, sizeof(dest.error));
    forget_listings();          // both folders and everything below the renamed one
    return ok;
}

static bool remote_remove_tree(const char *path, bool dir)
{
    if (!dir) {
        bool ok = ftp_delete(dest.ftp, path, dest.error, sizeof(dest.error));
        if (ok) {
            forget(path);
        }
        return ok;
    }
    listing *l = listing_of(path);
    if (!l) {
        return false;
    }
    bool ok = true;
    char child[PATH_MAX_];
    while (ok && l->count) {
        // Each removal forgets its entry in this listing, so the count goes down by one every time.
        int before = l->count;
        ftp_entry *e = &l->entries[l->count - 1];
        snprintf(child, sizeof(child), "%s/%s", path, e->name);
        ok = remote_remove_tree(child, e->dir) && l->count < before;
    }
    if (ok && ftp_rmdir(dest.ftp, path, dest.error, sizeof(dest.error))) {
        forget(path);
        return true;
    }
    return false;
}

// Delete a file of the install, or a folder and everything in it.
static bool dest_remove(const char *rel)
{
    char path[PATH_MAX_];
    dest_path(path, rel);
    if (!dest.remote) {
        return remove_tree(path);
    }
    ftp_entry *e = remote_entry(path, nullptr);
    return e && remote_remove_tree(path, e->dir);
}

// The entries of folder `rel`: `each` gets every name and whether it is a folder.
static void dest_list(const char *rel, void (*each)(void *ud, const char *name, bool dir), void *ud)
{
    char path[PATH_MAX_];
    dest_path(path, rel);
    if (!dest.remote) {
        char **names = SDL_GlobDirectory(path, nullptr, 0, nullptr);
        for (int i = 0; names && names[i]; i++) {
            char full[PATH_MAX_];
            join(full, path, names[i]);
            each(ud, names[i], is_dir(full));
        }
        SDL_free(names);
        return;
    }
    listing *l = listing_of(path);
    // A copy: `each` may remove entries from this very listing.
    int n = l ? l->count : 0;
    ftp_entry *copy = n ? malloc(sizeof(ftp_entry) * (size_t)n) : nullptr;
    for (int i = 0; i < n; i++) {
        copy[i] = (ftp_entry){strdup(l->entries[i].name), l->entries[i].size, l->entries[i].dir};
    }
    for (int i = 0; i < n; i++) {
        each(ud, copy[i].name, copy[i].dir);
    }
    ftp_entries_free(copy, n);
}

#define POOL_KEY 40

// "rev/<revision>/0001/<inner>" with a pooled <inner>: the revision name's length and the inner path, else 0.
static size_t pooled_dest(const char *dest, const char **inner)
{
    if (strncmp(dest, "rev/", 4) != 0) {
        return 0;
    }
    const char *slash = strchr(dest + 4, '/');
    if (!slash || strncmp(slash, "/0001/", 6) != 0) {
        return 0;
    }
    const char *in = slash + 6, *end = strchr(in, '/');
    size_t top = end ? (size_t)(end - in) : strlen(in);
    bool data = SDL_strncasecmp(in, "rom", 3) == 0 || SDL_strncasecmp(in, "sound", 5) == 0
                || (!end && (SDL_strcasecmp(in, "ftable.dat") == 0 || SDL_strcasecmp(in, "vtable.dat") == 0));
    if (!data || top == 0 || strlen(in) >= POOL_KEY) {
        return 0;
    }
    *inner = in;
    return (size_t)(slash - (dest + 4));
}

// Bytes the shared pool will take: each pooled path and size counted once across revisions (most files are the same
// at the same path in every revision; files that moved are counted again, so this errs on the high side).
typedef struct seen_file {
    char key[POOL_KEY];
    uint64_t size;
} seen_file;

static int by_seen(const void *a, const void *b)
{
    const seen_file *x = a, *y = b;
    int c = memcmp(x->key, y->key, POOL_KEY);
    return c ? c : (x->size < y->size ? -1 : x->size > y->size);
}

static uint64_t shared_estimate(const plan *p)
{
    seen_file *seen = calloc(p->count ? p->count : 1, sizeof(seen_file));
    size_t n = 0;
    uint64_t total = 0;
    for (size_t i = 0; seen && i < p->count; i++) {
        const char *inner;
        if (pooled_dest(p->jobs[i].dest, &inner)) {
            for (size_t c = 0; inner[c]; c++) {
                seen[n].key[c] = (char)SDL_tolower((unsigned char)inner[c]);
            }
            seen[n++].size = p->jobs[i].size;
        }
    }
    if (seen) {
        qsort(seen, n, sizeof(seen_file), by_seen);
        for (size_t i = 0; i < n; i++) {
            if (i == 0 || by_seen(&seen[i], &seen[i - 1]) != 0) {
                total += seen[i].size;
            }
        }
    }
    free(seen);
    return total;
}

static void make_plan(plan *p, const choices *c)
{
    char data2016[PATH_MAX_] = "", data2010[PATH_MAX_], path[PATH_MAX_], polcore[PATH_MAX_] = "";
    uint64_t size;
    *p = (plan){.ok = true, .update_pol = c->update_pol};
    bool opened = dest_open(c);
    if (!opened) {
        say(p, 305, "%s", dest.error[0] ? dest.error : "no install location");
    } else if (dest.remote) {
        say(p, false, "FTP: signed in to %s", dest.where);
    }
    if (!data_folder(c->root2016, data2016)) {
        say(p, 401, "2016: no FFXI data found (a folder with 0001\\ or FFXiMain.dll and ROM\\)");
        data2016[0] = 0;
    } else {
        for (int i = 0; i < 2; i++) {
            if (!find_ci(data2016, PROGRAM[i].name, path)) {
                say(p, 401, "2016: %s missing in %s", PROGRAM[i].name, data2016);
            } else if (PROGRAM[i].stamp) {
                check_build(p, PROGRAM[i].name, PROGRAM[i].stamp, PROGRAM[i].label, path);
            }
        }
        add_tree(p, "rev/" REV_2016 "/0001", data2016, false);
        add_config_sys(p, REV_2016, data2016);
    }

    bool download = false;
    if (c->polcore[0]) {
        SDL_strlcpy(polcore, c->polcore, sizeof(polcore));
    } else if (c->download_polcore) {
        download = true;
    } else {
        find_polcore(c->root2016, polcore);
    }
    if (download) {
        say(p, false, "PolCore: PlayOnline 20110726_A, downloaded from PlayOnline's update server at install");
        add(p, "rev/" REV_2016 "/PolCore.xex", JOB_POLCORE, POLP_CAP, 0, POLCORE_SIZE);
    } else if (!polcore[0] || !is_file(polcore, &size)) {
        say(p, 405, "PolCore: not found; choose PolCore.xex or let it download");
    } else {
        check_build(p, "PolCore", 0x4E240911, "PlayOnline 20110726_A", polcore);
        add(p, "rev/" REV_2016 "/PolCore.xex", JOB_FILE, polcore, 0, size);
    }
    if (c->download_polcore2010) {
        say(p, false, "PolCore 2010: PlayOnline " POLCORE_2010 " for Project Crystal, into " POLCORE_2010_DEST
                      " (downloaded at install)");
        add(p, POLCORE_2010_DEST, JOB_POLCORE, POLCORE_2010, 0, POLCORE_SIZE);
    }

    if (c->root2010[0]) {
        if (!data_folder(c->root2010, data2010)) {
            say(p, 403, "2010: no FFXI data found");
        } else {
            // The 2010 data's version names its revision (PS2-<version>) and is what the launcher reports to the
            // lobby for it (rev\<revision>\version.txt; its patch.ver says 40160203_0, the 2016 programs it runs):
            // the version.txt next to the data folder when the 2010 data is one of our revisions, else September
            // 2010, what this slot holds.
            char version_txt[PATH_MAX_], version[32] = "20100904_2", dest[PATH_MAX_];
            join(path, data2010, "..");
            join(version_txt, path, "version.txt");
            uint64_t version_size = 0;
            bool own_version = is_file(version_txt, &version_size) && version_size > 0 && version_size < 32;
            if (own_version) {
                size_t len = 0;
                char *text = SDL_LoadFile(version_txt, &len);
                if (text) {
                    text[strcspn(text, " \r\n")] = 0;
                    if (text[0]) {
                        SDL_strlcpy(version, text, sizeof(version));
                    }
                    SDL_free(text);
                }
            }
            snprintf(p->rev2010, sizeof(p->rev2010), "PS2-%s", version);
            bool taken = false;
            for (int i = 0; i < 2; i++) {
                if (find_ci(data2010, PROGRAM[i].name, path)) {
                    if (PROGRAM[i].stamp) {
                        check_build(p, PROGRAM[i].name, PROGRAM[i].stamp, PROGRAM[i].label, path);
                    }
                } else if (data2016[0] && find_ci(data2016, PROGRAM[i].name, path) && is_file(path, &size)) {
                    snprintf(dest, sizeof(dest), "rev/%s/0001/%s", p->rev2010, PROGRAM[i].name);
                    add(p, dest, JOB_FILE, path, 0, size);
                    taken = true;
                }
            }
            if (taken) {
                say(p, false, "2010: program files taken from the 2016 client");
            }
            snprintf(dest, sizeof(dest), "rev/%s/0001", p->rev2010);
            add_tree(p, dest, data2010, false);
            add_config_sys(p, p->rev2010, data2010);
            snprintf(dest, sizeof(dest), "rev/%s/version.txt", p->rev2010);
            if (own_version) {
                add(p, dest, JOB_FILE, version_txt, 0, version_size);
            } else {
                add(p, dest, JOB_BYTES, SEP2010_VERSION, 0, sizeof(SEP2010_VERSION) - 1);
            }
            snprintf(dest, sizeof(dest), "rev/%s/PolCore.xex", p->rev2010);
            if (find_polcore(c->root2010, path) && is_file(path, &size)) {
                check_build(p, "PolCore", 0x4E240911, "PlayOnline 20110726_A", path);
                add(p, dest, JOB_FILE, path, 0, size);
            } else if (download) {
                add(p, dest, JOB_POLCORE, POLP_CAP, 0, POLCORE_SIZE);
            } else if (polcore[0] && is_file(polcore, &size)) {
                add(p, dest, JOB_FILE, polcore, 0, size);
            }
            say(p, false, "2010: revision %s", p->rev2010);
        }
    } else {
        say(p, false, "2010: none chosen (optional)");
    }

    bool have_area = false;
    if (c->iso[0]) {
        int n = iso_playonline(p, c->iso);
        if (n == 0) {
            say(p, 404, "disc image: no PlayOnline folder (use a FINAL FANTASY XI disc)");
        } else if (n > 0) {
            say(p, false, "PlayOnline: %d files from the disc image", n);
            have_area = true;   // a disc's PlayOnline folder holds area.dat
        }
    } else if (c->pol_folder[0]) {
        char top[PATH_MAX_];
        if (find_ci(c->pol_folder, "data", path)) {
            SDL_strlcpy(top, c->pol_folder, sizeof(top));
        } else if (!find_ci(c->pol_folder, "PlayOnline", top)) {
            top[0] = 0;
        }
        if (!top[0] || !is_dir(top)) {
            say(p, 404, "PlayOnline: no PlayOnline folder (with data\\) there");
        } else {
            add_tree(p, "PlayOnline", top, true);
            if (find_ci(top, "area.dat", path) && is_file(path, &size)) {
                add(p, "area.dat", JOB_FILE, path, 0, size);
                have_area = true;
            }
            say(p, false, "PlayOnline: from %s", top);
        }
    } else {
        say(p, 404, "PlayOnline: choose a game disc image or a PlayOnline folder");
    }
    if (c->update_pol) {
        say(p, false, "PlayOnline: files changed after the disc come from PlayOnline's update server (to 20110726_A)");
    }
    if (c->area[0] && is_file(c->area, &size)) {
        add(p, "area.dat", JOB_FILE, c->area, 0, size);
    } else if (!have_area) {
        add(p, "area.dat", JOB_BYTES, AREA_DAT, 0, sizeof(AREA_DAT) - 1);
        say(p, false, "area.dat: none given, writing the default (1)");
    }
    if (is_file(payload_xex, &size)) {
        add(p, "default.xex", JOB_FILE, payload_xex, 0, size);
    } else {
        say(p, 999, "launcher missing from this package (%s)", payload_xex);
    }
    if (opened && !dest_size("launcher.ini", nullptr)) {
        add(p, "launcher.ini", JOB_BYTES, LAUNCHER_INI_HEAD, 0, sizeof(LAUNCHER_INI_HEAD) - 1);
    }
    if (opened) {
        uint64_t need = 0;
        for (size_t i = 0; i < p->count; i++) {
            const char *inner;
            if (pooled_dest(p->jobs[i].dest, &inner)) {
                p->data_plain += p->jobs[i].size;
                if (c->share) {
                    continue;     // counted below, once per path and size
                }
            }
            if (!dest_same_size(p->jobs[i].dest, p->jobs[i].size)) {
                need += p->jobs[i].size;
            }
        }
        p->data_pooled = shared_estimate(p);
        if (c->share) {
            need += p->data_pooled;
        }
        double gb = 1073741824.0, saved = (double)(p->data_plain - p->data_pooled);
        int percent = p->data_plain ? (int)(100.0 * saved / (double)p->data_plain + 0.5) : 0;
        if (c->share) {
            say(p, false, "storage: POOLED, game data about %.1f GB instead of %.1f GB as plain folders "
                          "(saves %.1f GB, %d%%)", p->data_pooled / gb, p->data_plain / gb, saved / gb, percent);
        } else {
            say(p, false, "storage: DIRECT (plain folders), game data %.1f GB; POOLED would take about %.1f GB "
                          "(%.1f GB, %d%% less)", p->data_plain / gb, p->data_pooled / gb, saved / gb, percent);
        }
        if (dest.remote) {
            say(p, false, "space: %.1f GB to copy to %s (free space not known over FTP)", need / 1073741824.0,
                dest.where);
        } else {
            char probe[PATH_MAX_];
            SDL_strlcpy(probe, c->dest, sizeof(probe));
            while (probe[0] && !is_dir(probe)) {
                char *s = SDL_strrchr(probe, '/'), *b = SDL_strrchr(probe, '\\');
                if (b > s) {
                    s = b;
                }
                if (!s || s == probe) {
                    break;
                }
                *s = 0;
            }
            uint64_t avail = free_space(probe);
            say(p, avail <= need ? 301 : 0, "space: %.1f GB to copy, %.1f GB free at %s", need / 1073741824.0,
                avail / 1073741824.0, probe);
        }
    }
    if (p->skipped_logs) {
        say(p, false, "left out: %d files the game writes itself (SYS/err*.bin, USER, TEMP)", p->skipped_logs);
    }
    if (p->skipped_unsafe) {
        say(p, false, "left out: %d files the console cannot hold (empty, or a name over 42 characters)",
            p->skipped_unsafe);
    }
    dest_close();
}

// ─── Install ───────────────────────────────────────────────────────────────────────────────────────────────────

typedef struct shared {
    SDL_Mutex *lock;
    SDL_AtomicInt cancel;
    double fraction;
    char current[PATH_MAX_];
    char result[640];
    int code;          // ZZ code of the last result, 0 when it went well
    bool busy;
} shared;
static shared state;

static void progress(double f, const char *name)
{
    SDL_LockMutex(state.lock);
    state.fraction = f;
    SDL_strlcpy(state.current, name, sizeof(state.current));
    SDL_UnlockMutex(state.lock);
}

static bool make_parent(const char *path)
{
    char dir[PATH_MAX_];
    SDL_strlcpy(dir, path, sizeof(dir));
    char *s = SDL_strrchr(dir, '/');
    if (!s) {
        return true;
    }
    *s = 0;
    return is_dir(dir) || SDL_CreateDirectory(dir);
}

static bool write_all(const char *out, const void *data, size_t len)
{
    char part[PATH_MAX_];
    snprintf(part, sizeof(part), "%s.part", out);
    SDL_IOStream *f = SDL_IOFromFile(part, "wb");
    if (!f) {
        return false;
    }
    bool ok = SDL_WriteIO(f, data, len) == len;
    ok = SDL_CloseIO(f) && ok;
    return ok && SDL_RenamePath(part, out);
}

static bool copy_range(const char *src, uint64_t offset, uint64_t size, const char *out)
{
    static unsigned char buf[1 << 20];
    char part[PATH_MAX_];
    snprintf(part, sizeof(part), "%s.part", out);
    SDL_IOStream *in = SDL_IOFromFile(src, "rb"), *o = in ? SDL_IOFromFile(part, "wb") : nullptr;
    bool ok = in && o && SDL_SeekIO(in, (Sint64)offset, SDL_IO_SEEK_SET) >= 0;
    while (ok && size) {
        size_t n = size > sizeof(buf) ? sizeof(buf) : (size_t)size;
        ok = SDL_ReadIO(in, buf, n) == n && SDL_WriteIO(o, buf, n) == n;
        size -= n;
    }
    if (in) {
        SDL_CloseIO(in);
    }
    if (o) {
        ok = SDL_CloseIO(o) && ok;
    }
    return ok && SDL_RenamePath(part, out);
}

// The PolCore of PlayOnline `version` (a POLCORES entry), from the update server, checked by MD5.
static bool fetch_polcore(const char *rel, const char *version, char *error, size_t error_size)
{
    const char *want = nullptr;
    for (size_t i = 0; i < SDL_arraysize(POLCORES); i++) {
        if (strcmp(POLCORES[i].version, version) == 0) {
            want = POLCORES[i].md5;
        }
    }
    if (!want) {
        snprintf(error, error_size, "no PolCore known for PlayOnline %s", version);
        return false;
    }
    polp_file *files = nullptr;
    int n = polp_catalogue(version, &files, error, error_size);
    bool ok = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(files[i].path, "PolCoreContent.xex") == 0) {
            size_t len;
            unsigned char *data = polp_download(&files[i], &len, error, error_size), digest[16];
            char hex[33];
            if (data) {
                md5(data, (unsigned)len, digest);
                for (int k = 0; k < 16; k++) {
                    snprintf(hex + k * 2, 3, "%02x", digest[k]);
                }
                if (strcmp(hex, want) != 0) {
                    snprintf(error, error_size, "downloaded PolCore does not match PlayOnline %s", version);
                } else {
                    ok = dest_write(rel, data, len);
                    if (!ok) {
                        snprintf(error, error_size, "%s", dest.error[0] ? dest.error : "cannot write PolCore");
                    }
                }
                free(data);
            }
            break;
        }
    }
    if (n >= 0 && !ok && !error[0]) {
        snprintf(error, error_size, "PlayOnline's update server no longer lists PolCore %s", version);
    }
    polp_files_free(files, n > 0 ? n : 0);
    return ok;
}

// Where update file `path` goes in the install, or false when it is not written there: .xex files (PolCore is its
// own job) and files the console cannot hold (console_unsafe, as for the disc's files).
static bool playonline_rel(const polp_file *f, char *rel, size_t size)
{
    size_t len = strlen(f->path);
    if ((len > 4 && SDL_strcasecmp(f->path + len - 4, ".xex") == 0)
        || console_unsafe(f->path, f->size < 0 ? 0 : (uint64_t)f->size)) {
        return false;
    }
    snprintf(rel, size, "PlayOnline/%s", f->path);
    return true;
}

// The disc's copy of a file the update replaces would be written and then overwritten on every run (the sizes
// differ, so the incremental check never matches): leave those disc jobs out.
static void skip_updated_disc_files(plan *p)
{
    char error[256] = "";
    polp_file *files = nullptr;
    int n = polp_catalogue(POLP_CAP, &files, error, sizeof(error));
    for (int i = 0; i < n; i++) {
        char rel[PATH_MAX_];
        if (!playonline_rel(&files[i], rel, sizeof(rel))) {
            continue;
        }
        for (size_t k = 0; k < p->count; k++) {
            if ((p->jobs[k].kind == JOB_ISO || p->jobs[k].kind == JOB_FILE)
                && SDL_strcasecmp(p->jobs[k].dest, rel) == 0) {
                p->jobs[k].kind = JOB_SKIP;
            }
        }
    }
    polp_files_free(files, n > 0 ? n : 0);
}

// Every PlayOnline file changed after the disc, up to POLP_CAP, into <install>/PlayOnline, less the files the
// console cannot hold (playonline_rel; a download that comes back empty is left out too).
static int update_playonline(char *error, size_t error_size)
{
    polp_file *files = nullptr;
    int n = polp_catalogue(POLP_CAP, &files, error, error_size), written = 0;
    long total = 0, done = 0;
    for (int i = 0; i < n; i++) {
        total += files[i].blob_size;
    }
    for (int i = 0; i < n && !SDL_GetAtomicInt(&state.cancel); i++) {
        char label[PATH_MAX_], rel[PATH_MAX_];
        snprintf(label, sizeof(label), "PlayOnline update: %s", files[i].path);
        progress(total ? (double)done / total : 1.0, label);
        done += files[i].blob_size;
        if (!playonline_rel(&files[i], rel, sizeof(rel))) {
            continue;
        }
        if (dest_same_size(rel, (uint64_t)files[i].size)) {
            continue;
        }
        size_t got = 0;
        unsigned char *data = polp_download(&files[i], &got, error, error_size);
        if (data && got == 0) {
            free(data);
            continue;
        }
        if (!data || !dest_write(rel, data, got)) {
            if (!error[0]) {
                snprintf(error, error_size, "cannot write %s: %s", rel, dest.error);
            }
            free(data);
            written = -1;
            break;
        }
        free(data);
        written++;
    }
    polp_files_free(files, n > 0 ? n : 0);
    return n < 0 ? -1 : written;
}

// ─── Shared data pool ──────────────────────────────────────────────────────────────────────────────────────────
//
// With "share" (the default; -no-share turns it off) every distinct FFXI data file is written once, as
// data/NNN/MMM.dat, and each revision gets rev/<revision>/files.tbl, its table from game path to pool file; the
// launcher redirects the game's opens through it (launcher.c, load_data_table). Two revisions of mostly the same
// data take little more than one. The same layout as tools/ffxi360-pool.py: ROM* and sound* folders and
// FTABLE.DAT / VTABLE.DAT are pooled, everything else in 0001 stays a plain file. Files count as the same when
// their MD5 and size match; pool numbers follow the order of the plan, so a second run of an interrupted install
// assigns the same names and skips what is already there.

typedef struct pool_item {
    unsigned char md5[16];
    uint64_t size;
    size_t job;         // index in the plan
    uint32_t id;
} pool_item;

static int by_content(const void *a, const void *b)
{
    const pool_item *x = a, *y = b;
    int c = memcmp(x->md5, y->md5, 16);
    if (c == 0) {
        c = x->size < y->size ? -1 : x->size > y->size;
    }
    return c ? c : (x->job < y->job ? -1 : x->job > y->job);
}

static int by_job(const void *a, const void *b)
{
    const pool_item *x = a, *y = b;
    return x->job < y->job ? -1 : x->job > y->job;
}

typedef struct table_row {
    char key[POOL_KEY];
    uint32_t id;
} table_row;

static int by_key(const void *a, const void *b) { return memcmp(a, b, POOL_KEY); }

static bool read_job(const job *j, unsigned char *buf)
{
    SDL_IOStream *in = SDL_IOFromFile(j->src, "rb");
    bool ok = in && SDL_SeekIO(in, (Sint64)(j->kind == JOB_ISO ? j->offset : 0), SDL_IO_SEEK_SET) >= 0
              && (j->size == 0 || SDL_ReadIO(in, buf, (size_t)j->size) == (size_t)j->size);
    if (in) {
        SDL_CloseIO(in);
    }
    return ok;
}

static void put_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

// Turn the plan's data files into pool files and per-revision tables. Returns false (with `error`) when a file
// cannot be read or the plan holds more revisions than it can make tables for.
static bool pool_plan(plan *p, char *error, size_t error_size)
{
    size_t n = 0, largest = 0;
    for (size_t i = 0; i < p->count; i++) {
        const char *inner;
        if ((p->jobs[i].kind == JOB_FILE || p->jobs[i].kind == JOB_ISO) && pooled_dest(p->jobs[i].dest, &inner)) {
            n++;
            largest = p->jobs[i].size > largest ? (size_t)p->jobs[i].size : largest;
        }
    }
    if (n == 0) {
        return true;
    }
    pool_item *items = calloc(n, sizeof(pool_item));
    unsigned char *buf = malloc(largest ? largest : 1);
    if (!items || !buf) {
        free(items);
        free(buf);
        snprintf(error, error_size, "not enough memory to compare the game files");
        return false;
    }
    size_t k = 0;
    for (size_t i = 0; i < p->count && k < n; i++) {
        const char *inner;
        job *j = &p->jobs[i];
        if (!((j->kind == JOB_FILE || j->kind == JOB_ISO) && pooled_dest(j->dest, &inner))) {
            continue;
        }
        if (SDL_GetAtomicInt(&state.cancel)) {
            snprintf(error, error_size, "Cancelled. Install again to continue where it stopped.");
            free(items);
            free(buf);
            return false;
        }
        char label[PATH_MAX_];
        snprintf(label, sizeof(label), "Comparing versions: %s", j->dest);
        progress((double)k / n, label);
        if (!read_job(j, buf)) {
            snprintf(error, error_size, "cannot read %s", j->src);
            free(items);
            free(buf);
            return false;
        }
        md5(buf, (unsigned)j->size, items[k].md5);
        items[k].size = j->size;
        items[k].job = i;
        k++;
    }
    free(buf);

    // Pool numbers in plan order: sort by content, give each group's first job (lowest plan index) the group's
    // number once groups are ordered by that job.
    qsort(items, n, sizeof(pool_item), by_content);
    size_t groups = 0;
    size_t *first = malloc(n * sizeof(size_t));
    for (size_t i = 0; i < n; i++) {
        if (i == 0 || memcmp(items[i].md5, items[i - 1].md5, 16) != 0 || items[i].size != items[i - 1].size) {
            first[groups++] = items[i].job;
        }
        items[i].id = (uint32_t)(groups - 1);     // provisional: group index in content order
    }
    // Group index -> pool number by the plan order of its first job.
    pool_item *rank = malloc(groups * sizeof(pool_item));
    for (size_t g = 0; g < groups; g++) {
        rank[g] = (pool_item){.job = first[g], .id = (uint32_t)g};
    }
    qsort(rank, groups, sizeof(pool_item), by_job);
    uint32_t *number = malloc(groups * sizeof(uint32_t));
    for (size_t r = 0; r < groups; r++) {
        number[rank[r].id] = (uint32_t)r;
    }
    free(rank);

    // Tables, one per revision; pool.txt; the jobs' new destinations.
    char revisions[8][64];
    table_row *rows[8] = {0};
    size_t row_count[8] = {0};
    int revision_count = 0;
    size_t pool_txt_cap = groups * 48 + 1, pool_txt_len = 0;
    char *pool_txt = malloc(pool_txt_cap);
    bool ok = pool_txt != nullptr;
    for (size_t i = 0; i < n && ok; i++) {
        job *j = &p->jobs[items[i].job];
        const char *inner;
        size_t rev_len = pooled_dest(j->dest, &inner);
        uint32_t id = number[items[i].id];
        int r;
        for (r = 0; r < revision_count; r++) {
            if (strlen(revisions[r]) == rev_len && strncmp(revisions[r], j->dest + 4, rev_len) == 0) {
                break;
            }
        }
        if (r == revision_count) {
            if (revision_count == 8 || rev_len >= sizeof(revisions[0])) {
                snprintf(error, error_size, "too many revisions to share data between");
                ok = false;
                break;
            }
            SDL_strlcpy(revisions[r], j->dest + 4, rev_len + 1);
            rows[r] = calloc(n, sizeof(table_row));
            revision_count++;
        }
        table_row *row = &rows[r][row_count[r]++];
        for (size_t c = 0; inner[c]; c++) {
            char ch = inner[c] == '/' ? '\\' : inner[c];
            row->key[c] = (char)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
        }
        row->id = id;
        if (items[i].job == first[items[i].id]) {
            char dest[32];
            snprintf(dest, sizeof(dest), "data/%03u/%03u.dat", id / 1000, id % 1000);
            SDL_free(j->dest);
            j->dest = SDL_strdup(dest);
            pool_txt_len += (size_t)snprintf(pool_txt + pool_txt_len, pool_txt_cap - pool_txt_len, "%u ", id);
            for (int b = 0; b < 16; b++) {
                pool_txt_len += (size_t)snprintf(pool_txt + pool_txt_len, pool_txt_cap - pool_txt_len, "%02x",
                                                 items[i].md5[b]);
            }
            pool_txt_len += (size_t)snprintf(pool_txt + pool_txt_len, pool_txt_cap - pool_txt_len, " %llu\n",
                                             (unsigned long long)items[i].size);
        } else {
            j->kind = JOB_SKIP;   // the pool already holds these bytes
        }
    }
    free(items);
    free(first);
    free(number);
    for (int r = 0; r < revision_count; r++) {
        if (ok) {
            size_t bytes = 16 + row_count[r] * (POOL_KEY + 4);
            unsigned char *t = malloc(bytes);
            qsort(rows[r], row_count[r], sizeof(table_row), by_key);
            memcpy(t, "114360P1", 8);
            put_be32(t + 8, (uint32_t)row_count[r]);
            put_be32(t + 12, POOL_KEY);
            for (size_t i = 0; i < row_count[r]; i++) {
                memcpy(t + 16 + i * (POOL_KEY + 4), rows[r][i].key, POOL_KEY);
                put_be32(t + 16 + i * (POOL_KEY + 4) + POOL_KEY, rows[r][i].id);
            }
            char dest[96];
            snprintf(dest, sizeof(dest), "rev/%s/files.tbl", revisions[r]);
            add(p, dest, JOB_BYTES, (const char *)t, 0, bytes);
            p->owned[p->owned_count++] = t;
        }
        free(rows[r]);
    }
    if (ok) {
        // pool.txt last: its size marks a finished pool; an interrupted run writes it again.
        add(p, "data/pool.txt", JOB_BYTES, pool_txt, 0, pool_txt_len);
        p->owned[p->owned_count++] = pool_txt;
    } else {
        free(pool_txt);
    }
    return ok;
}

// ─── Revision names and storage mode ───────────────────────────────────────────────────────────────────────────

static SDL_EnumerationResult SDLCALL remove_entry(void *ud, const char *dir, const char *name)
{
    (void)ud;
    char path[PATH_MAX_];
    join(path, dir, name);
    return remove_tree(path) ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
}

// Delete a file, or a folder and everything in it.
static bool remove_tree(const char *path)
{
    if (is_dir(path) && !SDL_EnumerateDirectory(path, remove_entry, nullptr)) {
        return false;
    }
    return SDL_RemovePath(path);
}

// Until 2026-11-04 PS2 revisions were named rev/P20-<...>; the code is PS2, Square Enix's own (PS2.0001 on the
// patch servers). Collected first, renamed after the listing.
typedef struct {
    char names[32][64];
    int count;
} old_ps2_revisions;

static void collect_p20(void *ud, const char *name, bool dir)
{
    old_ps2_revisions *o = ud;
    if (dir && strncmp(name, "P20-", 4) == 0 && strlen(name) < sizeof(o->names[0]) && o->count < 32) {
        snprintf(o->names[o->count++], sizeof(o->names[0]), "%s", name);
    }
}

// Installs made before revisions were named after their data set used rev/40160203_0 and rev/sep2010, and later
// ones rev/P20-<version>. Rename them (so the copy that follows finds its files in place) and the launcher.ini
// presets that name them.
static void migrate_revisions(const char *rev2010)
{
    static const char *const old_names[2] = {"40160203_0", "sep2010"};
    const char *new_names[2] = {REV_2016, rev2010[0] ? rev2010 : REV_2010_DEFAULT};
    char from[96], to[96];
    for (int i = 0; i < 2; i++) {
        snprintf(from, sizeof(from), "rev/%s", old_names[i]);
        snprintf(to, sizeof(to), "rev/%s", new_names[i]);
        if (dest_is_dir(from) && !dest_is_dir(to)) {
            dest_rename(from, to);
        }
    }
    old_ps2_revisions old = {0};
    if (dest_is_dir("rev")) {
        dest_list("rev", collect_p20, &old);
    }
    for (int i = 0; i < old.count; i++) {
        snprintf(from, sizeof(from), "rev/%s", old.names[i]);
        snprintf(to, sizeof(to), "rev/PS2-%s", old.names[i] + 4);
        if (!dest_is_dir(to)) {
            dest_rename(from, to);
        }
    }
    size_t len = 0;
    char *text = dest_read("launcher.ini", &len);
    if (!text) {
        return;
    }
    // Rewrite line by line: "revision = <old>" (any spacing) becomes "revision = <new>".
    size_t cap = len + 256, n = 0;
    char *out = malloc(cap);
    bool changed = false;
    for (char *line = text; out && *line;) {
        char *end = strchr(line, '\n');
        size_t line_len = end ? (size_t)(end - line) + 1 : strlen(line);
        const char *k = line;
        while (*k == ' ' || *k == '\t') {
            k++;
        }
        bool replaced = false;
        if (SDL_strncasecmp(k, "revision", 8) == 0) {
            const char *v = k + 8;
            while (*v == ' ' || *v == '\t' || *v == '=') {
                v++;
            }
            for (int i = 0; i < 2 && !replaced; i++) {
                size_t ol = strlen(old_names[i]);
                if (strncmp(v, old_names[i], ol) == 0 && (v[ol] == '\r' || v[ol] == '\n' || v[ol] == 0 || v[ol] == ' ')) {
                    n += (size_t)snprintf(out + n, cap - n, "revision = %s\n", new_names[i]);
                    replaced = changed = true;
                }
            }
            if (!replaced && strncmp(v, "P20-", 4) == 0) {
                size_t vl = strcspn(v + 4, "\r\n ");
                n += (size_t)snprintf(out + n, cap - n, "revision = PS2-%.*s\n", (int)vl, v + 4);
                replaced = changed = true;
            }
        }
        if (!replaced && n + line_len < cap) {
            memcpy(out + n, line, line_len);
            n += line_len;
        }
        line += line_len;
    }
    if (out && changed) {
        dest_write("launcher.ini", out, n);
    }
    free(out);
    free(text);
}

typedef struct storage_walk {
    bool share;
    const char *installed[2];     // the revisions this install wrote
    int removed, tables_left;
    char rev[96];                 // the revision being looked at (settle_top)
} storage_walk;

static bool is_installed(const storage_walk *w, const char *name)
{
    return (w->installed[0] && strcmp(name, w->installed[0]) == 0)
           || (w->installed[1] && strcmp(name, w->installed[1]) == 0);
}

// One entry of rev/<revision>/0001 of a pooled revision: its pooled tops (ROM*, sound*, FTABLE/VTABLE) go.
static void settle_top(void *ud, const char *top, bool dir)
{
    storage_walk *w = ud;
    (void)dir;
    bool pooled = SDL_strncasecmp(top, "rom", 3) == 0 || SDL_strncasecmp(top, "sound", 5) == 0
                  || SDL_strcasecmp(top, "ftable.dat") == 0 || SDL_strcasecmp(top, "vtable.dat") == 0;
    char rel[PATH_MAX_];
    snprintf(rel, sizeof(rel), "rev/%s/0001/%s", w->rev, top);
    if (pooled && dest_remove(rel)) {
        w->removed++;
    }
}

// With "share", a revision this install pooled keeps no plain copies of its pooled files (left from a DIRECT
// install of it); without, the revisions it wrote plain lose their tables, and the pool goes once no revision
// uses it. Other revisions (made by other tools) are left as they are.
static void settle_revision(void *ud, const char *name, bool dir)
{
    storage_walk *w = ud;
    char table[PATH_MAX_];
    snprintf(table, sizeof(table), "rev/%s/files.tbl", name);
    if (!dir || !dest_size(table, nullptr)) {
        return;
    }
    if (w->share) {
        if (is_installed(w, name)) {
            char data[PATH_MAX_];
            SDL_strlcpy(w->rev, name, sizeof(w->rev));
            snprintf(data, sizeof(data), "rev/%s/0001", name);
            dest_list(data, settle_top, w);
        }
    } else if (is_installed(w, name) && dest_remove(table)) {
        w->removed++;
    } else {
        w->tables_left++;
    }
}

static int settle_storage(bool share, const char *rev2010)
{
    storage_walk w = {share, {REV_2016, rev2010[0] ? rev2010 : nullptr}, 0, 0, ""};
    dest_list("rev", settle_revision, &w);
    if (!share && w.tables_left == 0 && dest_is_dir("data") && dest_remove("data")) {
        w.removed++;
    }
    return w.removed;
}

typedef struct {
    plan p;
    choices c;
} install_ctx;

// Carry out plan `p` (from make_plan with `c`) into c's target: 0, or the ZZ code of what stopped it. `message`
// gets the result line ("Done: ..." or the error). Every file written or skipped goes to the log.
static int run_install(plan *p, const choices *c, char *message, size_t message_size)
{
    uint64_t total = 0, done = 0;
    char error[512] = "";
    int code = 0;
    size_t copied = 0, same = 0;
    ilog("install: to %s, %zu jobs, %s", c->target == TARGET_FTP ? "FTP" : c->dest, p->count,
         c->share ? "POOLED" : "DIRECT");
    bool ok = dest_open(c);
    if (!ok) {
        snprintf(error, sizeof(error), "%s", dest.error[0] ? dest.error : "no install location");
        code = c->target == TARGET_FTP ? 303 : 305;
    }
    if (ok) {
        migrate_revisions(p->rev2010);
        ok = !c->share || pool_plan(p, error, sizeof(error));
        if (!ok) {
            code = SDL_GetAtomicInt(&state.cancel) ? 902 : 305;
        }
        if (ok && p->update_pol) {
            skip_updated_disc_files(p);
        }
    }
    for (size_t i = 0; i < p->count; i++) {
        total += p->jobs[i].kind == JOB_SKIP ? 0 : p->jobs[i].size;
    }
    if (ok && !(dest_mkdirs("PlayOnline/pub/home00") && dest_mkdirs("PlayOnline/usr/home00"))) {
        snprintf(error, sizeof(error), "cannot make PlayOnline's folders: %s", dest.error);
        ok = false;
        code = 302;
    }
    for (size_t i = 0; i < p->count && ok; i++) {
        job *j = &p->jobs[i];
        if (j->kind == JOB_SKIP) {
            continue;
        }
        if (SDL_GetAtomicInt(&state.cancel)) {
            snprintf(error, sizeof(error), "Cancelled. Install again to continue where it stopped.");
            ok = false;
            code = 902;
            break;
        }
        progress(total ? (double)done / total : 1.0, j->dest);
        done += j->size;
        bool always = strcmp(j->dest, "default.xex") == 0 || strcmp(j->dest, "area.dat") == 0;
        if (!always && dest_same_size(j->dest, j->size)) {
            same++;
            ilog("skip  %s (%llu bytes, already there)", j->dest, (unsigned long long)j->size);
            continue;
        }
        dest.error[0] = 0;
        switch (j->kind) {
        case JOB_BYTES:
            ok = dest_write(j->dest, j->src, (size_t)j->size);
            break;
        case JOB_POLCORE:
            ok = fetch_polcore(j->dest, j->src, error, sizeof(error));
            code = ok ? 0 : 405;
            break;
        default:
            ok = dest_copy(j->dest, j->src, j->kind == JOB_ISO ? j->offset : 0, j->size);
            break;
        }
        ilog("%s %s (%llu bytes%s%s)", ok ? "write" : "FAIL ", j->dest, (unsigned long long)j->size,
             j->kind == JOB_FILE || j->kind == JOB_ISO ? " from " : "",
             j->kind == JOB_FILE || j->kind == JOB_ISO ? j->src : "");
        if (!ok && !error[0]) {
            snprintf(error, sizeof(error), "cannot write %s%s%s", j->dest, dest.error[0] ? ": " : "", dest.error);
        }
        if (!ok && !code) {
            code = c->target == TARGET_FTP ? 304 : 302;
        }
        copied++;
    }
    if (ok && p->update_pol) {
        int n = update_playonline(error, sizeof(error));
        ok = n >= 0;
        if (ok) {
            copied += (size_t)n;
        } else {
            code = 201;
        }
    }
    int settled = ok ? settle_storage(c->share, p->rev2010) : 0;
    dest_close();
    if (ok) {
        double gb = 1073741824.0;
        if (c->share) {
            snprintf(message, message_size,
                     "Done: %zu files written, %zu already there, to %s (POOLED: game data about %.1f GB, %.1f GB "
                     "saved%s)", copied, same, dest.where, p->data_pooled / gb,
                     (double)(p->data_plain - p->data_pooled) / gb, settled ? "; the DIRECT leftovers removed" : "");
        } else {
            snprintf(message, message_size, "Done: %zu files written, %zu already there, to %s (DIRECT: game data "
                     "%.1f GB%s)", copied, same, dest.where, p->data_plain / gb,
                     settled ? "; the POOLED leftovers removed" : "");
        }
    } else {
        snprintf(message, message_size, "ZZ%03d Stopped: %s", code ? code : 999, error);
    }
    ilog("install: %s", message);
    return ok ? 0 : (code ? code : 999);
}

static int install_thread(void *ud)
{
    install_ctx *ctx = ud;
    char message[600];
    int code = run_install(&ctx->p, &ctx->c, message, sizeof(message));
    SDL_LockMutex(state.lock);
    state.fraction = code ? state.fraction : 1.0;
    state.code = code;
    SDL_strlcpy(state.result, message, sizeof(state.result));
    state.current[0] = 0;
    state.busy = false;
    SDL_UnlockMutex(state.lock);
    plan_free(&ctx->p);
    free(ctx);
    return 0;
}

// ─── Window ────────────────────────────────────────────────────────────────────────────────────────────────────
//
// Five pages: 1. Info (logo, what you need, Regular install, or a Secret Key for a Simple install), 2. Game files,
// 3. PlayOnline, 4. Advanced (storage, console-unsafe files, extra 2010 sources, Generate secret key), 5. Install
// (the Simple wizard, or the Regular method/Check/Install). Every result carries its ZZ code; Copy report puts a
// plain and an encoded report on the clipboard for bug reports, with the log's path.

enum { PAGE_INFO, PAGE_FILES, PAGE_POL, PAGE_ADVANCED, PAGE_INSTALL, PAGE_LOG, PAGE_COUNT };
enum { HOW_USB, HOW_FTP, HOW_NEIGHBORHOOD, HOW_XENIA };
enum { POL_FROM_SE, POL_FROM_LOCAL };
enum { MODE_SIMPLE, MODE_REGULAR };    // the dropdown above the tabs

static choices ui;
static uint32_t how, pol_source;
static bool direct_install;            // Advanced: plain folders instead of the shared pool
static plan checked;                   // last regular check, shown and installed
static bool have_plan;
static char shown[MAX_MESSAGES][340], status_line[700];
static bool stop_flag;                 // simple_* and pack_build stop on it (state.cancel stops copies)
static char base_dir[PATH_MAX_];       // the installer's own folder
static uint32_t install_mode;          // MODE_SIMPLE: Info and Install only; MODE_REGULAR: every page
static int current_page = PAGE_INFO;   // the shown page as a PAGE_* (the tab index depends on the mode)

static void goto_page(int page) { current_page = page; }

static SDL_Window *main_window;
static const SDL_DialogFileFilter ISO_FILTER[] = {{"Disc images", "iso"}, {"All files", "*"}};
static const SDL_DialogFileFilter XEX_FILTER[] = {{"Xbox 360 executables", "xex"}, {"All files", "*"}};
static const SDL_DialogFileFilter ANY_FILTER[] = {{"All files", "*"}};

static void SDLCALL browse_done(void *ud, const char *const *list, int filter)
{
    (void)filter;
    char *target = ud;
    if (list && list[0]) {
        SDL_LockMutex(state.lock);
        SDL_strlcpy(target, list[0], PATH_MAX_);
        SDL_UnlockMutex(state.lock);
    }
}

static void browse_folder(void *ud) { SDL_ShowOpenFolderDialog(browse_done, ud, main_window, nullptr, false); }
static void browse_iso(void *ud) { SDL_ShowOpenFileDialog(browse_done, ud, main_window, ISO_FILTER, 2, nullptr, false); }
static void browse_xex(void *ud) { SDL_ShowOpenFileDialog(browse_done, ud, main_window, XEX_FILTER, 2, nullptr, false); }
static void browse_any(void *ud) { SDL_ShowOpenFileDialog(browse_done, ud, main_window, ANY_FILTER, 1, nullptr, false); }

static void work_dir(const choices *c, char *out)
{
    if (c->work[0]) {
        SDL_strlcpy(out, c->work, PATH_MAX_);
    } else {
        join(out, base_dir, "114360-work");
    }
}

// The window's state as install choices (taken under the lock: browse callbacks write the fields).
static void ui_choices(choices *c)
{
    SDL_LockMutex(state.lock);
    *c = ui;
    uint32_t h = how, pol = pol_source;
    bool direct = direct_install;
    SDL_UnlockMutex(state.lock);
    if (c->iso[0] && is_dir(c->iso)) {     // one field takes a disc image or a PlayOnline folder
        SDL_strlcpy(c->pol_folder, c->iso, sizeof(c->pol_folder));
        c->iso[0] = 0;
    }
    c->target = h == HOW_FTP ? TARGET_FTP : h == HOW_NEIGHBORHOOD ? TARGET_NEIGHBORHOOD : TARGET_FOLDER;
    if (pol == POL_FROM_LOCAL) {
        c->download_polcore = c->download_polcore2010 = c->update_pol = false;
    } else {
        c->polcore[0] = c->area[0] = 0;
    }
    c->share = !direct;
    keep_empty_files = c->keep_empty;
    keep_long_names = c->keep_long;
}

// ── Per-field Check buttons (Game files, PlayOnline, Advanced): one result line under each field ───────────────

enum { NOTE_WORK, NOTE_2016, NOTE_ISO, NOTE_PS2, NOTE_2026, NOTE_POLCORE, NOTE_AREA, NOTE_UC, NOTE_PC, NOTE_COUNT };
static char note[NOTE_COUNT][400];

static void set_note(int which, plan *p)
{
    char text[400] = "";
    size_t len = 0;
    for (int i = 0; i < p->message_count && len < sizeof(text); i++) {
        if (p->codes[i]) {
            len += (size_t)snprintf(text + len, sizeof(text) - len, "%s[ZZ%03d] %s", len ? "  " : "", p->codes[i],
                                    p->messages[i]);
        } else {
            len += (size_t)snprintf(text + len, sizeof(text) - len, "%s[ok] %s", len ? "  " : "", p->messages[i]);
        }
    }
    SDL_LockMutex(state.lock);
    SDL_strlcpy(note[which], text, sizeof(note[0]));
    SDL_UnlockMutex(state.lock);
    plan_free(p);
}

static void on_check_work(void *ud)
{
    (void)ud;
    choices c;
    ui_choices(&c);
    char dir[PATH_MAX_];
    work_dir(&c, dir);
    plan p = {.ok = true};
    if (!is_dir(dir) && !SDL_CreateDirectory(dir)) {
        say(&p, 302, "cannot make %s", dir);
    } else {
        say(&p, 0, "%s, %.1f GB free", dir, free_space(dir) / 1073741824.0);
    }
    set_note(NOTE_WORK, &p);
}

static void on_check_2016(void *ud)
{
    (void)ud;
    choices c;
    ui_choices(&c);
    char data[PATH_MAX_], path[PATH_MAX_];
    plan p = {.ok = true};
    if (!data_folder(c.root2016, data)) {
        say(&p, 401, "no FFXI data here (a folder with 0001\\ or FFXiMain.dll and ROM\\)");
    } else {
        for (int i = 0; i < 2; i++) {
            if (!find_ci(data, PROGRAM[i].name, path)) {
                say(&p, 401, "%s missing in %s", PROGRAM[i].name, data);
            } else {
                check_build(&p, PROGRAM[i].name, PROGRAM[i].stamp, PROGRAM[i].label, path);
            }
        }
    }
    set_note(NOTE_2016, &p);
}

static void on_check_iso(void *ud)
{
    (void)ud;
    choices c;
    ui_choices(&c);
    char path[PATH_MAX_];
    plan p = {.ok = true};
    if (c.pol_folder[0]) {
        if (find_ci(c.pol_folder, "data", path) || find_ci(c.pol_folder, "PlayOnline", path)) {
            say(&p, 0, "PlayOnline folder found");
        } else {
            say(&p, 404, "no PlayOnline folder (with data\\) there");
        }
    } else if (!c.iso[0]) {
        say(&p, 404, "choose a FINAL FANTASY XI disc image (.iso) or a PlayOnline folder");
    } else {
        int n = iso_playonline(&p, c.iso);
        if (n > 0) {
            say(&p, 0, "disc image: %d PlayOnline files", n);
        } else if (n == 0) {
            say(&p, 404, "disc image: no PlayOnline folder (use a FINAL FANTASY XI disc)");
        }
    }
    set_note(NOTE_ISO, &p);
}

static void on_check_ps2(void *ud)
{
    (void)ud;
    choices c;
    ui_choices(&c);
    char path[PATH_MAX_];
    plan p = {.ok = true};
    if (!c.root_ps2[0]) {
        say(&p, 0, "none chosen (optional: only for the PS2-2010 recreation)");
    } else if (!find_ci(c.root_ps2, "ROM", path) || !find_ci(c.root_ps2, "FTABLE.DAT", path)) {
        say(&p, 403, "no PS2 patch set here (a folder with ROM\\ and FTABLE.DAT)");
    } else {
        say(&p, 0, "PS2 patch set found");
    }
    set_note(NOTE_PS2, &p);
}

static void on_check_2026(void *ud)
{
    (void)ud;
    plan p = {.ok = true};
    say(&p, 0, "the PC-2026 recreation is not finished; this folder is not used yet");
    set_note(NOTE_2026, &p);
}

static void on_check_polcore(void *ud)
{
    (void)ud;
    choices c;
    ui_choices(&c);
    uint64_t size;
    plan p = {.ok = true};
    if (!c.polcore[0] || !is_file(c.polcore, &size)) {
        say(&p, 405, "choose PolCore.xex");
    } else {
        check_build(&p, "PolCore", 0x4E240911, "PlayOnline 20110726_A", c.polcore);
    }
    set_note(NOTE_POLCORE, &p);
}

static void on_check_area(void *ud)
{
    (void)ud;
    choices c;
    ui_choices(&c);
    uint64_t size = 0;
    plan p = {.ok = true};
    if (!c.area[0]) {
        say(&p, 0, "none chosen: the default (region 1) is written");
    } else if (!is_file(c.area, &size) || size == 0 || size > 64) {
        say(&p, 405, "%s is not an area.dat (a few bytes: the region code)", c.area);
    } else {
        say(&p, 0, "area.dat, %llu bytes", (unsigned long long)size);
    }
    set_note(NOTE_AREA, &p);
}

static void on_check_folder_note(int which, const char *folder, const char *what)
{
    char data[PATH_MAX_];
    plan p = {.ok = true};
    if (!folder[0]) {
        say(&p, 0, "none chosen");
    } else if (!data_folder(folder, data)) {
        say(&p, 403, "no FFXI data in %s (%s)", folder, what);
    } else {
        say(&p, 0, "%s found", what);
    }
    set_note(which, &p);
}

static void on_check_uc(void *ud)
{
    (void)ud;
    on_check_folder_note(NOTE_UC, ui.root_uc2010, "Ultimate Collection 2010 data");
}

static void on_check_pc(void *ud)
{
    (void)ud;
    on_check_folder_note(NOTE_PC, ui.root_pc2011, "PC 2011 data");
}

// ── Regular install: Check, Install, Cancel ────────────────────────────────────────────────────────────────────

static int check_thread(void *ud)
{
    (void)ud;
    plan p;
    choices c;
    ui_choices(&c);
    make_plan(&p, &c);
    SDL_LockMutex(state.lock);
    if (have_plan) {
        plan_free(&checked);
    }
    checked = p;
    have_plan = true;
    state.code = 0;
    for (int i = 0; i < p.message_count && !state.code; i++) {
        state.code = p.codes[i];
    }
    snprintf(state.result, sizeof(state.result), "%s",
             p.ok ? "Ready to install." : "Fix the items marked with a ZZ code first.");
    state.busy = false;
    SDL_UnlockMutex(state.lock);
    return 0;
}

static bool start_busy(void)
{
    SDL_LockMutex(state.lock);
    bool start = !state.busy;
    if (start) {
        state.busy = true;
        state.result[0] = 0;
        state.code = 0;
        state.fraction = 0;
        state.current[0] = 0;
        SDL_SetAtomicInt(&state.cancel, 0);
        stop_flag = false;
    }
    SDL_UnlockMutex(state.lock);
    return start;
}

static void on_check(void *ud)
{
    (void)ud;
    if (start_busy()) {
        SDL_DetachThread(SDL_CreateThread(check_thread, "check", nullptr));
    }
}

static void on_install(void *ud)
{
    (void)ud;
    SDL_LockMutex(state.lock);
    bool ready = have_plan && checked.ok && !state.busy;
    bool busy = state.busy;
    SDL_UnlockMutex(state.lock);
    if (!ready) {
        if (!busy) {
            SDL_LockMutex(state.lock);
            snprintf(state.result, sizeof(state.result), "Press Check first (and fix what it marks).");
            SDL_UnlockMutex(state.lock);
        }
        return;
    }
    if (!start_busy()) {
        return;
    }
    install_ctx *ctx = calloc(1, sizeof(*ctx));
    SDL_LockMutex(state.lock);
    ctx->p = checked;            // the thread owns this plan now
    checked = (plan){0};
    have_plan = false;
    SDL_UnlockMutex(state.lock);
    ui_choices(&ctx->c);
    SDL_DetachThread(SDL_CreateThread(install_thread, "install", ctx));
}

static void on_cancel(void *ud)
{
    (void)ud;
    SDL_SetAtomicInt(&state.cancel, 1);
    stop_flag = true;
}

// ── Simple install: Secret Key → manifest → packs → target ────────────────────────────────────────────────────

#define SIM_LINES 10
static struct {
    char key[4096];
    simple_key k;
    simple_manifest m;
    bool have;                 // a manifest is loaded
    bool chosen;               // the user pressed Simple Install: the Install page runs the wizard
    int step;                  // 0 ready?, 1 how, 2 where, 3 checked, 4 installing/done
    bool check_ok, done;
    char info[SIM_LINES][400];
    int info_count;
    char lines[SIM_LINES][400];
    int line_count;
    char where_text[400];
    uint64_t phase_total, phase_base;   // progress across downloads and writes
} sim;

static void sim_log(void *ud, const char *line)
{
    (void)ud;
    ilog("net: %s", line);
}

static void sim_progress(void *ud, uint64_t done, uint64_t total)
{
    const char *label = ud;
    SDL_LockMutex(state.lock);
    uint64_t all = sim.phase_total ? sim.phase_total : (total ? total : 1);
    state.fraction = (double)(sim.phase_base + (done < total ? done : total)) / (double)all;
    snprintf(state.current, sizeof(state.current), "%s (%.0f of %.0f MB)", label ? label : "", done / 1048576.0,
             total / 1048576.0);
    SDL_UnlockMutex(state.lock);
}

static void sim_info_add(const char *fmt, ...)
{
    if (sim.info_count == SIM_LINES) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sim.info[sim.info_count++], sizeof(sim.info[0]), fmt, ap);
    va_end(ap);
}

static void sim_line(const char *fmt, ...)
{
    char text[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    ilog("simple: %s", text);
    SDL_LockMutex(state.lock);
    if (sim.line_count < SIM_LINES) {
        SDL_strlcpy(sim.lines[sim.line_count++], text, sizeof(sim.lines[0]));
    }
    SDL_UnlockMutex(state.lock);
}

// The pack bytes still missing from the work folder (a complete pack file is reused; its SHA-512 is checked
// again before use). wanted: which packs count (nullptr = all).
static uint64_t sim_download_need(const char *work, const bool *wanted)
{
    uint64_t need = 0;
    for (int i = 0; i < sim.m.packs_count; i++) {
        if (wanted && !wanted[i]) {
            continue;
        }
        char path[PATH_MAX_];
        uint64_t size = 0;
        join(path, work, sim.m.packs[i].name);
        if (!is_file(path, &size) || size != sim.m.packs[i].size) {
            need += sim.m.packs[i].size - (size < sim.m.packs[i].size ? size : 0);
        }
    }
    return need;
}

static int key_thread(void *ud)
{
    (void)ud;
    char error[512] = "";
    SDL_LockMutex(state.lock);
    char key[sizeof(sim.key)];
    SDL_strlcpy(key, sim.key, sizeof(key));
    if (sim.have) {
        simple_manifest_free(&sim.m);
        sim.have = false;
    }
    sim.info_count = 0;
    sim.phase_total = sim.phase_base = 0;
    SDL_UnlockMutex(state.lock);
    simple_key k = {0};
    simple_manifest m = {0};
    int code = simple_key_parse(key, &k);
    ilog("key: parse -> %s", code ? simple_code_text(code) : "ok");
    if (!code) {
        code = simple_manifest_fetch(&k, &m, sim_progress, "Downloading the install's manifest", &stop_flag, error,
                                     sizeof(error));
    }
    SDL_LockMutex(state.lock);
    state.code = code;
    if (code) {
        snprintf(state.result, sizeof(state.result), "ZZ%03d %s", code,
                 error[0] ? error : code == 101 ? "that is not a 114360 secret key"
                 : code == 102 ? "the key's signature is wrong: it was changed or forged; do not use it"
                               : "the key could not be used");
    } else {
        sim.k = k;
        sim.m = m;
        sim.have = true;
        sim_info_add("%s", k.signed_present ? "Key: SIGNED by 114360 (verified)"
                                           : "Key: UNSIGNED - only use it if you trust the person who gave it to you");
        sim_info_add("Install: %s (made %s, %s storage)", m.name, m.created,
                     strcmp(m.storage, "pooled") == 0 ? "POOLED" : "DIRECT");
        for (int i = 0; i < m.sets_count; i++) {
            sim_info_add("  holds %s: %s%s%s", m.sets[i].id, m.sets[i].revision, m.sets[i].note[0] ? " - " : "",
                         m.sets[i].note);
        }
        uint64_t packs = 0;
        for (int i = 0; i < m.packs_count; i++) {
            packs += m.packs[i].size;
        }
        sim_info_add("Data Required to Download: %.2f GB in %d packs (installed size %.2f GB, %d files)",
                     packs / 1073741824.0, m.packs_count, m.size / 1073741824.0, m.files_count);
        snprintf(state.result, sizeof(state.result), "Key OK. Press Simple Install.");
        ilog("key: %s, %s, %d packs, %d files", k.signed_present ? "signed" : "unsigned", m.name, m.packs_count,
             m.files_count);
    }
    state.current[0] = 0;
    state.busy = false;
    SDL_UnlockMutex(state.lock);
    return 0;
}

static void on_check_key(void *ud)
{
    (void)ud;
    if (start_busy()) {
        SDL_DetachThread(SDL_CreateThread(key_thread, "key", nullptr));
    }
}

static void on_simple_install(void *ud)
{
    (void)ud;
    SDL_LockMutex(state.lock);
    bool ok = sim.have && !state.busy;
    if (ok) {
        sim.step = 0;
        sim.check_ok = sim.done = false;
        sim.line_count = 0;
        state.result[0] = 0;
        state.code = 0;
    } else if (!state.busy) {
        snprintf(state.result, sizeof(state.result), "Paste a Secret Key and press Check key first.");
    }
    SDL_UnlockMutex(state.lock);
    if (ok) {
        goto_page(PAGE_INSTALL);
    }
}

static void on_regular_next(void *ud)
{
    (void)ud;
    goto_page(PAGE_FILES);
}

static void on_sim_ready(void *ud) { (void)ud; sim.step = sim.step < 1 ? 1 : sim.step; }
static void on_sim_how(void *ud) { (void)ud; sim.step = sim.step < 2 ? 2 : sim.step; }

// Wizard step 3: open the target, size what has to be written and downloaded, check the space.
static int simple_check_thread(void *ud)
{
    (void)ud;
    choices c;
    ui_choices(&c);
    char work[PATH_MAX_];
    work_dir(&c, work);
    SDL_LockMutex(state.lock);
    sim.line_count = 0;
    SDL_UnlockMutex(state.lock);
    sim_line("Okay, good, please hang tight:");
    int code = 0;
    if (!is_dir(work) && !SDL_CreateDirectory(work)) {
        sim_line("[ZZ302] cannot make the work folder %s", work);
        code = 302;
    }
    if (!code && !dest_open(&c)) {
        code = c.target == TARGET_FTP ? 303 : 305;
        sim_line("[ZZ%03d] %s", code, dest.error[0] ? dest.error : "choose where to install");
    }
    if (!code) {
        uint64_t write = 0;
        int skipped = 0;
        for (int i = 0; i < sim.m.files_count; i++) {
            const simple_file *f = &sim.m.files[i];
            if (console_unsafe(f->path, f->size)) {
                skipped++;
            } else if (!dest_same_size(f->path, f->size)) {
                write += f->size;
            }
        }
        uint64_t download = sim_download_need(work, nullptr);
        sim_line("[ok] the install: %s (%s key)", sim.m.name, sim.k.signed_present ? "signed" : "UNSIGNED");
        sim_line("[ok] installing to %s", dest.where);
        sim_line("[ok] to download: %.2f GB into %s", download / 1073741824.0, work);
        uint64_t work_free = free_space(work);
        if (work_free <= download) {
            sim_line("[ZZ301] the work folder has %.2f GB free; it needs %.2f GB", work_free / 1073741824.0,
                     download / 1073741824.0);
            code = 301;
        }
        if (dest.remote) {
            sim_line("[ok] to write: %.2f GB (the console's free space cannot be read over FTP)",
                     write / 1073741824.0);
        } else {
            uint64_t avail = free_space(c.dest);
            if (avail <= write) {
                sim_line("[ZZ301] %.2f GB to write, only %.2f GB free at %s", write / 1073741824.0,
                         avail / 1073741824.0, c.dest);
                code = 301;
            } else {
                sim_line("[ok] to write: %.2f GB, %.2f GB free", write / 1073741824.0, avail / 1073741824.0);
            }
        }
        if (skipped) {
            sim_line("[ok] left out: %d files the console cannot hold (Advanced to keep them)", skipped);
        }
        dest_close();
    }
    SDL_LockMutex(state.lock);
    sim.check_ok = code == 0;
    sim.step = 3;
    state.code = code;
    snprintf(state.result, sizeof(state.result), "%s",
             code ? "Fix the items marked with a ZZ code, then Continue again." : "Okay ready! Press INSTALL.");
    state.busy = false;
    SDL_UnlockMutex(state.lock);
    return 0;
}

static void on_sim_where(void *ud)
{
    (void)ud;
    if (sim.have && start_busy()) {
        SDL_DetachThread(SDL_CreateThread(simple_check_thread, "simple-check", nullptr));
    }
}

// launcher.ini already in the install: keep it (the user's servers and passwords) and add only the presets it
// does not have yet. Returns a new buffer (caller frees), or nullptr when nothing is missing.
static char *merge_launcher_ini(const char *mine, size_t mine_len, const char *theirs, size_t theirs_len,
                                size_t *out_len)
{
    size_t len = mine_len;
    char *out = malloc(mine_len + theirs_len + 2);
    memcpy(out, mine, mine_len);
    bool added = false;
    for (size_t i = 0; i < theirs_len;) {
        // [i, end): one preset (a "[name]" line and what follows up to the next one), or the head before the first
        size_t end = i + 1;
        while (end < theirs_len && !(theirs[end] == '[' && theirs[end - 1] == '\n')) {
            end++;
        }
        if (theirs[i] == '[') {
            const char *close = memchr(theirs + i, ']', end - i);
            char name[128];
            size_t n = close ? (size_t)(close - (theirs + i)) + 1 : 0;
            if (n && n < sizeof(name)) {
                memcpy(name, theirs + i, n);
                name[n] = 0;
                bool have = false;
                for (size_t s = 0; s + n <= mine_len && !have; s++) {
                    have = (s == 0 || mine[s - 1] == '\n') && memcmp(mine + s, name, n) == 0;
                }
                if (!have) {
                    if (len && out[len - 1] != '\n') {
                        out[len++] = '\n';
                    }
                    memcpy(out + len, theirs + i, end - i);
                    len += end - i;
                    added = true;
                    ilog("launcher.ini: added preset %s", name);
                }
            }
        }
        i = end;
    }
    if (!added) {
        free(out);
        return nullptr;
    }
    out[len] = 0;
    *out_len = len;
    return out;
}

static int simple_install_thread(void *ud)
{
    (void)ud;
    choices c;
    ui_choices(&c);
    char work[PATH_MAX_], error[512] = "", message[640];
    work_dir(&c, work);
    int code = 0;
    size_t written = 0, same = 0, skipped = 0;
    ilog("simple: install %s to %s", sim.m.name, c.target == TARGET_FTP ? c.ftp_host : c.dest);
    if (!dest_open(&c)) {
        code = c.target == TARGET_FTP ? 303 : 305;
        snprintf(error, sizeof(error), "%s", dest.error);
    }
    // What has to be written, and so which packs are needed.
    bool *wanted = calloc((size_t)(sim.m.packs_count > 0 ? sim.m.packs_count : 1), sizeof(bool));
    bool *todo = calloc((size_t)(sim.m.files_count > 0 ? sim.m.files_count : 1), sizeof(bool));
    uint64_t write_total = 0;
    for (int i = 0; i < sim.m.files_count && !code; i++) {
        const simple_file *f = &sim.m.files[i];
        bool always = strcmp(f->path, "default.xex") == 0 || strcmp(f->path, "launcher.ini") == 0;
        if (console_unsafe(f->path, f->size)) {
            skipped++;
            ilog("leave %s (the console cannot hold it)", f->path);
        } else if (!always && dest_same_size(f->path, f->size)) {
            same++;
        } else {
            todo[i] = true;
            wanted[f->pack] = true;
            write_total += f->size;
        }
    }
    uint64_t download_total = code ? 0 : sim_download_need(work, wanted);
    SDL_LockMutex(state.lock);
    sim.phase_total = download_total + write_total;
    sim.phase_base = 0;
    SDL_UnlockMutex(state.lock);
    // Downloads (a complete pack in the work folder is checked and reused).
    for (int i = 0; i < sim.m.packs_count && !code; i++) {
        if (!wanted[i]) {
            continue;
        }
        char label[96], path[PATH_MAX_];
        uint64_t had = 0;
        join(path, work, sim.m.packs[i].name);
        is_file(path, &had);
        snprintf(label, sizeof(label), "Downloading %s", sim.m.packs[i].name);
        code = simple_pack_fetch(&sim.m, i, work, sim_progress, label, &stop_flag, error, sizeof(error));
        ilog("pack %s: %s", sim.m.packs[i].name, code ? simple_code_text(code) : "ok");
        SDL_LockMutex(state.lock);
        sim.phase_base += had < sim.m.packs[i].size ? sim.m.packs[i].size - had : 0;
        SDL_UnlockMutex(state.lock);
        if (stop_flag && code) {
            code = 902;
        }
    }
    // Files.
    uint64_t done = 0;
    for (int i = 0; i < sim.m.files_count && !code; i++) {
        if (!todo[i]) {
            continue;
        }
        if (stop_flag || SDL_GetAtomicInt(&state.cancel)) {
            code = 902;
            snprintf(error, sizeof(error), "Cancelled. Install again to continue where it stopped.");
            break;
        }
        const simple_file *f = &sim.m.files[i];
        SDL_LockMutex(state.lock);
        state.fraction = (double)(sim.phase_base + done) / (double)(sim.phase_total ? sim.phase_total : 1);
        SDL_strlcpy(state.current, f->path, sizeof(state.current));
        SDL_UnlockMutex(state.lock);
        unsigned char *data = nullptr;
        size_t len = 0;
        code = simple_file_read(&sim.m, f, work, &data, &len, error, sizeof(error));
        if (!code) {
            size_t have_len = 0;
            char *have = strcmp(f->path, "launcher.ini") == 0 ? dest_read("launcher.ini", &have_len) : nullptr;
            bool ok;
            if (have) {
                size_t merged_len = 0;
                char *merged = merge_launcher_ini(have, have_len, (const char *)data, len, &merged_len);
                ok = !merged || dest_write("launcher.ini", merged, merged_len);
                ilog("launcher.ini: %s", merged ? "kept yours, added the missing presets" : "kept yours");
                free(merged);
                SDL_free(have);
            } else {
                ok = dest_write(f->path, data, len);
            }
            if (!ok) {
                code = c.target == TARGET_FTP ? 304 : 302;
                snprintf(error, sizeof(error), "cannot write %s: %s", f->path, dest.error);
            }
            written++;
        }
        ilog("%s %s (%llu bytes)", code ? "FAIL " : "write", f->path, (unsigned long long)f->size);
        free(data);
        done += f->size;
    }
    if (!code && !(dest_mkdirs("PlayOnline/pub/home00") && dest_mkdirs("PlayOnline/usr/home00"))) {
        code = 302;
        snprintf(error, sizeof(error), "cannot make PlayOnline's folders: %s", dest.error);
    }
    if (!code) {
        const char *rev2010 = "";
        for (int i = 0; i < sim.m.sets_count; i++) {
            if (strcmp(sim.m.sets[i].id, "2010") == 0) {
                rev2010 = sim.m.sets[i].revision;
            }
        }
        settle_storage(strcmp(sim.m.storage, "pooled") == 0, rev2010);
    }
    dest_close();
    free(wanted);
    free(todo);
    if (code) {
        snprintf(message, sizeof(message), "ZZ%03d Stopped: %s", code, error[0] ? error : simple_code_text(code));
    } else {
        snprintf(message, sizeof(message), "Done: %zu files written, %zu already there, %zu left out, to %s",
                 written, same, skipped, dest.where);
    }
    ilog("simple: %s", message);
    SDL_LockMutex(state.lock);
    state.code = code;
    state.fraction = code ? state.fraction : 1.0;
    SDL_strlcpy(state.result, message, sizeof(state.result));
    state.current[0] = 0;
    state.busy = false;
    sim.done = code == 0;
    SDL_UnlockMutex(state.lock);
    return 0;
}

static void on_sim_install(void *ud)
{
    (void)ud;
    if (sim.have && sim.check_ok && start_busy()) {
        sim.step = 4;
        SDL_DetachThread(SDL_CreateThread(simple_install_thread, "simple-install", nullptr));
    }
}

// ── Advanced: Generate secret key (stage a regular install, pack it, then a key for its Drive folder) ─────────

static bool gen_2016 = true, gen_2010 = true, gen_2026, gen_pool = true, gen_direct;
static struct {
    char dir[2][PATH_MAX_];        // hosted folders made: [0] pooled, [1] direct
    unsigned char sha[2][64];
    bool made[2];
    char link[2][512], key[2][1024];
} gen;

static void gen_progress(void *ud, uint64_t done, uint64_t total)
{
    SDL_LockMutex(state.lock);
    state.fraction = total ? (double)done / (double)total : 0;
    snprintf(state.current, sizeof(state.current), "%s", (const char *)ud);
    SDL_UnlockMutex(state.lock);
}

static int gen_thread(void *ud)
{
    (void)ud;
    choices base;
    ui_choices(&base);
    char work[PATH_MAX_], message[640] = "";
    work_dir(&base, work);
    int code = 0;
    for (int s = 0; s < 2 && !code; s++) {
        bool want = s == 0 ? gen_pool : gen_direct;
        gen.made[s] = false;
        if (!want) {
            continue;
        }
        choices c = base;
        char stage[PATH_MAX_];
        join(stage, work, s == 0 ? "stage-pooled" : "stage-direct");
        join(gen.dir[s], work, s == 0 ? "hosted-pooled" : "hosted-direct");
        SDL_CreateDirectory(work);
        c.target = TARGET_FOLDER;
        SDL_strlcpy(c.dest, stage, sizeof(c.dest));
        c.share = s == 0;
        if (!gen_2010) {
            c.root2010[0] = c.root_ps2[0] = 0;
        }
        plan p;
        make_plan(&p, &c);
        if (!p.ok) {
            for (int i = 0; i < p.message_count && !code; i++) {
                if (p.codes[i]) {
                    code = p.codes[i];
                    snprintf(message, sizeof(message), "ZZ%03d %s (fix it on the Game files page)", code,
                             p.messages[i]);
                }
            }
            plan_free(&p);
            break;
        }
        code = run_install(&p, &c, message, sizeof(message));
        plan_free(&p);
        if (code) {
            break;
        }
        pack_set sets[2];
        int set_count = 0;
        if (gen_2016) {
            sets[set_count++] = (pack_set){"2016", REV_2016, "the 40160203_0 Xbox 360 client"};
        }
        if (gen_2010 && (c.root2010[0] || c.root_ps2[0])) {
            sets[set_count++] = (pack_set){"2010", REV_2010_DEFAULT, "September 2010 data"};
        }
        char name[160], err[512] = "";
        snprintf(name, sizeof(name), "114360 %s%s(%s)", gen_2016 ? "2016 " : "", set_count > 1 ? "+ 2010 " : "",
                 s == 0 ? "pooled" : "direct");
        code = pack_build(stage, gen.dir[s], name, sets, set_count, gen_progress, "Packing for hosting", &stop_flag,
                          gen.sha[s], err, sizeof(err));
        if (code) {
            snprintf(message, sizeof(message), "ZZ%03d %s", code, err);
        } else {
            gen.made[s] = true;
            snprintf(message, sizeof(message), "Packed into %s: upload that folder to Google Drive, share it "
                     "\"Anyone with the link\", paste its link below and press Make key.", gen.dir[s]);
        }
        ilog("generate: %s", message);
    }
    SDL_LockMutex(state.lock);
    state.code = code;
    SDL_strlcpy(state.result, message, sizeof(state.result));
    state.current[0] = 0;
    state.busy = false;
    SDL_UnlockMutex(state.lock);
    return 0;
}

static void on_generate(void *ud)
{
    (void)ud;
    if (!gen_pool && !gen_direct) {
        snprintf(state.result, sizeof(state.result), "Tick POOL INSTALL and/or DIRECT INSTALL.");
        return;
    }
    if (start_busy()) {
        SDL_DetachThread(SDL_CreateThread(gen_thread, "generate", nullptr));
    }
}

static void on_make_key(void *ud)
{
    int s = (int)(intptr_t)ud;
    char id[256];
    if (!gen.made[s]) {
        snprintf(gen.key[s], sizeof(gen.key[s]), "Build the hosted install first.");
    } else if (!pack_folder_id(gen.link[s], id, sizeof(id))) {
        snprintf(gen.key[s], sizeof(gen.key[s]), "Paste the Google Drive folder's share link.");
    } else if (!pack_key(id, gen.sha[s], gen.key[s], sizeof(gen.key[s]))) {
        snprintf(gen.key[s], sizeof(gen.key[s]), "ZZ999 the key does not fit.");
    } else {
        SDL_SetClipboardText(gen.key[s]);
        ilog("generate: key for folder %s made (unsigned), copied to the clipboard", id);
    }
}

// ── Report (Copy report: plain text and an encoded line, for Discord or email) ───────────────────────────────

static void on_copy_report(void *ud)
{
    (void)ud;
    char text[8192];
    size_t len = 0;
    SDL_LockMutex(state.lock);
    len += (size_t)snprintf(text + len, sizeof(text) - len, "114360 installer report (%s)\nresult: %s\n",
                            SDL_GetPlatform(), state.result[0] ? state.result : "(none)");
    if (state.code) {
        len += (size_t)snprintf(text + len, sizeof(text) - len, "code: ZZ%03d\n", state.code);
    }
    for (int i = 0; have_plan && i < checked.message_count && len < sizeof(text); i++) {
        if (checked.codes[i]) {
            len += (size_t)snprintf(text + len, sizeof(text) - len, "ZZ%03d %s\n", checked.codes[i],
                                    checked.messages[i]);
        } else {
            len += (size_t)snprintf(text + len, sizeof(text) - len, "ok    %s\n", checked.messages[i]);
        }
    }
    for (int i = 0; sim.chosen && i < sim.line_count && len < sizeof(text); i++) {
        len += (size_t)snprintf(text + len, sizeof(text) - len, "%s\n", sim.lines[i]);
    }
    SDL_UnlockMutex(state.lock);
    if (len >= sizeof(text)) {
        len = sizeof(text) - 1;
    }
    // The same report as one base64 line: survives chat formatting.
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t enc_len = (len + 2) / 3 * 4;
    char *all = malloc(len + enc_len + PATH_MAX_ + 64);
    int head = snprintf(all, len + PATH_MAX_ + 64, "%.*slog: %s\nencoded: 114360R1:", (int)len, text, log_path);
    size_t o = (size_t)head;
    for (size_t i = 0; i < len; i += 3) {
        unsigned v = (unsigned char)text[i] << 16 | (i + 1 < len ? (unsigned char)text[i + 1] << 8 : 0)
                     | (i + 2 < len ? (unsigned char)text[i + 2] : 0);
        all[o++] = b64[v >> 18 & 63];
        all[o++] = b64[v >> 12 & 63];
        all[o++] = i + 1 < len ? b64[v >> 6 & 63] : '=';
        all[o++] = i + 2 < len ? b64[v & 63] : '=';
    }
    all[o++] = '\n';
    all[o] = 0;
    SDL_SetClipboardText(all);
    ilog("report copied to the clipboard (%zu bytes)", o);
    free(all);
}

// Command line, the same flags as core.py: INSTALL -4016 ROOT [-2010 ROOT] [-iso DISC | -pol DIR]
// [-polcore FILE|download] [-polcore2010] [-area FILE] [-update-pol] [-neighborhood] [-no-share] [-check].
// INSTALL is a folder, or ftp://[user[:password]@]host[:port]/Hdd1/114360 to upload to a console (user and
// password default to xboxftp). Without arguments the window opens.

// "ftp://user:password@host:port/folder" into `c` (TARGET_FTP). False if `url` is not an ftp:// URL.
static bool parse_ftp_url(const char *url, choices *c)
{
    if (SDL_strncasecmp(url, "ftp://", 6) != 0) {
        return false;
    }
    char rest[PATH_MAX_];
    SDL_strlcpy(rest, url + 6, sizeof(rest));
    char *slash = strchr(rest, '/');
    SDL_strlcpy(c->ftp_dir, slash ? slash : "/", sizeof(c->ftp_dir));
    if (slash) {
        *slash = 0;
    }
    char *at = strrchr(rest, '@'), *host = rest;
    if (at) {
        *at = 0;
        host = at + 1;
        char *colon = strchr(rest, ':');
        if (colon) {
            *colon = 0;
            SDL_strlcpy(c->ftp_password, colon + 1, sizeof(c->ftp_password));
        }
        SDL_strlcpy(c->ftp_user, rest, sizeof(c->ftp_user));
    }
    SDL_strlcpy(c->ftp_host, host, sizeof(c->ftp_host));
    c->target = TARGET_FTP;
    return true;
}

// Prints the running install's progress until its thread is done; returns the result line.
static const char *cli_wait(SDL_Thread *t)
{
    char last[PATH_MAX_] = "";
    for (;;) {
        SDL_LockMutex(state.lock);
        bool busy = state.busy;
        if (strcmp(last, state.current) != 0) {
            SDL_strlcpy(last, state.current, sizeof(last));
            printf("\r%3d%% %-70.70s", (int)(state.fraction * 100), last);
            fflush(stdout);
        }
        SDL_UnlockMutex(state.lock);
        if (!busy) {
            break;
        }
        SDL_Delay(100);
    }
    SDL_WaitThread(t, nullptr);
    printf("\n%s\n", state.result);
    return state.result;
}

// 114360-installer <INSTALL LOCATION> -key <key | @file> [-work <dir>] [-neighborhood]: the Simple install of
// the window (the same threads: key and manifest, the check, then download and write) without it.
static int command_line_key(const choices *where, const char *key_arg, int argc, char **argv)
{
    SDL_LockMutex(state.lock);
    ui = *where;
    how = where->target == TARGET_FTP ? HOW_FTP : where->target == TARGET_NEIGHBORHOOD ? HOW_NEIGHBORHOOD : HOW_USB;
    SDL_UnlockMutex(state.lock);
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-work") == 0 && i + 1 < argc) {
            SDL_strlcpy(ui.work, argv[++i], sizeof(ui.work));
        }
    }
    if (key_arg[0] == '@') {          // the key from a file
        size_t len = 0;
        char *text = SDL_LoadFile(key_arg + 1, &len);
        if (!text) {
            printf("ZZ101    cannot read the key file %s\n", key_arg + 1);
            return 1;
        }
        SDL_strlcpy(sim.key, text, sizeof(sim.key));
        SDL_free(text);
    } else {
        SDL_strlcpy(sim.key, key_arg, sizeof(sim.key));
    }
    state.busy = true;
    key_thread(nullptr);
    for (int i = 0; i < sim.info_count; i++) {
        printf("ok       %s\n", sim.info[i]);
    }
    if (state.code) {
        printf("%s\n", state.result);
        return 1;
    }
    sim.chosen = true;
    state.busy = true;
    simple_check_thread(nullptr);
    for (int i = 0; i < sim.line_count; i++) {
        printf("%s\n", sim.lines[i]);
    }
    if (!sim.check_ok) {
        return 1;
    }
    state.busy = true;
    const char *result = cli_wait(SDL_CreateThread(simple_install_thread, "simple-install", nullptr));
    return strncmp(result, "Done", 4) == 0 ? 0 : 1;
}

static int command_line(int argc, char **argv)
{
    choices c = {.share = true};
    bool check_only = false;
    if (!parse_ftp_url(argv[1], &c)) {
        SDL_strlcpy(c.dest, argv[1], sizeof(c.dest));
    }
#ifdef _WIN32
    // A windowed program has no console: borrow the one it was started from, so the results show there.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
    for (int i = 2; i + 1 < argc; i++) {
        if (strcmp(argv[i], "-key") == 0) {
            for (int j = 2; j < argc; j++) {
                if (strcmp(argv[j], "-neighborhood") == 0 && c.target == TARGET_FOLDER) {
                    c.target = TARGET_NEIGHBORHOOD;
                }
            }
            return command_line_key(&c, argv[i + 1], argc, argv);
        }
    }
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (strcmp(a, "-check") == 0) {
            check_only = true;
        } else if (strcmp(a, "-update-pol") == 0) {
            c.update_pol = true;
        } else if (strcmp(a, "-no-share") == 0) {
            c.share = false;
        } else if (strcmp(a, "-neighborhood") == 0) {
            if (c.target == TARGET_FOLDER) {
                c.target = TARGET_NEIGHBORHOOD;
            }
        } else if (strcmp(a, "-polcore2010") == 0) {
            c.download_polcore2010 = true;
        } else if (strcmp(a, "-polcore") == 0 && strcmp(v, "download") == 0) {
            c.download_polcore = true;
            i++;
        } else {
            char *dst = strcmp(a, "-4016") == 0 ? c.root2016 : strcmp(a, "-2010") == 0 ? c.root2010
                      : strcmp(a, "-iso") == 0 ? c.iso : strcmp(a, "-pol") == 0 ? c.pol_folder
                      : strcmp(a, "-polcore") == 0 ? c.polcore : strcmp(a, "-area") == 0 ? c.area : nullptr;
            if (!dst) {
                fprintf(stderr, "unknown option %s\n", a);
                return 2;
            }
            SDL_strlcpy(dst, v, PATH_MAX_);
            i++;
        }
    }
    plan p;
    make_plan(&p, &c);
    for (int i = 0; i < p.message_count; i++) {
        if (p.codes[i]) {
            printf("ZZ%03d    %s\n", p.codes[i], p.messages[i]);
        } else {
            printf("ok       %s\n", p.messages[i]);
        }
    }
    if (!p.ok || check_only) {
        int rc = p.ok ? 0 : 1;
        plan_free(&p);
        return rc;
    }
    install_ctx *ctx = calloc(1, sizeof(*ctx));
    ctx->p = p;
    ctx->c = c;
    state.busy = true;
    SDL_Thread *t = SDL_CreateThread(install_thread, "install", ctx);
    return strncmp(cli_wait(t), "Done", 4) == 0 ? 0 : 1;
}

// The Info page's logo (installer/assets/logo.png; replace the file and rebuild to change it).
static const unsigned char LOGO_PNG[] = {
#embed "../assets/logo.png"
};

// ── Layout: entries are appended in order; a section is a run of them; pages show the sections marked visible ──

#define MAX_ENTRIES 320
#define MAX_SECTIONS 64
static dg_presdl_entry entries[MAX_ENTRIES];
static uint32_t entry_count;

typedef struct ui_section {
    dg_presdl_section s;
    int page;
    bool *visible;     // nullptr = always
    bool show;         // the frame's visibility (computed in the loop)
} ui_section;
static ui_section all_sections[MAX_SECTIONS];
static int section_count;
static uint32_t section_first;

static uint32_t add_entry(dg_presdl_entry e)
{
    if (entry_count == MAX_ENTRIES) {
        SDL_Log("installer: too many window entries");
        return entry_count - 1;
    }
    entries[entry_count] = e;
    return entry_count++;
}

#define LABEL(text) add_entry((dg_presdl_entry){text, DG_PRESDL_SECTIONIONING})
#define BUTTON(label, fn, ud) \
    add_entry((dg_presdl_entry){"", DG_PRESDL_BUTTON, .Complement.Button = {.Label = label, .OnClick = fn, .UserData = ud}})
#undef TEXT   // windows.h's TEXT() string macro: unused here, and the window's text field takes the name
#define TEXT(label, buf, hint) \
    add_entry((dg_presdl_entry){label, DG_PRESDL_TEXT, \
                                .Complement.Text = {.Buffer = buf, .BufSize = sizeof(buf), .Placeholder = hint}})
#define CHECKBOX(label, value, help) \
    add_entry((dg_presdl_entry){label, DG_PRESDL_CHECKBOX, .Complement.Checkbox = {.Value = value, .HelpText = help}})
#define DROPDOWN(label, options, selected) \
    add_entry((dg_presdl_entry){label, DG_PRESDL_DROPDOWN, \
                                .Complement.Dropdown = {.Options = options, .OptionCount = SDL_arraysize(options), \
                                                        .Selected = selected}})

static void section_begin(void) { section_first = entry_count; }

static ui_section *section_end(int page, const char *title, bool *visible)
{
    if (section_count == MAX_SECTIONS) {
        SDL_Log("installer: too many window sections");
        return &all_sections[MAX_SECTIONS - 1];
    }
    ui_section *s = &all_sections[section_count++];
    *s = (ui_section){.s = {.Title = title, .EntryStart = section_first, .EntryCount = entry_count - section_first},
                      .page = page, .visible = visible};
    return s;
}

// Paragraph text: one DG_PRESDL_PARAGRAPH entry, which preSDL word-wraps by the measured width of the real font
// (dg_presdl_text_width, as Mercurio's info window lays out its text) and re-flows when the window is resized.
static void add_text_block(int page, const char *title, const char *text, bool *visible)
{
    section_begin();
    add_entry((dg_presdl_entry){text, DG_PRESDL_PARAGRAPH});
    section_end(page, title, visible);
}

static const char SIMPLE_HELLO[] =
    "Hello! This is the 114360 Launcher Installer: it sets up FFXI and the 114360 launcher on your Xbox 360, or in "
    "Xenia, so you can play and run custom data packs and client modifications.\n"
    "As a heads up, you'll need a real FFXI Xbox 360 disc.\n"
    "A Simple install downloads a ready-made install from a Secret Key someone gave you. Installing from your own "
    "disc and patch sets instead? Choose Regular install at the top.";
static const char REGULAR_HELLO[] =
    "Hello! This is the 114360 Launcher Installer: it sets up FFXI and the 114360 launcher on your Xbox 360, or in "
    "Xenia, so you can play and run custom data packs and client modifications.\n"
    "A Regular install builds everything from your own disc and patch sets. Someone gave you a Secret Key instead? "
    "Choose Simple install at the top.";
static const char REGULAR_TEXT[] =
    "Go through the tabs from left to right: Game files, PlayOnline, then Install, where Check shows what will be "
    "installed before anything is written. Advanced has the extra options, and makes Secret Keys for a Simple "
    "install. Log shows what the installer is doing as it happens.";
static const char NEEDS_TEXT[] =
    "XB2-4016 (OpenLobby), the minimum: any Final Fantasy XI 360 disc, and the 40160203_0 Xbox patch set.\n"
    "PS2-2010 Recreation (Project Crystal servers): the Final Fantasy XI 360 Ultimate Collection 2010 disc, the "
    "40160203_0 Xbox patch set, and the 20100904_2 PS2 patch set.\n"
    "PC-2026 Recreation (current LSB): an internet connection to download from Square Enix's servers, or a local "
    "game install. (NOT FINISHED)";
static const char KEY_TEXT[] =
    "1. Paste your Secret Key below and press Check key: it shows what the key installs and how much it downloads.\n"
    "2. Press Continue, and answer the questions on the Install tab.\n"
    "Only use Secret Keys from people you trust. Keys from the 114360 project show as SIGNED.";
static const char HELP_TEXT[] =
    "Something went wrong? Press Copy report and send it to me on Discord or by email, miguel@crinalesystems.com, "
    "along with file-114360-installer.log.";
static const char AFTER_TEXT[] =
    "LISTEN! Now use the launcher's default.xex and watch for any STORAGE errors. You can type \\errors or /errors "
    "in PlayOnline's chat to see anything else. Give it a solid 15-20 seconds if it's in progress.";

// ── Credits: 114360-installer -credits prints who made what and every license in full (the zips ship no license
// files, and these licenses ask for their text to come with the program). ─────────────────────────────────────────

static const char CREDITS[] =
    "114360 Installer and Launcher\n"
    "Copyright 2026 Miguel Dias De Freitas (Migzy) <miguel@crinalesystems.com>\n"
    "https://github.com/crinalesystems/114360\n"
    "\n"
    "The launcher (payload/default.xex) contains code from Project Crystal Server, reworked from C# into C:\n"
    "  Project Crystal Server - GNU Affero General Public License 3.0\n"
    "  https://github.com/Project-Crystal-Server/Project-Crystal-Server\n"
    "It also uses:\n"
    "  microui, by rxi - MIT License\n"
    "  X.Org misc-fixed fonts 7x13 and 9x15 - public domain\n"
    "\n"
    "The installer uses:\n"
    "  DragonGuilt PreSDL, by Miguel Dias De Freitas - BSD 3-Clause License\n"
    "  SDL 3 - zlib License\n"
    "  FreeType - FreeType License\n"
    "  zlib - zlib License\n"
    "  libpng and bzip2 (the Windows build) - their own licenses\n"
    "  Monocypher 4.0.2, by Loup Vaillant - BSD 2-Clause License or CC0 1.0\n"
    "  Noto Sans (payload/font.ttf) - SIL Open Font License 1.1\n"
    "  From the system, not included: libcurl and Fontconfig on Linux, WinHTTP on Windows.\n"
    "\n"
    "FINAL FANTASY XI and PlayOnline are registered trademarks of Square Enix Co., Ltd. This project is not\n"
    "affiliated with or endorsed by Square Enix. No game data is included.\n";

static const unsigned char LIC_AGPL[] = {
#embed "../../licenses/AGPL-3.0.txt"
};
static const unsigned char LIC_MICROUI[] = {
#embed "../../licenses/microui-MIT.txt"
};
static const unsigned char LIC_X11[] = {
#embed "../../licenses/X11-fonts.txt"
};
static const unsigned char LIC_PRESDL[] = {
#embed "../third_party/presdl/LICENSE"
};
static const unsigned char LIC_SDL3[] = {
#embed "../../licenses/SDL3-zlib.txt"
};
static const unsigned char LIC_FREETYPE[] = {
#embed "../../licenses/FreeType-FTL.txt"
};
static const unsigned char LIC_ZLIB[] = {
#embed "../../licenses/zlib.txt"
};
static const unsigned char LIC_LIBPNG[] = {
#embed "../../licenses/libpng.txt"
};
static const unsigned char LIC_BZIP2[] = {
#embed "../../licenses/bzip2.txt"
};
static const unsigned char LIC_MONOCYPHER[] = {
#embed "../third_party/monocypher/LICENSE"
};
static const unsigned char LIC_NOTO[] = {
#embed "../../licenses/NotoSans-OFL-1.1.txt"
};

static int print_credits(void)
{
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {   // a windowed program: print in the console it was started from
        freopen("CONOUT$", "w", stdout);
    }
#endif
    static const struct {
        const char *name;
        const unsigned char *text;
        size_t len;
    } LICENSES[] = {
        {"Project Crystal Server: GNU Affero General Public License 3.0", LIC_AGPL, sizeof(LIC_AGPL)},
        {"microui: MIT License", LIC_MICROUI, sizeof(LIC_MICROUI)},
        {"X.Org misc-fixed fonts", LIC_X11, sizeof(LIC_X11)},
        {"DragonGuilt PreSDL: BSD 3-Clause License", LIC_PRESDL, sizeof(LIC_PRESDL)},
        {"SDL 3: zlib License", LIC_SDL3, sizeof(LIC_SDL3)},
        {"FreeType: FreeType License", LIC_FREETYPE, sizeof(LIC_FREETYPE)},
        {"zlib: zlib License", LIC_ZLIB, sizeof(LIC_ZLIB)},
        {"libpng", LIC_LIBPNG, sizeof(LIC_LIBPNG)},
        {"bzip2", LIC_BZIP2, sizeof(LIC_BZIP2)},
        {"Monocypher: BSD 2-Clause License or CC0 1.0", LIC_MONOCYPHER, sizeof(LIC_MONOCYPHER)},
        {"Noto Sans: SIL Open Font License 1.1", LIC_NOTO, sizeof(LIC_NOTO)},
    };
    fputs(CREDITS, stdout);
    fputs("\nThe licenses in full follow.\n", stdout);
    for (size_t i = 0; i < SDL_arraysize(LICENSES); i++) {
        printf("\n==================== %s ====================\n\n", LICENSES[i].name);
        fwrite(LICENSES[i].text, 1, LICENSES[i].len, stdout);
        if (LICENSES[i].len && LICENSES[i].text[LICENSES[i].len - 1] != '\n') {
            fputc('\n', stdout);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && (strcmp(argv[1], "-credits") == 0 || strcmp(argv[1], "--credits") == 0)) {
        return print_credits();
    }
    const char *base = SDL_GetBasePath();
    SDL_strlcpy(base_dir, base ? base : ".", sizeof(base_dir));
    join(payload_xex, base_dir, "payload/default.xex");
    state.lock = SDL_CreateMutex();
    log_open(base);
    simple_set_log(sim_log, nullptr);
    if (argc > 1 && argv[1][0] != '-') {
        return command_line(argc, argv);
    }
    ui.download_polcore = true;
    ui.download_polcore2010 = true;
    ui.update_pol = true;
    ui.share = true;
    SDL_strlcpy(ui.ftp_user, "xboxftp", sizeof(ui.ftp_user));
    SDL_strlcpy(ui.ftp_password, "xboxftp", sizeof(ui.ftp_password));
    SDL_strlcpy(ui.ftp_dir, "/Hdd1/114360", sizeof(ui.ftp_dir));

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    dg_presdl_preconfig_window win = {0};
    if (dg_presdl_init(&win, "114360 Installer - FINAL FANTASY XI for Xbox 360 / Xenia", 960, 820) != 0) {
        return 1;
    }
    main_window = win.Window;
    // The bundled Noto Sans (payload/font.ttf), so Windows (no Fontconfig) and Linux look the same; Linux falls
    // back to the system sans preSDL found, Windows to the debug font.
    char font[PATH_MAX_];
    join(font, base_dir, "payload/font.ttf");
    if (!dg_presdl_font_set_file(&win, font, 18)) {
        SDL_Log("no %s; keeping the default font", font);
    }
    SDL_Texture *logo = nullptr;
    SDL_IOStream *logo_io = SDL_IOFromConstMem(LOGO_PNG, sizeof(LOGO_PNG));
    SDL_Surface *logo_surface = logo_io ? SDL_LoadPNG_IO(logo_io, true) : nullptr;
    if (logo_surface) {
        logo = SDL_CreateTextureFromSurface(win.Renderer, logo_surface);
        SDL_DestroySurface(logo_surface);
    }
    if (!logo) {
        ilog("logo: %s", SDL_GetError());
    }

    static const char *HOWS[] = {"USB drive", "FTP (the console's FTP server)", "Xbox 360 Neighborhood",
                                 "This computer first (Xenia)"};
    static const char *POL_SOURCES[] = {"Install from SE (PlayOnline's update server)", "Install from Local"};
    static bool not_simple, mode_simple, mode_regular, sim_log, pol_se, pol_local, sim_ftp, sim_folder, reg_ftp,
        reg_folder, sim_step[5], sim_checked_ok, sim_after, gen_made[2];

    // The install type, above the tabs (preSDL's TopBar): Simple shows Info and Install, Regular every page.
    static const char *MODES[] = {"Simple install - with a Secret Key someone gave you",
                                  "Regular install - from your own disc and patch sets"};
    uint32_t mode_entry = DROPDOWN("Install type", MODES, &install_mode);

    // 1. Info
    add_text_block(PAGE_INFO, "Hello!", SIMPLE_HELLO, &mode_simple);
    add_text_block(PAGE_INFO, "Hello!", REGULAR_HELLO, &mode_regular);
    add_text_block(PAGE_INFO, "What you need", NEEDS_TEXT, &mode_regular);
    add_text_block(PAGE_INFO, "How it goes", REGULAR_TEXT, &mode_regular);
    section_begin();
    BUTTON("Next: Game files", on_regular_next, nullptr);
    section_end(PAGE_INFO, nullptr, &mode_regular);
    add_text_block(PAGE_INFO, "Secret Key", KEY_TEXT, &mode_simple);
    section_begin();
    TEXT("Secret Key", sim.key, "Please put base64 key here.");
    BUTTON("Check key", on_check_key, nullptr);
    for (int i = 0; i < SIM_LINES; i++) {
        LABEL(sim.info[i]);
    }
    ui_section *key_section = section_end(PAGE_INFO, nullptr, &mode_simple);   // shows only the filled info lines
    section_begin();
    BUTTON("Continue", on_simple_install, nullptr);
    section_end(PAGE_INFO, nullptr, &mode_simple);
    add_text_block(PAGE_INFO, "Help", HELP_TEXT, nullptr);

    // 2. Game files
    section_begin();
    TEXT("Work location", ui.work, "where files are processed (empty: 114360-work next to the installer)");
    BUTTON("Browse...", browse_folder, ui.work);
    BUTTON("Check", on_check_work, nullptr);
    LABEL(note[NOTE_WORK]);
    section_end(PAGE_FILES, "Work Location", nullptr);
    section_begin();
    TEXT("XB2-40160203_0 patch set", ui.root2016, "folder with 0001\\ (FFXiMain.dll, ROM\\ ...)");
    BUTTON("Browse...", browse_folder, ui.root2016);
    BUTTON("Check", on_check_2016, nullptr);
    LABEL(note[NOTE_2016]);
    TEXT("FFXI-360 disc ISO (or Xbox PlayOnline folder)", ui.iso,
         "for the PlayOnline files not in the patches");
    BUTTON("Browse disc image...", browse_iso, ui.iso);
    BUTTON("Browse PlayOnline folder...", browse_folder, ui.iso);
    BUTTON("Check", on_check_iso, nullptr);
    LABEL(note[NOTE_ISO]);
    section_end(PAGE_FILES, "2016 Install", nullptr);
    section_begin();
    TEXT("20100904_2 PS2 patch set", ui.root_ps2, "folder with ROM\\ and FTABLE.DAT");
    BUTTON("Browse...", browse_folder, ui.root_ps2);
    BUTTON("Check", on_check_ps2, nullptr);
    LABEL(note[NOTE_PS2]);
    section_end(PAGE_FILES, "2010 Install", nullptr);
    section_begin();
    TEXT("FFXI-360 compatible legacy DAT files", ui.root2026, "(not finished)");
    BUTTON("Browse...", browse_folder, ui.root2026);
    BUTTON("Check", on_check_2026, nullptr);
    LABEL(note[NOTE_2026]);
    section_end(PAGE_FILES, "2026 Treated Game Files (NOT FINISHED)", nullptr);

    // 3. PlayOnline
    section_begin();
    DROPDOWN("PlayOnline patch set", POL_SOURCES, &pol_source);
    section_end(PAGE_POL, "PlayOnline Patch Set", nullptr);
    section_begin();
    CHECKBOX("PolCore 20110726_A", &ui.download_polcore, "the PlayOnline the launcher runs");
    CHECKBOX("PolCore " POLCORE_2010 " (Project Crystal)", &ui.download_polcore2010,
             "kept in pol\\X2U-20100601_1 for Project Crystal's era");
    CHECKBOX("Update the disc's POL (never the goodbye build)", &ui.update_pol,
             "the PlayOnline files changed after the disc, up to 20110726_A");
    section_end(PAGE_POL, "Online: from Square Enix", &pol_se);
    section_begin();
    TEXT("PolCore.xex", ui.polcore, "PlayOnline 20110726_A");
    BUTTON("Browse...", browse_xex, ui.polcore);
    BUTTON("Check", on_check_polcore, nullptr);
    LABEL(note[NOTE_POLCORE]);
    TEXT("area.dat", ui.area, "empty: the default (region 1) is written");
    BUTTON("Browse...", browse_any, ui.area);
    BUTTON("Check", on_check_area, nullptr);
    LABEL(note[NOTE_AREA]);
    section_end(PAGE_POL, "Offline: your own files", &pol_local);

    // 4. Advanced
    section_begin();
    CHECKBOX("Direct Install", &direct_install,
             "By default 114360 reuses files across versions to save space on your Xbox: that is why the install "
             "is ~11 GB, not 22 or 33 GB. Direct makes plain folders instead.");
    CHECKBOX("Include empty Xbox files from POL (likely to break real hardware)", &ui.keep_empty,
             "Random data and cache files that come by default. Neighborhood doesn't like them.");
    CHECKBOX("Include too-long Xbox files from POL (WILL BREAK)", &ui.keep_long,
             "Some Xbox POL music files: FATX drives take names of at most 42 characters.");
    section_end(PAGE_ADVANCED, "Advanced", nullptr);
    section_begin();
    CHECKBOX("Use 2010 UC Data for 2010 install", &ui.use_uc2010, "the Ultimate Collection 2010 disc's data");
    TEXT("2010 UC data", ui.root_uc2010, "folder with 0001\\ (from the Ultimate Collection disc)");
    BUTTON("Browse...", browse_folder, ui.root_uc2010);
    BUTTON("Check", on_check_uc, nullptr);
    LABEL(note[NOTE_UC]);
    CHECKBOX("Use 2011 PC Data for 2010 install", &ui.use_pc2011, "the January 2011 PC data");
    TEXT("2011 PC data", ui.root_pc2011, "folder with ROM\\ (the PC install's FINAL FANTASY XI folder)");
    BUTTON("Browse...", browse_folder, ui.root_pc2011);
    BUTTON("Check", on_check_pc, nullptr);
    LABEL(note[NOTE_PC]);
    TEXT("2010 data already converted (optional)", ui.root2010, "a September 2010 revision in 360 format");
    BUTTON("Browse...", browse_folder, ui.root2010);
    section_end(PAGE_ADVANCED, "2010 data sources", nullptr);
    add_text_block(PAGE_ADVANCED, "Generate Secret Key",
                   "Build a hosted install from the files on Game files, then upload it to Google Drive and make the "
                   "key people paste in a Simple install.", nullptr);
    section_begin();
    CHECKBOX("2016", &gen_2016, nullptr);
    CHECKBOX("2010", &gen_2010, nullptr);
    CHECKBOX("2026 (not finished: not included)", &gen_2026, nullptr);
    CHECKBOX("POOL INSTALL", &gen_pool, nullptr);
    CHECKBOX("DIRECT INSTALL (can be both)", &gen_direct, nullptr);
    BUTTON("Build hosted install", on_generate, nullptr);
    section_end(PAGE_ADVANCED, nullptr, nullptr);
    section_begin();
    TEXT("Pooled: Drive folder link", gen.link[0], "https://drive.google.com/drive/folders/...");
    BUTTON("Make key (pooled)", on_make_key, (void *)(intptr_t)0);
    LABEL(gen.key[0]);
    section_end(PAGE_ADVANCED, nullptr, &gen_made[0]);
    section_begin();
    TEXT("Direct: Drive folder link", gen.link[1], "https://drive.google.com/drive/folders/...");
    BUTTON("Make key (direct)", on_make_key, (void *)(intptr_t)1);
    LABEL(gen.key[1]);
    section_end(PAGE_ADVANCED, nullptr, &gen_made[1]);

    // 5. Install: the Simple wizard...
    section_begin();
    BUTTON("OK!", on_sim_ready, nullptr);
    section_end(PAGE_INSTALL, "Hi, ready?", &sim_step[0]);
    section_begin();
    DROPDOWN("Bring it over with", HOWS, &how);
    BUTTON("Next", on_sim_how, nullptr);
    section_end(PAGE_INSTALL, "What are you using to bring it to the Xbox?", &sim_step[1]);
    section_begin();
    LABEL(sim.where_text);
    TEXT("Install to", ui.dest, "the USB drive, the console folder, or a folder on this computer");
    BUTTON("Browse...", browse_folder, ui.dest);
    section_end(PAGE_INSTALL, "Where?", &sim_folder);
    section_begin();
    LABEL(sim.where_text);
    TEXT("Console address", ui.ftp_host, "IP address, e.g. 192.168.1.50 (or ip:port)");
    TEXT("FTP user", ui.ftp_user, "xboxftp");
    TEXT("FTP password", ui.ftp_password, "xboxftp");
    TEXT("Folder on the console", ui.ftp_dir, "/Hdd1/114360");
    section_end(PAGE_INSTALL, "Where?", &sim_ftp);
    section_begin();
    BUTTON("Continue", on_sim_where, nullptr);
    section_end(PAGE_INSTALL, nullptr, &sim_step[2]);
    section_begin();
    for (int i = 0; i < SIM_LINES; i++) {
        LABEL(sim.lines[i]);
    }
    ui_section *sim_lines = section_end(PAGE_INSTALL, "Checking", &sim_step[3]);
    section_begin();
    BUTTON("[ INSTALL ]", on_sim_install, nullptr);
    section_end(PAGE_INSTALL, "Okay ready! Press Install and see its progress!", &sim_checked_ok);
    add_text_block(PAGE_INSTALL, "After the install", AFTER_TEXT, &sim_after);
    // ...or the Regular install.
    section_begin();
    DROPDOWN("Install method", HOWS, &how);
    section_end(PAGE_INSTALL, "Install Method", &not_simple);
    section_begin();
    TEXT("Install to", ui.dest, "a folder, a USB drive, or the folder you copy with Neighborhood");
    BUTTON("Browse...", browse_folder, ui.dest);
    section_end(PAGE_INSTALL, nullptr, &reg_folder);
    section_begin();
    TEXT("Console address", ui.ftp_host, "IP address, e.g. 192.168.1.50 (or ip:port)");
    TEXT("FTP user", ui.ftp_user, "xboxftp");
    TEXT("FTP password", ui.ftp_password, "xboxftp");
    TEXT("Folder on the console", ui.ftp_dir, "/Hdd1/114360");
    section_end(PAGE_INSTALL, "FTP login", &reg_ftp);
    section_begin();
    BUTTON("Check", on_check, nullptr);
    BUTTON("Install", on_install, nullptr);
    section_end(PAGE_INSTALL, nullptr, &not_simple);
    section_begin();
    for (int i = 0; i < MAX_MESSAGES; i++) {
        LABEL(shown[i]);
    }
    ui_section *results = section_end(PAGE_INSTALL, "Check results", &not_simple);
    // Progress and report, on every page.
    static char log_line[PATH_MAX_ + 16];
    snprintf(log_line, sizeof(log_line), "Log: %s", log_path);
    for (int page = 0; page < PAGE_COUNT; page++) {
        section_begin();
        LABEL(status_line);
        BUTTON("Copy report", on_copy_report, nullptr);
        BUTTON("Cancel", on_cancel, nullptr);
        LABEL(log_line);
        section_end(page, "Progress", nullptr);
    }
    // The live log: its own tab in a Regular install, under the questions on the Install tab in a Simple one.
    static char live_text[LOG_LIVE * 260];
    static uint32_t live_seq;
    add_text_block(PAGE_LOG, "Live log (newest last)", live_text, nullptr);
    add_text_block(PAGE_INSTALL, "Live log (newest last)", live_text, &sim_log);

    static dg_presdl_section page_sections[PAGE_COUNT][MAX_SECTIONS];
    dg_presdl_page pages[PAGE_COUNT] = {
        {"1. Info", logo, 0, page_sections[0], 0},
        {"2. Game files", nullptr, 0, page_sections[1], 0},
        {"3. PlayOnline", nullptr, 0, page_sections[2], 0},
        {"4. Advanced", nullptr, 0, page_sections[3], 0},
        {"5. Install", nullptr, 0, page_sections[4], 0},
        {"6. Log", nullptr, 0, page_sections[5], 0},
    };
    win.Entries = entries;
    win.EntryCount = entry_count;
    win.TopBar = &entries[mode_entry];
    ilog("window: %u of %d entries, %d of %d sections", entry_count, MAX_ENTRIES, section_count, MAX_SECTIONS);
    // The tabs each mode shows (PAGE_* order), and their titles.
    static const int SIMPLE_VIEW[] = {PAGE_INFO, PAGE_INSTALL}, REGULAR_VIEW[] = {PAGE_INFO, PAGE_FILES, PAGE_POL,
                                                                                  PAGE_ADVANCED, PAGE_INSTALL,
                                                                                  PAGE_LOG};
    static const char *SIMPLE_TITLES[] = {"1. Info", "2. Install"};
    dg_presdl_page view[PAGE_COUNT];

    while (!win.ShouldClose) {
        SDL_LockMutex(state.lock);
        int n = have_plan ? checked.message_count : 0;
        for (int i = 0; i < n; i++) {
            if (checked.codes[i]) {
                snprintf(shown[i], sizeof(shown[i]), "[ZZ%03d] %s", checked.codes[i], checked.messages[i]);
            } else {
                snprintf(shown[i], sizeof(shown[i]), "[ok] %s", checked.messages[i]);
            }
        }
        results->s.EntryCount = (uint32_t)n;
        key_section->s.EntryCount = (uint32_t)(2 + sim.info_count);   // the key field, Check key, the info lines
        sim_lines->s.EntryCount = (uint32_t)sim.line_count;
        sim.chosen = install_mode == MODE_SIMPLE;     // the Install page runs the wizard in Simple mode
        not_simple = mode_regular = !sim.chosen;
        mode_simple = sim.chosen;
        sim_log = sim.chosen && (state.busy || sim.step >= 3);   // only once the wizard is checking or installing
        for (int i = 0; i < 5; i++) {
            sim_step[i] = sim.chosen && sim.step >= i;
        }
        sim_checked_ok = sim.chosen && sim.step >= 3 && sim.check_ok && !sim.done;
        sim_after = sim.chosen && sim.done;
        sim_ftp = sim.chosen && sim.step >= 2 && how == HOW_FTP;
        sim_folder = sim.chosen && sim.step >= 2 && how != HOW_FTP;
        reg_ftp = !sim.chosen && how == HOW_FTP;
        reg_folder = !sim.chosen && how != HOW_FTP;
        pol_se = pol_source == POL_FROM_SE;
        pol_local = pol_source == POL_FROM_LOCAL;
        gen_made[0] = gen.made[0];
        gen_made[1] = gen.made[1];
        snprintf(sim.where_text, sizeof(sim.where_text), "%s",
                 how == HOW_FTP ? "Your console's FTP server (Aurora or DashLaunch; xboxftp / xboxftp by default)."
                 : how == HOW_XENIA ? "Pick the folder Xenia will run the launcher from."
                 : "Okay, please browse DIRECTLY from USB or Neighbourhood to your XBOX360, ensure it's awake.");
        if (state.busy && state.current[0]) {
            snprintf(status_line, sizeof(status_line), "%3d%%  %s", (int)(state.fraction * 100), state.current);
        } else if (state.busy) {
            snprintf(status_line, sizeof(status_line), "working...");
        } else {
            snprintf(status_line, sizeof(status_line), "%s",
                     state.result[0] ? state.result
                     : sim.chosen    ? "Ready. Paste your Secret Key on the Info tab."
                                     : "Ready. Fill in the tabs from left to right.");
        }
        SDL_UnlockMutex(state.lock);
        // Each page shows its visible sections.
        log_live_text(live_text, sizeof(live_text), &live_seq);   // the live log entries show live_text
        for (int p = 0; p < PAGE_COUNT; p++) {
            pages[p].SectionCount = 0;
        }
        for (int i = 0; i < section_count; i++) {
            ui_section *s = &all_sections[i];
            if (!s->visible || *s->visible) {
                dg_presdl_page *pg = &pages[s->page];
                page_sections[s->page][pg->SectionCount++] = s->s;
            }
        }
        pages[PAGE_INFO].HeaderImageH = logo ? win.W / 4 : 0;
        // The mode's tabs: copies of its pages (scroll state copied back after the frame), the shown page kept
        // as a PAGE_* so switching modes stays on it when the new mode has it, else goes to Info.
        const int *map = sim.chosen ? SIMPLE_VIEW : REGULAR_VIEW;
        uint32_t count = sim.chosen ? SDL_arraysize(SIMPLE_VIEW) : SDL_arraysize(REGULAR_VIEW);
        win.CurrentPage = 0;
        for (uint32_t k = 0; k < count; k++) {
            view[k] = pages[map[k]];
            if (sim.chosen) {
                view[k].Title = SIMPLE_TITLES[k];
            }
            if (map[k] == current_page) {
                win.CurrentPage = k;
            }
        }
        win.Pages = view;
        win.PageCount = count;
        dg_presdl_frame(&win);
        for (uint32_t k = 0; k < count; k++) {
            pages[map[k]].ScrollY = view[k].ScrollY;
            pages[map[k]].ContentH = view[k].ContentH;
        }
        current_page = map[win.CurrentPage < count ? win.CurrentPage : 0];
#ifdef INSTALLER_SNAPSHOTS
        // Layout check builds only (-DINSTALLER_SNAPSHOTS): after the first frames, render every tab of the mode
        // (INSTALLER_SNAPSHOT_MODE=regular for Regular) to INSTALLER_SNAPSHOTS/page-N.png and quit (a window behind
        // others cannot be screenshotted reliably). The readback returns the frame before the one presented, so
        // the shot taken while tab N+1 shows is tab N; one extra round takes the last.
        static int frames;
        const char *snap_dir = SDL_getenv("INSTALLER_SNAPSHOTS");
        if (snap_dir && ++frames >= 10) {
            const char *snap_mode = SDL_getenv("INSTALLER_SNAPSHOT_MODE");
            install_mode = snap_mode && strcmp(snap_mode, "regular") == 0 ? MODE_REGULAR : MODE_SIMPLE;
            int page = (frames - 10) / 12;
            if (page > (int)win.PageCount) {
                win.ShouldClose = true;
            } else {
                if (page < (int)win.PageCount) {
                    current_page = map[page];
                }
                if (page > 0 && (frames - 10) % 12 == 0) {
                    SDL_Surface *shot = SDL_RenderReadPixels(win.Renderer, nullptr);
                    char out[PATH_MAX_];
                    snprintf(out, sizeof(out), "%s/page-%d.png", snap_dir, page);
                    if (shot) {
                        SDL_SavePNG(shot, out);
                        SDL_DestroySurface(shot);
                    }
                }
            }
        }
#endif
        SDL_Delay(16);
    }
    SDL_SetAtomicInt(&state.cancel, 1);
    stop_flag = true;
    dg_presdl_shutdown(&win);
    SDL_Quit();
    ilog("---- closed ----");
    return 0;
}
