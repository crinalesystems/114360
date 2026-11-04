/* 114360 launcher: start the 2016 FFXI client through PlayOnline's core without the PlayOnline Viewer.

   Retail chain (docs/ffxi-main-2016.md): the Viewer sets launch data {product 1} and relaunches
   GameExecContent0001.xex. That loads PolCore and calls its ordinal 7. PolCore reads the launch data, finds
   BOOT:FFXi.dll in the product's CONFIG.SYS on drive "000100:", loads the game image, and calls its
   ordinal 2 with PolCore's interface table. This launcher plays GameExec's part:

       game:\default.xex                   this launcher
       game:\launcher.ini                  options and server presets (see load_config)
       game:\area.dat, game:\PlayOnline\   from the disc's PlayOnline folder, shared by all revisions
       game:\rev\<revision>\PolCore.xex    the PlayOnline core of that revision (2016: 20110726_A)
       game:\rev\<revision>\0001\          its FFXI data + FFXi.dll + CONFIG.SYS (ID:1, BOOT:FFXi.dll)

   "000100:" and the other FFXI drives are normally content packages. Here every FFXI content drive is
   linked to the chosen revision's 0001 folder (content_create). */
#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>

#include "engine.h"
#include "menu.h"
#include "md5.h"
#include "polnet.h"
#include "polreg.h"
#include "patches.h"
typedef struct _ANSI_STRING_X {
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} ANSI_STRING_X;

NTSYSAPI LONG NTAPI ObCreateSymbolicLink(ANSI_STRING_X *link, ANSI_STRING_X *device);
NTSYSAPI LONG NTAPI ObDeleteSymbolicLink(ANSI_STRING_X *link);
NTSYSAPI LONG NTAPI XexLoadImage(const char *name, DWORD flags, DWORD min_version, HANDLE *module);
NTSYSAPI LONG NTAPI XexGetProcedureAddress(HANDLE module, DWORD ordinal, PVOID *address);
NTSYSAPI LONG NTAPI XexGetModuleHandle(const char *name, HANDLE *module);
ULONG __cdecl DbgPrint(const char *format, ...);
#include <stdarg.h>

/* Every DbgPrint line also goes to game:\launcher.log (flushed per line), since a retail console shows debug
   output nowhere: after a crash on hardware, the file's last line shows how far it got. A crashed game that comes
   back to the launcher starts it again, so the earlier logs are kept: at each start launcher.log becomes
   launcher.1.log, launcher.1.log becomes launcher.2.log, and so on up to LOG_KEEP. */
#define LOG_KEEP 4
static FILE *launcher_log;

static void rotate_logs(void)
{
    char from[32], to[32];
    int i;
    _snprintf(to, sizeof(to), "game:\\launcher.%d.log", LOG_KEEP);
    DeleteFileA(to);
    for (i = LOG_KEEP - 1; i >= 0; i--) {
        if (i == 0) {
            strcpy(from, "game:\\launcher.log");
        } else {
            _snprintf(from, sizeof(from), "game:\\launcher.%d.log", i);
        }
        _snprintf(to, sizeof(to), "game:\\launcher.%d.log", i + 1);
        MoveFileA(from, to);
    }
}

static void launcher_print(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    line[sizeof(line) - 1] = '\0';
    DbgPrint("%s", line);
    if (launcher_log == NULL) {
        rotate_logs();
        launcher_log = fopen("game:\\launcher.log", "w");
    }
    if (launcher_log != NULL) {
        fputs(line, launcher_log);
        fflush(launcher_log);
    }
}
#define DbgPrint launcher_print


/* XexLoadImage flags: 0x8 = DLL, 0x1 = into the title process. PolCore loads FFXi.dll with 0x9 (Xenia log). With
   0x8 alone consoles refused PolCore with C0000022 (RGH and Peer Pressure, even the genuine PolCore inside the
   console's own PlayOnline package); Xenia accepts either. */
#define XEX_LOADIMG_FLAG_TITLE_DLL 0x00000009
#define POLCORE_ORDINAL_MAIN 7
#define FFXI_PRODUCT_ID 1
/* Device folder behind game: (the launcher's own folder), as a kernel path for symbolic links: Xenia's host
   folder is the partition root, a console's is wherever Aurora or the file manager started default.xex
   (\Device\Harddisk0\Partition1\Games\FFXI, \Device\Mass0\FFXI, ...). find_game_device reads it from the
   launcher's loader entry; when that has no \Device\ path (Xenia's has no name at all) the default stays. */
static char game_device[128] = "\\Device\\Harddisk0\\Partition1";

/* The launcher's loader entry (LDR_DATA_TABLE_ENTRY; GetModuleHandle(NULL) returns it): FullDllName, a
   UNICODE_STRING (u16 length in bytes, u16 maximum, wide buffer), at +0x24. Not yet seen on a console. */
static void find_game_device(void)
{
    const BYTE *entry = (const BYTE *)GetModuleHandle(NULL);
    const WCHAR *name = NULL;
    char path[sizeof(game_device)];
    int i, n = 0, cut = -1;
    path[0] = '\0';
    if (entry != NULL) {
        n = *(const WORD *)(entry + 0x24) / 2;
        name = *(const WCHAR *const *)(entry + 0x28);
    }
    if (name != NULL && n > 0 && n < (int)sizeof(path)) {
        for (i = 0; i < n; i++) {
            path[i] = name[i] < 0x80 ? (char)name[i] : '?';
            if (path[i] == '\\') {
                cut = i;
            }
        }
        path[n] = '\0';
    }
    DbgPrint("114360: launched as \"%s\" (loader entry %p)\n", path, entry);
    if (_strnicmp(path, "\\Device\\", 8) == 0 && cut > 8) {
        path[cut] = '\0';
        strcpy(game_device, path);
    }
    DbgPrint("114360: game: is %s\n", game_device);
}

/* PlayOnline launch data, 0x24 bytes, as PolCore ordinal 4 (0x82423B88) reads it. */
typedef struct _POL_LAUNCH_DATA {
    char name[0x10];      /* PolCore's default is "PlayOnlineViewer" */
    DWORD one;            /* +0x10: PolCore's default is 1 */
    BYTE flags[0x0C];     /* +0x14..+0x1F */
    DWORD product;        /* +0x20: 1 = FINAL FANTASY XI, 1000 = Viewer */
} POL_LAUNCH_DATA;

typedef int (*polcore_main_fn)(int argc, char **argv, void *reserved);

static void ansi(ANSI_STRING_X *s, const char *text)
{
    s->Buffer = (PCHAR)text;
    s->Length = (USHORT)strlen(text);
    s->MaximumLength = s->Length + 1;
}

/* Development trace: log what the game reports back to PlayOnline when it exits. FFXi.dll reports a normal
   exit through slot +0xF24 (code) and an error through slot +0x1008 (code, "%s(%d)" message) of PolCore's
   interface table (POLCORE_PROFILE.table). A watcher thread swaps both slots for logging wrappers once PolCore
   has filled them. */
static const POLCORE_PROFILE *polcore_profile;     /* chosen in main() from PolCore's time stamp */
static const FFXIMAIN_PROFILE *ffximain_profile;   /* set when FFXi.dll loads FFXiMain */
typedef int (*slot_f24_fn)(int code);
typedef int (*slot_1008_fn)(int code, const char *message);
static slot_f24_fn orig_f24;
static slot_1008_fn orig_1008;

static int direct_mode;   /* FFXI started without the Viewer: see direct_answer() below */
static int viewer_mode;   /* set in main(): this run starts the Viewer (product 1000) rather than FFXI */

/* Running under Xenia rather than on a console. Some socket fixes undo Xenia bugs (FIONREAD byte order,
   sockaddr byte order, recv end of stream); on hardware they would break the lobby (FIONREAD 0x27 read as
   654311424, FFXI-3101 on a Peer Pressure console, 2026-11-03). Told apart by the kernel version
   (XboxKrnlVersion, xboxkrnl ordinal 0x158: u16 major, minor, build, qfe): Xenia reports build 1888
   (its kernel_build_version default), consoles today 17559. */
static int on_xenia;

static void detect_xenia(void)
{
    HANDLE kernel = NULL;
    const USHORT *version = NULL;
    if (XexGetModuleHandle("xboxkrnl.exe", &kernel) >= 0
        && XexGetProcedureAddress(kernel, 0x158, (PVOID *)&version) >= 0 && version != NULL) {
        on_xenia = version[2] < 2000;
        DbgPrint("114360: kernel %u.%u.%u.%u: %s\n", version[0], version[1], version[2], version[3],
                 on_xenia ? "Xenia" : "console");
    } else {
        DbgPrint("114360: kernel version not found; assuming a console\n");
    }
}

/* Name the launcher puts in the launch data before restarting itself, so the restarted copy does a cold
   start (direct FFXI) instead of treating the data as a Viewer hand-off. */
#define RETRY_LAUNCH_NAME "114360-retry"

/* In a direct start there is no Viewer to go back to: restart the launcher instead, marked as a return so its
   menu waits (no autostart countdown). Any error dialog the game showed has been dismissed by then. */
static void back_to_launcher(const char *why)
{
    static char retry[0x24];
    strcpy(retry, RETRY_LAUNCH_NAME);
    XSetLaunchData(retry, sizeof(retry));
    DbgPrint("114360: direct start: %s; back to the launcher menu instead of the Viewer\n", why);
    XLaunchNewImage("game:\\default.xex", 0);   /* does not return */
}

/* When the game ends, PolCore returns to the PlayOnline Viewer through table+0xF24. Without a Viewer PolCore
   cannot find it ("PolViewer executable not found") and Xenia is left hanging. */
static int trace_f24(int code)
{
    DbgPrint("114360: game exit via table+0xF24 code=%d\n", code);
    if (direct_mode) {
        back_to_launcher("game exit");
    }
    return orig_f24(code);
}

/* PolCore's own title launches: the game's "back to PlayOnline" can start the Viewer directly
   (XamLoaderLaunchTitle, or XamContentLaunchImage for the Viewer inside its content package) instead of going
   through +0xF24. In a direct start both come back to the launcher. */
typedef DWORD (*launch_title_fn)(const char *path, DWORD flags);
typedef DWORD (*content_launch_fn)(DWORD user, const void *content, const char *path);
static launch_title_fn real_launch_title;
static content_launch_fn real_content_launch;

static DWORD launch_title_hook(const char *path, DWORD flags)
{
    DbgPrint("114360: PolCore launches \"%s\" (flags %X)\n", path ? path : "(null)", flags);
    if (direct_mode) {
        back_to_launcher("PolCore launches a title");
    }
    return real_launch_title(path, flags);
}

static DWORD content_launch_hook(DWORD user, const void *content, const char *path)
{
    DbgPrint("114360: PolCore launches \"%s\" from a content package\n", path ? path : "(null)");
    if (direct_mode) {
        back_to_launcher("PolCore launches a packaged title");
    }
    return real_content_launch(user, content, path);
}

static int trace_1008(int code, const char *message)
{
    DbgPrint("114360: game error via table+0x1008 code=%d message=\"%s\"\n", code, message ? message : "(null)");
    return orig_1008(code, message);
}

/* FFXiMain's pad poll (0x84103630) asks PlayOnline which controller port to read through slot +0x1834, then
   calls XInputGetState on it. The Viewer normally decides that port (the pad that pressed START). Without
   the Viewer, follow the player: the port of the pad that last had real input (a button, a trigger, or a
   stick past the XInput dead zone; sensor jitter does not count), starting with the first connected one. So
   any pad works, wherever SDL or the console put it (Xenia gave a PS4 pad port 0 and a virtual 360 pad
   port 1). */
typedef int (*slot_1834_fn)(void);
static slot_1834_fn orig_1834;
static int pad_port(void)
{
    static int logged, current = -1;
    int orig = orig_1834(), port;
    for (port = 0; port < XUSER_MAX_COUNT; port++) {
        XINPUT_STATE state;
        const XINPUT_GAMEPAD *g = &state.Gamepad;
        int active;
        if (XInputGetState(port, &state) != ERROR_SUCCESS) {
            continue;
        }
        active = g->wButtons != 0 || g->bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD
                 || g->bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD
                 || g->sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || g->sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
                 || g->sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || g->sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
                 || g->sThumbRX > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || g->sThumbRX < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE
                 || g->sThumbRY > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || g->sThumbRY < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
        if (current < 0 || (active && port != current)) {
            DbgPrint("114360: table+0x1834 (pad port) PolCore says %d, using port %d\n", orig, port);
            current = port;
        }
    }
    if (current < 0 && !logged) {
        logged = 1;
        DbgPrint("114360: table+0x1834 (pad port) PolCore says %d, no pad connected yet; using 0\n", orig);
    }
    return current < 0 ? 0 : current;
}

/* State 0xD of FFXiMain's lobby machine (0x841A44C8) fetches the PlayOnline credential through slot +0xEA0
   into a 0x90-byte buffer and copies its first 0x40 bytes into its first lobby packet (0x26, +0x34: the
   content-auth password; 0x841B92C0), where a server reads which PlayOnline sign-in is playing. It then takes
   0x10 bytes through slot +0xFAC (0x841B93E0, connection +0x10). A direct start has no login; when PolCore
   refuses, hand the game an empty credential (the bridge then falls back to its shared LSB account if
   FFXI_REQUIRE_SIGNED_IN=0).

   A preset with `credential = <file>` (under game:\) plays as a real PlayOnline sign-in instead, made on the
   PC by tools/pol-signin (which keeps that session open): +0xEA0 answers its content-auth password and +0xFAC
   its random value, the secret later lobby requests prove with MD5(random + key) (Project Crystal's lobby,
   Client.VerifyPassword). File: "POLC", u8 version 1, 3 bytes, char[8] PlayOnline ID, u8[16] random value,
   u8[64] content-auth password. */
#define POL_CREDENTIAL_SLOT 0xEA0
#define POL_RANDOM_SLOT 0xFAC
#define POL_CREDENTIAL_SIZE 0x90
#define POL_AUTH_SIZE 0x40
#define POL_RANDOM_SIZE 0x10
typedef int (*slot_buf_fn)(void *buf);
static slot_buf_fn orig_ea0, orig_fac;
static int have_signin;
static char signin_id[9];
static BYTE signin_random[POL_RANDOM_SIZE], signin_auth[POL_AUTH_SIZE];

/* Read game:\<name> (pol-signin's file) into signin_*. */
static void load_signin(const char *name)
{
    char path[MENU_TEXT + 16];
    BYTE data[0x60];
    FILE *f;
    _snprintf(path, sizeof(path), "game:\\%s", name);
    path[sizeof(path) - 1] = '\0';
    f = fopen(path, "rb");
    if (f == NULL || fread(data, sizeof(data), 1, f) != 1 || memcmp(data, "POLC", 4) != 0 || data[4] != 1) {
        DbgPrint("114360: no PlayOnline sign-in in %s (run tools/pol-signin first)\n", path);
        if (f != NULL) {
            fclose(f);
        }
        return;
    }
    fclose(f);
    memcpy(signin_id, data + 8, 8);
    signin_id[8] = '\0';
    memcpy(signin_random, data + 0x10, POL_RANDOM_SIZE);
    memcpy(signin_auth, data + 0x20, POL_AUTH_SIZE);
    have_signin = 1;
    DbgPrint("114360: playing as PlayOnline ID %s (signed in on the PC, %s)\n", signin_id, path);
}

/* The PlayOnline session signed in on the console (polirc.c over polnet.c): the menu signs in for a preset with
   a PlayOnline ID (pol_id/pol_password) before it starts, and from its chat page. The session's credential then
   answers +0xEA0/+0xFAC like pol-signin's file, and a thread keeps the session alive (PING) while the game runs:
   the server drops the credential when the connection goes. The sign-in server is the preset's address. */
static POL_SESSION pol_session;

static int menu_signin(const MENU_PRESET *p)
{
    unsigned a, b, c, d;
    if (pol_session.state == POL_ONLINE) {
        pol_signout(&pol_session);
    }
    if (sscanf(p->address, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
        _snprintf(pol_session.error, sizeof(pol_session.error), "needs an IPv4 server address");
        pol_session.state = POL_FAILED;
        return 0;
    }
    {
        /* The preset's PlayOnline version, else the one the game's PolCore is. */
        const POL_VERSION *v = pol_version_find(p->pol_version);
        int first = pol_session.log_count, i;
        if (v == NULL) {
            v = pol_version_find(POL_VERSION_DEFAULT);
        }
        if (!pol_signin(&pol_session, &pol_console_net, (a << 24) | (b << 16) | (c << 8) | d, v, p->pol_id,
                        p->pol_password)) {
            DbgPrint("114360: PlayOnline sign-in as %s on %s failed: %s\n", p->pol_id, p->address, pol_session.error);
            return 0;
        }
        if (pol_session.log_count - first > POL_LOG_LINES) {
            first = pol_session.log_count - POL_LOG_LINES;
        }
        for (i = first; i < pol_session.log_count; i++) {   /* the sign-in as the chat page shows it */
            DbgPrint("114360: %s\n", pol_session.log[i % POL_LOG_LINES]);
        }
    }
    DbgPrint("114360: signed in to PlayOnline as %s on %s, handle %s\n", pol_session.pol_id, p->address,
             pol_session.handle_name[0] ? pol_session.handle_name : "(none)");
    return 1;
}

/* /register on the chat page: a new PlayOnline ID through the preset's server's sign-up pages (polreg.c). */
static int menu_register(const MENU_PRESET *p, const char *handle, const char *password, const char *code,
                         char *pol_id, char *error, int error_size)
{
    unsigned a, b, c, d;
    int r;
    if (sscanf(p->address, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
        _snprintf(error, error_size, "needs an IPv4 server address");
        error[error_size - 1] = '\0';
        return 0;
    }
    r = pol_register(&pol_console_net, (a << 24) | (b << 16) | (c << 8) | d, handle, password, code, pol_id, error,
                     error_size);
    DbgPrint("114360: PlayOnline registration on %s: %s %s\n", p->address,
             r == 1 ? "made" : r == 2 ? "waiting for the mailed code" : "refused", r == 1 ? pol_id : error);
    return r;
}

/* /verify CODE: the mailed code of a registration that returned 2. */
static int menu_register_code(const char *code, char *pol_id, char *error, int error_size)
{
    int r = pol_register_code(&pol_console_net, code, pol_id, error, error_size);
    DbgPrint("114360: PlayOnline registration code: %s %s\n", r ? "made" : "refused", r ? pol_id : error);
    return r;
}

static DWORD WINAPI pol_keepalive(LPVOID unused)
{
    while (pol_poll(&pol_session, 1000)) {
    }
    DbgPrint("114360: PlayOnline session ended (%s)\n", pol_session.error[0] ? pol_session.error : "connection closed");
    return 0;
}

/* Take the credential from the console's session into signin_*. */
static void use_session(void)
{
    memcpy(signin_id, pol_session.pol_id, 8);
    signin_id[8] = '\0';
    memcpy(signin_random, pol_session.random, POL_RANDOM_SIZE);
    memcpy(signin_auth, pol_session.auth, POL_AUTH_SIZE);
    have_signin = 1;
    CreateThread(NULL, 0, pol_keepalive, NULL, 0, NULL);
    DbgPrint("114360: playing as PlayOnline ID %s (signed in on the console)\n", signin_id);
}

static int pol_credential(void *buf)
{
    int r = orig_ea0(buf);
    DbgPrint("114360: table+0xEA0 (PlayOnline credential) returned %d\n", r);
    if (have_signin && buf != NULL) {
        /* Only the first 0x40 bytes: the rest of PolCore's answer is state the lobby code still reads (zeroing
           all 0x90 bytes crashed it at 0x846F3CC0 while building 0x26). */
        memcpy(buf, signin_auth, POL_AUTH_SIZE);
        DbgPrint("114360: table+0xEA0 answered with %s's content-auth password\n", signin_id);
        r = 0;
    } else if (r < 0 && direct_mode && buf != NULL) {
        memset(buf, 0, POL_CREDENTIAL_SIZE);
        DbgPrint("114360: table+0xEA0 failed on a direct start; using an empty credential\n");
        r = 0;
    }
    return r;
}

/* +0xFAC fills a 0x10-byte buffer and returns its address. FFXiMain keeps those bytes at connection +0x10, and
   with a credential every later lobby request carries MD5(them + key) (0x841B9928 -> 0x841C8068), which Crystal's
   lobby checks against the sign-in's random value. Without a sign-in PolCore answers 00000000 0050000F FF...
   Keep its return value. */
static int pol_random(void *buf)
{
    int r = orig_fac(buf);
    if (have_signin && buf != NULL) {
        memcpy(buf, signin_random, POL_RANDOM_SIZE);
        DbgPrint("114360: table+0xFAC answered with %s's random value\n", signin_id);
    }
    return r;
}

/* +0xAC4 fills a 0x10-byte buffer whose first u32 is the current Unix time on PlayOnline's clock. FFXiMain's
   0x841CD3F0 reads it and 0x84308598 turns it into the year the title screen prints ("(c) 2002, <year>"). PolCore
   only knows that clock after its own PlayOnline sign-in, so a direct start, or a sign-in made by polirc.c or on
   the PC, gets 0: 1970 in UTC, 1969 behind UTC. When PolCore's answer is before 2016, use the console clock, and
   the session's login time if the clock is unset too. */
#define POL_TIME_SLOT 0xAC4
#define FILETIME_UNIX_EPOCH 116444736000000000ULL /* 1970-01-01 in 100 ns units since 1601-01-01 */
#define UNIX_TIME_2016 1451606400UL              /* a clock before the game's own build is unset */
static slot_buf_fn orig_ac4;

static int pol_time(void *buf)
{
    int r = orig_ac4(buf);
    DWORD *t = (DWORD *)buf;
    if (t != NULL && t[0] < UNIX_TIME_2016) {
        FILETIME ft;
        unsigned long long now;
        GetSystemTimeAsFileTime(&ft);
        now = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        if (now > FILETIME_UNIX_EPOCH && (now - FILETIME_UNIX_EPOCH) / 10000000 >= UNIX_TIME_2016) {
            t[0] = (DWORD)((now - FILETIME_UNIX_EPOCH) / 10000000);
        } else if (pol_session.login_time != 0) {
            t[0] = pol_session.login_time;
        }
    }
    return r;
}

/* Development trace for the PlayOnline network API. FFXiMain's network module (0x841A6110–0x841AB898) calls
   table slots +0x29C..+0x490. Each slot in that range is swapped for a wrapper that logs the slot, the first
   two arguments and the result, then calls the original. PPC passes the first 8 arguments in r3–r10, so
   forwarding 8 values works for any slot that takes up to 8 integer/pointer arguments. */
#define NET_FIRST 0x29C
/* Slot +0x304 is the variadic error reporter and is never wrapped. Set TRACE_NET 0 to leave every slot
   alone; that also turns off the Xenia DNS fallback below, which lives in the slot wrappers. */
#define NET_SKIP 0x304
#ifndef TRACE_NET
#define TRACE_NET 1
#endif
#define NET_COUNT 128
typedef long long (*any_fn)(long long, long long, long long, long long,
                            long long, long long, long long, long long);
static any_fn net_orig[NET_COUNT];
static long net_calls[NET_COUNT];

/* Lobby address. FFXiMain asks PolCore to resolve the lobby name ffxi00.pol.com (0x840115F0): slot +0x374
   (0x8246D018) starts the lookup and returns a request descriptor, slot +0x378 (0x8246D120) polls it: 0 while
   pending, then 1 with a 0x14-byte result {u16 address count, u16 0, u32 IPv4 address, ...}, or a negative
   status (-19 shows as POL-0019; Xenia's XNetDnsLookup always fails). ffxi00.pol.com itself is never looked
   up when the server preset has an address:
     - an IPv4 address: +0x374 for the lobby name returns LOBBY_LOOKUP without calling PolCore, and +0x378 for
       that descriptor returns the address at once. No DNS at all.
     - a host name: PolCore looks up that name instead (an ordinary public lookup; fails under Xenia).
   Other names still go to PolCore unchanged. */
#define LOOKUP_START_SLOT 0x374
#define LOOKUP_POLL_SLOT 0x378
#define LOBBY_NAME "ffxi00.pol.com"
#define LOBBY_LOOKUP 0x1436   /* descriptor handed out for the lobby name; PolCore's own start at 0 */
static DWORD lobby_address;   /* IPv4 in network (= PPC native) byte order; 0 = none */
static char lobby_host[64];   /* host name to look up instead of ffxi00.pol.com; "" = none */

static int is_lobby_name(long long name)
{
    return name != 0 && strcmp((const char *)(DWORD)name, LOBBY_NAME) == 0;
}

/* Answer the lobby name lookup from lobby_address. Returns 1 when the call was answered. */
static int lobby_lookup(int slot, long long a, long long b, long long *result)
{
    if (lobby_address == 0) {
        return 0;
    }
    if (slot == LOOKUP_START_SLOT && is_lobby_name(a)) {
        *result = LOBBY_LOOKUP;
        return 1;
    }
    if (slot == LOOKUP_POLL_SLOT && a == LOBBY_LOOKUP && b != 0) {
        BYTE *out = (BYTE *)(DWORD)b;
        memset(out, 0, 0x14);
        *(USHORT *)out = 1;
        *(DWORD *)(out + 4) = lobby_address;
        *result = 1;
        return 1;
    }
    return 0;
}

static int config_lang = -1;  /* launcher.ini language as FFXI's number; -1 = leave PlayOnline's answer */

/* Language. FFXi.dll does not pass config.txt to FFXiMain: the command line FFXiMain parses is FFXi.dll's
   own " -net %d" plus PolCore's arguments (logged as " -net 3 -port 54001"), so a `-lang` there never
   reaches it. Both modules take the language from PlayOnline instead, through slot +0x694: FFXi.dll
   (0x83060190) for the start-up block FFXiMain copies into its settings, FFXiMain (0x8410FB90) for
   settings +0xC. Answer that slot with launcher.ini's language. */
typedef int (*slot_694_fn)(void);
static slot_694_fn orig_694;

static int language_slot(void)
{
    static int logged;
    int r = orig_694();
    if (logged++ < 4) {
        DbgPrint("114360: table+0x694 language: PlayOnline %d, answering %d\n", r, config_lang >= 0 ? config_lang : r);
    }
    return config_lang >= 0 ? config_lang : r;
}

/* Launcher options, game:\launcher.ini. `key = value` lines, `#` comments, `[name]` starts a server preset:

     server    = lab          # the preset to play on
     language  = english      # japanese, english, or FFXI's number 0-3; omitted = PlayOnline's choice
     font      = 7x13         # menu font (font.h): 7x13, 9x15, 6x13 or 8x16
     autostart = 5            # seconds before the menu starts the game on its own; 0 = wait
     packet_dump = 192.168.1.10:9999   # also send every server packet there (tools/packet-dump-recv.py)

     [lab]
     address  = 127.0.0.1    # IPv4 (no DNS at all) or a host name (looked up instead of ffxi00.pol.com)
     revision = XB2-40160203_0   # client revision this server needs: game:\rev\<revision>\ (named by data set)
     version  = 20100904_2   # optional: client version the lobby login reports (send_fixed); omitted = the
                             # revision's version.txt or patch.ver, in its PS2 form
     credential = crystal.polc  # optional: play as this PlayOnline sign-in (tools/pol-signin's file, game:\)
     pol_id   = TEST0001        # optional: sign in to PlayOnline on the console with this ID (the menu turns
     pol_password = password    #   blue) before starting; the chat page uses the same session
     platform = PS2             # optional: platform the world login names (PS2, WIN); omitted = the 360's own.
                                #   Project Crystal sends 2010-layout packets with renumbered NPCs to PS2
     pol_version = 20100601_1   # optional: the PlayOnline version to sign in as (polirc.c pol_versions):
                                #   20100601_1 for Project Crystal, 20110726_A for OpenLobby; omitted = the
                                #   game's own PolCore (20110726_A)

   A preset names everything a server needs; the launcher starts the revision the chosen preset asks for. The
   menu (menu.cpp) edits all of it and writes the file back when something changed, without comments.
   game:\0001\config.txt is FFXi.dll's file and is not read: under PolCore its options never reach FFXiMain. */
#define LAUNCHER_INI "game:\\launcher.ini"
#define DEFAULT_AUTOSTART 5
static MENU menu;                         /* every option and preset, as the menu shows and edits them */
static char config_server[32];            /* the chosen preset, once the menu is done (apply_config) */
static char config_address[MENU_TEXT];
static char config_language[16];
static char config_revision[MENU_TEXT];
static char config_platform[8];             /* the world login's platform tag; "" = the game's own (sendto_fixed) */
static char config_patches[MENU_TEXT];      /* the preset's client patches (patches.h); "" = defaults */
static DWORD dump_address;                /* packet export target (packet_dump), network order; 0 = off */
static WORD dump_port;

/* Copy src into dst (size bytes) without leading/trailing blanks and without a trailing comment. */
static void copy_trimmed(char *dst, int size, const char *src)
{
    int n;
    while (*src == ' ' || *src == '\t') {
        src++;
    }
    n = (int)strcspn(src, "#\r\n");
    while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\t')) {
        n--;
    }
    if (n > size - 1) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Read launcher.ini into `menu`. */
static void load_config(void)
{
    char line[256], key[32], value[MENU_TEXT], server[32];
    MENU_PRESET *preset = NULL;
    int i;
    FILE *f = fopen(LAUNCHER_INI, "r");
    menu.autostart = DEFAULT_AUTOSTART;
    server[0] = '\0';
    if (f == NULL) {
        DbgPrint("114360: no %s\n", LAUNCHER_INI);
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        const char *start = line + strspn(line, " \t");
        char *eq;
        if (*start == '[') {
            preset = NULL;
            if (menu.preset_count < MENU_MAX_PRESETS) {
                preset = &menu.presets[menu.preset_count++];
                copy_trimmed(preset->name, sizeof(preset->name), start + 1);
                preset->name[strcspn(preset->name, "]")] = '\0';
            }
            continue;
        }
        if (*start == '#' || (eq = strchr(line, '=')) == NULL) {
            continue;
        }
        *eq = '\0';
        copy_trimmed(key, sizeof(key), line);
        copy_trimmed(value, sizeof(value), eq + 1);
        if (preset != NULL) {
            if (strcmp(key, "address") == 0) {
                strcpy(preset->address, value);
            } else if (strcmp(key, "revision") == 0) {
                strcpy(preset->revision, value);
            } else if (strcmp(key, "version") == 0) {
                strcpy(preset->version, value);
            } else if (strcmp(key, "credential") == 0) {
                strcpy(preset->credential, value);
            } else if (strcmp(key, "pol_id") == 0) {
                copy_trimmed(preset->pol_id, sizeof(preset->pol_id), value);
            } else if (strcmp(key, "pol_password") == 0) {
                copy_trimmed(preset->pol_password, sizeof(preset->pol_password), value);
            } else if (strcmp(key, "platform") == 0) {
                copy_trimmed(preset->platform, sizeof(preset->platform), value);
            } else if (strcmp(key, "pol_version") == 0) {
                copy_trimmed(preset->pol_version, sizeof(preset->pol_version), value);
            } else if (strcmp(key, "patches") == 0) {
                copy_trimmed(preset->patches, sizeof(preset->patches), value);
            }
        } else if (strcmp(key, "server") == 0) {
            copy_trimmed(server, sizeof(server), value);
        } else if (strcmp(key, "language") == 0) {
            copy_trimmed(menu.language, sizeof(menu.language), value);
        } else if (strcmp(key, "font") == 0) {
            copy_trimmed(menu.font, sizeof(menu.font), value);
        } else if (strcmp(key, "autostart") == 0) {
            menu.autostart = atoi(value);
        } else if (strcmp(key, "packet_dump") == 0) {
            copy_trimmed(menu.packet_dump, sizeof(menu.packet_dump), value);
        } else if (strcmp(key, "chat_channel") == 0) {
            copy_trimmed(menu.channel, sizeof(menu.channel), value);
        }
    }
    fclose(f);
    for (i = 0; i < menu.preset_count; i++) {
        if (_stricmp(menu.presets[i].name, server) == 0) {
            menu.current = i;
        }
    }
}

/* Write `menu` back to launcher.ini. */
static void save_config(void)
{
    int i;
    FILE *f = fopen(LAUNCHER_INI, "w");
    if (f == NULL) {
        DbgPrint("114360: cannot write %s\n", LAUNCHER_INI);
        return;
    }
    fprintf(f, "# 114360 launcher options (written by the launcher menu; see client/launcher/launcher.c)\n");
    fprintf(f, "server    = %s\n", menu.preset_count > 0 ? menu.presets[menu.current].name : "");
    if (menu.language[0]) {
        fprintf(f, "language  = %s\n", menu.language);
    }
    if (menu.font[0]) {
        fprintf(f, "font      = %s\n", menu.font);
    }
    fprintf(f, "autostart = %d\n", menu.autostart);
    if (menu.packet_dump[0]) {
        fprintf(f, "packet_dump = %s\n", menu.packet_dump);
    }
    if (menu.channel[0]) {
        fprintf(f, "chat_channel = %s\n", menu.channel);
    }
    for (i = 0; i < menu.preset_count; i++) {
        fprintf(f, "\n[%s]\naddress  = %s\nrevision = %s\n", menu.presets[i].name, menu.presets[i].address,
                menu.presets[i].revision);
        if (menu.presets[i].version[0]) {
            fprintf(f, "version  = %s\n", menu.presets[i].version);
        }
        if (menu.presets[i].credential[0]) {
            fprintf(f, "credential = %s\n", menu.presets[i].credential);
        }
        if (menu.presets[i].pol_id[0]) {
            fprintf(f, "pol_id   = %s\npol_password = %s\n", menu.presets[i].pol_id, menu.presets[i].pol_password);
        }
        if (menu.presets[i].platform[0]) {
            fprintf(f, "platform = %s\n", menu.presets[i].platform);
        }
        if (menu.presets[i].pol_version[0]) {
            fprintf(f, "pol_version = %s\n", menu.presets[i].pol_version);
        }
        if (menu.presets[i].patches[0]) {
            fprintf(f, "patches  = %s\n", menu.presets[i].patches);
        }
    }
    fclose(f);
    DbgPrint("114360: wrote %s (%d presets)\n", LAUNCHER_INI, menu.preset_count);
}

/* FFXI's language number for a launcher.ini language, or -1 for PlayOnline's choice. */
static int language_number(const char *name)
{
    if (_stricmp(name, "japanese") == 0) {
        return 0;
    }
    if (_stricmp(name, "english") == 0) {
        return 1;
    }
    if (name[0] >= '0' && name[0] <= '3' && name[1] == '\0') {
        return name[0] - '0';
    }
    return -1;
}

/* Take the options the game needs from the chosen preset. */
static void apply_config(void)
{
    unsigned a, b, c, d;
    if (menu.preset_count > 0) {
        const MENU_PRESET *p = &menu.presets[menu.current];
        strcpy(config_server, p->name);
        strcpy(config_address, p->address);
        if (p->pol_id[0] && pol_session.state == POL_ONLINE) {
            use_session();
        } else if (p->credential[0]) {
            load_signin(p->credential);
        }
        strcpy(config_revision, p->revision);
        strcpy(config_platform, p->platform);
        strcpy(config_patches, p->patches);
    }
    strcpy(config_language, menu.language);
    config_lang = language_number(config_language);
    if (sscanf(config_address, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && a < 256 && b < 256 && c < 256 && d < 256) {
        lobby_address = (a << 24) | (b << 16) | (c << 8) | d;
    } else if (config_address[0]) {
        strcpy(lobby_host, config_address);
    }
    {
        unsigned port;
        if (sscanf(menu.packet_dump, "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port) == 5 && a < 256 && b < 256
            && c < 256 && d < 256 && port > 0 && port < 65536) {
            dump_address = (a << 24) | (b << 16) | (c << 8) | d;
            dump_port = (WORD)port;
        }
    }
    DbgPrint("114360: server [%s] address [%s] revision [%s] language [%s] packet dump [%s]\n", config_server,
             config_address, config_revision, config_language, dump_address ? menu.packet_dump : "off");
}

/* Client revisions. Each lives in its own folder, game:\rev\<revision>\:
     PolCore.xex   the PlayOnline core that revision was built against
     0001\         the FFXI data: FFXi.dll, FFXiMain.dll, CONFIG.SYS, patch.ver, ROM*, ...
   The launcher loads PolCore from there and points every FFXI content drive at 0001 (see content_create),
   so no content package is involved and revisions sit side by side. In a pooled install the game data is
   stored once for all revisions in game:\data and each revision has a table instead (load_data_table). The
   PlayOnline folder and area.dat stay shared in game:\.

   Every fix below lands on addresses inside one build of PolCore, FFXi.dll, and FFXiMain, whatever the data
   is. The launcher reads the three files' XEX time stamps and only starts a folder whose builds it knows
   (engine.h); patch.ver is shown but says nothing about the program files. */
#define REVISION_DRIVE "rev:"
static char revision_device[192];   /* game_device\rev\<revision>; "" until select_revision */
static void load_data_table(void);

/* Point rev: at the chosen preset's revision folder. Returns 0 without a revision. */
static int select_revision(void)
{
    ANSI_STRING_X link, device;
    LONG status;
    if (config_revision[0] == '\0'
        || strlen(game_device) + 5 + strlen(config_revision) >= sizeof(revision_device)) {
        return 0;
    }
    _snprintf(revision_device, sizeof(revision_device), "%s\\rev\\%s", game_device, config_revision);
    ansi(&link, "\\??\\" REVISION_DRIVE);
    ansi(&device, revision_device);
    status = ObCreateSymbolicLink(&link, &device);
    DbgPrint("114360: %s -> %s status=%08X\n", REVISION_DRIVE, revision_device, status);
    if (status >= 0) {
        load_data_table();
    }
    return status >= 0;
}

/* Direct FFXI start (no Viewer, no Viewer launch data): answer FFXiMain's PlayOnline requests locally. Each
   is a request that returns a handle (or fills a buffer) plus a poll (0 pending, >0 done, <0 error); with
   no PlayOnline connection PolCore fails them (-515 shows as POL-0515) or, for the channel, crashes. They
   tell PlayOnline what the player does or refresh PlayOnline's copy of the character table; LandSandBoat
   needs none of it. Viewer launches use the real slots.
     +0x30C/+0x310  lobby/world channel (0x82480B20; crashes at 0x82480D3C without a Viewer session)
     +0x458/+0x45C  list of up to `count` 0x48-byte records during lobby login (0x841AE2A0); poll -515 ended
                    the lobby after 0x1F (FFXI-3101 before the FIONREAD fix)
     +0x2BC/+0x2C0  request after character creation (0x841A4E28); poll -515 = POL-0515
     +0x2C4         PlayOnline character table entry `index` (0..63) into a buffer. World entry
                    (0x841A4388) looks the chosen character up in it and fails with POL-0001 when no entry
                    matches. PolCore copies 0x68-byte entries from its table at 0x824DFA40 (0x82484390).
                    Entry: +0x00 u16 flags (bit 0 = used), +0x02 u16 content code (1 = FFXI), +0x04 u32 world
                    field, +0x08 u64 Content ID, +0x10 u64 flags (bit 0x8000 = open a PlayOnline channel; on
                    the big-endian PPC that bit lives in the u32 at +0x14). Answered from the lobby's own
                    character list (FFXIMAIN_PROFILE.lobby_chars; 40160203_0: *(0x8481FC78) + 0x13844),
                    0x8C bytes per slot: Content ID at +0, world field built as 0x841A4468 does
                    (byte +0xB << 24 | u16 +6 << 16 | u16 +4), no channel.
     +0x2DC/+0x2E0, +0x2CC/+0x2D0, +0x2D4/+0x2D8   world-entry status requests and their polls
                    (0x841A4F88): handle 0, poll done. The +0x2D0 poll failed with -5136 (shown as 5136).
   FFXiMain addresses above are 40160203_0's. */
#define POL_LIST_REQUEST 0x458
#define POL_LIST_RECORD 0x48
#define POL_CHAR_RECORD 0x2C4
#define POL_HANDLE_RECORD 0x2B4
#define LOBBY_CHAR_SLOTS 16

/* FFXiMain's lobby root object, or 0 before FFXiMain is loaded or has made it. */
static DWORD lobby_root(void)
{
    return ffximain_profile != NULL ? *(volatile DWORD *)ffximain_profile->lobby_root : 0;
}

/* World entry (0x841A4F88) reads the chosen character's entry twice. The first time (state 0xE) bit 0x8000
   of the u64 at +0x10 must be clear, or it opens a PlayOnline channel straight away; after its status
   requests (+0x2DC/+0x2CC/+0x2D4) it reads the entry again (state 0x14) and now needs the bit set, the
   PlayOnline server having marked the character as entering the game. Without it the game never moves on
   and times out with FFXI-3113. Set once the +0x2D4 request has been answered. */
static int world_entry_announced;

/* Fill PlayOnline character-table entry `index` from lobby character slot `index`. */
static void pol_char_record(DWORD index, BYTE *rec)
{
    DWORD root = lobby_root();
    memset(rec, 0, 0x68);
    if (root != 0 && index < LOBBY_CHAR_SLOTS) {
        const BYTE *slot = (const BYTE *)(root + ffximain_profile->lobby_chars
                                          + index * ffximain_profile->lobby_char_size);
        DWORD content_id = *(const DWORD *)slot;
        if (content_id != 0) {
            *(USHORT *)rec = 1;
            *(USHORT *)(rec + 2) = 1;
            *(DWORD *)(rec + 4) = (DWORD)slot[0xB] << 24 | (DWORD)*(const USHORT *)(slot + 6) << 16
                                  | *(const USHORT *)(slot + 4);
            *(DWORD *)(rec + 0xC) = content_id;   /* low word of the big-endian u64 at +0x08 */
            if (world_entry_announced) {
                *(DWORD *)(rec + 0x14) = 0x8000;   /* low word of the u64 flags at +0x10 */
            }
        }
    }
}

static int direct_answer(int slot, long long a, long long b, long long *r)
{
    switch (slot) {
    case 0x30C: case 0x2BC: case 0x2CC: case 0x2DC: case 0x2E0:
        *r = 0;
        return 1;
    case 0x2D4:
        world_entry_announced = 1;
        *r = 0;
        return 1;
    case 0x310: case 0x45C: case 0x2C0: case 0x2D0: case 0x2D8:
        *r = 1;
        return 1;
    case POL_LIST_REQUEST:
        if (a != 0 && b > 0 && b <= 64) {
            memset((void *)(DWORD)a, 0, (size_t)b * POL_LIST_RECORD);
        }
        *r = 0;
        return 1;
    case POL_CHAR_RECORD:
        if (b != 0) {
            pol_char_record((DWORD)a, (BYTE *)(DWORD)b);
        }
        *r = 0;
        return 1;
    }
    return 0;
}

static long long net_call(int i, long long a, long long b, long long c, long long d,
                          long long e, long long f, long long g, long long h)
{
    static int late_logged;
    long long r;
    int slot = NET_FIRST + i * 4;
    int n = net_calls[i]++;
    /* Each slot logs its first four calls; once world entry has announced itself (+0x2D4), every call is
       logged (up to 200) so the step it waits on shows up. */
    int log = n < 4 || (world_entry_announced && late_logged++ < 200);
    if (log) {
        DbgPrint("114360: net slot +0x%03X -> %p (%08X, %08X, %08X)\n", slot, net_orig[i],
                 (DWORD)a, (DWORD)b, (DWORD)c);
    }
    if (lobby_lookup(slot, a, b, &r) || (direct_mode && direct_answer(slot, a, b, &r))) {
        if (log) {
            DbgPrint("114360: net slot +0x%03X answered locally: %d\n", slot, (int)r);
        }
        return r;
    }
    if (slot == LOOKUP_START_SLOT && lobby_host[0] && is_lobby_name(a)) {
        a = (long long)(DWORD)lobby_host;
        DbgPrint("114360: looking up %s instead of %s\n", lobby_host, LOBBY_NAME);
    }
    r = net_orig[i](a, b, c, d, e, f, g, h);
    /* +0x2B4 (PolCore 0x8247FD40) reads PlayOnline handle `index` into a record whose first u64 has bit 0 =
       "handle in use" and whose bytes at +0x20..+0x27 are its eight character slots (bit 0 = taken). World
       entry (0x841A42C8) needs a handle in use with a free slot, else POL-0001. PolCore leaves every
       handle empty without a Viewer login, so a direct start marks handle 0 in use (its slots stay free). */
    if (direct_mode && slot == POL_HANDLE_RECORD && a == 0 && b != 0 && (int)r >= 0) {
        ((BYTE *)(DWORD)b)[7] |= 1;
    }
    if (log) {
        DbgPrint("114360: net slot +0x%03X returned %08X\n", NET_FIRST + i * 4, (DWORD)r);
    }
    return r;
}

/* Wrapper net_w<i>_<k> serves table index i*8+k. The index must be computed, not pasted: pasting `i##k`
   made the token 10 (decimal ten) for the wrapper in position 8, so every wrapper past the first eight
   logged the wrong slot and forwarded to another slot's original. */
#define NETW(i, k) static long long net_w##i##_##k(long long a, long long b, long long c, long long d, \
    long long e, long long f, long long g, long long h) { return net_call((i) * 8 + (k), a, b, c, d, e, f, g, h); }
#define NETW8(i) NETW(i, 0) NETW(i, 1) NETW(i, 2) NETW(i, 3) NETW(i, 4) NETW(i, 5) NETW(i, 6) NETW(i, 7)
NETW8(0) NETW8(1) NETW8(2) NETW8(3) NETW8(4) NETW8(5) NETW8(6) NETW8(7)
NETW8(8) NETW8(9) NETW8(10) NETW8(11) NETW8(12) NETW8(13) NETW8(14) NETW8(15)
#define NETP8(i) net_w##i##_0, net_w##i##_1, net_w##i##_2, net_w##i##_3, net_w##i##_4, net_w##i##_5, \
    net_w##i##_6, net_w##i##_7
static const any_fn net_wrappers[NET_COUNT] = {
    NETP8(0), NETP8(1), NETP8(2), NETP8(3), NETP8(4), NETP8(5), NETP8(6), NETP8(7),
    NETP8(8), NETP8(9), NETP8(10), NETP8(11), NETP8(12), NETP8(13), NETP8(14), NETP8(15)
};

/* Socket error codes under Xenia. Xenia's NetDll_WSAGetLastError (xam ordinal 27) returns the Linux host's
   errno: a non-blocking connect reports 115 (EINPROGRESS) where a 360 reports WSAEWOULDBLOCK (10035), and
   FFXiMain then treats the connect as failed (FFXI-3100). Every module that does its own sockets (PolCore,
   PolViewer, FFXiMain) gets its import thunk for the ordinal redirected here (thunk_hooks). On a console
   errno values never appear, so the translator changes nothing there. */
typedef DWORD (*wsa_error_fn)(void);
static wsa_error_fn real_wsa_error;

/* Set by a launcher wrapper that turns a host result into a different socket error; the guest's next
   WSAGetLastError returns it once. */
static DWORD wsa_error_override;

static DWORD wsa_error(void)
{
    static const USHORT map[][2] = {
        {11, 10035}, {115, 10035}, {114, 10037}, {106, 10056}, {107, 10057}, {111, 10061}, {104, 10054},
        {103, 10053}, {110, 10060}, {113, 10065}, {101, 10051}, {98, 10048}, {90, 10040}, {108, 10058},
    };
    DWORD err;
    int i;
    if (wsa_error_override != 0) {
        err = wsa_error_override;
        wsa_error_override = 0;
        return err;
    }
    err = real_wsa_error();
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (err == map[i][0]) {
            return map[i][1];
        }
    }
    return err;
}

/* Overwrite a 16-byte import thunk with an absolute jump to `target`. No instruction-cache flush: the XDK
   has no icbi intrinsic, and this is for Xenia, whose JIT translates the thunk on first call. Patched before
   the module first runs it, so there is no stale copy. */
static void write_jump(DWORD thunk, DWORD target)
{
    volatile DWORD *code = (volatile DWORD *)thunk;
    code[0] = 0x3D600000 | (target >> 16);       /* lis   r11, target@h */
    code[1] = 0x616B0000 | (target & 0xFFFF);     /* ori   r11, r11, target@l */
    code[2] = 0x7D6903A6;                         /* mtctr r11 */
    code[3] = 0x4E800420;                         /* bctr */
    __dcbst(0, (void *)code);
    __sync();
}

/* select() under Xenia. Xenia reports the 2016 client's lobby socket readable when nothing has arrived:
   right after connect, and again in the gap after the client sends its character-list request (0x1F) while
   the bridge fetches the list. The client's socket layer (0x841CB228) then asks FIONREAD, gets 0, takes
   "readable with nothing to read" as the peer closing, and its lobby poll (0x841B8D60) fails with code 0x65,
   which the game shows as FFXI-3101 (3000 + 0x65). Keep Xenia's combined call for its timeout, ask about each
   set on its own with a zero timeout, and keep a socket in the read set only if a non-blocking
   recv(MSG_PEEK) sees data or a real end of stream (0). Xbox fd_set: u32 count, then up to 64 u32 sockets.
   recv is xam ordinal 18. */
#define XMSG_PEEK 2
typedef struct _XFDSET {
    DWORD count;
    DWORD fd[64];
} XFDSET;
typedef struct _XTIMEVAL {
    LONG sec;
    LONG usec;
} XTIMEVAL;
typedef int (*select_fn)(DWORD caller, int nfds, XFDSET *rd, XFDSET *wr, XFDSET *ex, XTIMEVAL *timeout);
typedef int (*recv_fn)(DWORD caller, DWORD s, char *buf, int len, int flags);
static select_fn real_select;
static recv_fn real_recv;
static int select_calls;
static int key_fix_waiting(DWORD s);
/* One set on its own, zero timeout: `set` becomes the sockets that are ready. Returns how many. */
static int select_one(DWORD caller, int nfds, int which, XFDSET *set, const XFDSET *asked)
{
    XTIMEVAL zero = {0, 0};
    int r;
    *set = *asked;
    r = real_select(caller, nfds, which == 0 ? set : NULL, which == 1 ? set : NULL, which == 2 ? set : NULL,
                    &zero);
    if (r <= 0) {
        set->count = 0;
        return 0;
    }
    return (int)set->count;
}

/* Keep only sockets with bytes waiting (recv(MSG_PEEK) > 0). A peek of 0 would be a closed stream on a real
   stack, but after the client's 0x1F it came back >= 0 with nothing to read and the game still closed, so 0
   is not trusted here; a peer that really closes is still noticed by the game's own timeout. Logs each peek
   for the first calls. Returns how many remain. */
static int peek_logged;
static int readable_only(DWORD caller, XFDSET *set)
{
    char byte;
    DWORD i, kept = 0;
    for (i = 0; i < set->count && i < 64; i++) {
        int r = real_recv(caller, set->fd[i], &byte, 1, XMSG_PEEK);
        if (peek_logged++ < 40) {
            DbgPrint("114360:   peek socket %08X -> %d (err %u)\n", set->fd[i], r,
                     r < 0 ? real_wsa_error() : 0);
        }
        if (r > 0 || key_fix_waiting(set->fd[i]) > 0) {
            set->fd[kept++] = set->fd[i];
        }
    }
    set->count = kept;
    return (int)kept;
}

static int select_fixed(DWORD caller, int nfds, XFDSET *rd, XFDSET *wr, XFDSET *ex, XTIMEVAL *timeout)
{
    XFDSET ask_r, ask_w, ask_e;
    int raw, fixed = 0, n = select_calls++;
    DWORD raw_r, raw_w, raw_e;
    if (rd) ask_r = *rd;
    if (wr) ask_w = *wr;
    if (ex) ask_e = *ex;
    if (!on_xenia) {
        /* The console's select is right; only a cut 0x05 waiting in key_fix (no bytes left on the socket) needs
           reporting, at once. */
        DWORD i;
        for (i = 0; rd != NULL && i < ask_r.count && i < 64; i++) {
            if (key_fix_waiting(ask_r.fd[i]) > 0) {
                rd->fd[0] = ask_r.fd[i];
                rd->count = 1;
                if (wr) wr->count = 0;
                if (ex) ex->count = 0;
                return 1;
            }
        }
        return real_select(caller, nfds, rd, wr, ex, timeout);
    }
    raw = real_select(caller, nfds, rd, wr, ex, timeout);
    raw_r = rd ? rd->count : 0;
    raw_w = wr ? wr->count : 0;
    raw_e = ex ? ex->count : 0;
    if (raw <= 0) {
        fixed = raw;
    } else {
        if (rd && select_one(caller, nfds, 0, rd, &ask_r) > 0) fixed += readable_only(caller, rd);
        if (wr) fixed += select_one(caller, nfds, 1, wr, &ask_w);
        if (ex) fixed += select_one(caller, nfds, 2, ex, &ask_e);
    }
    /* A socket with a cut 0x05 still queued (recv_fixed) is readable whatever Xenia says. */
    if (rd) {
        DWORD i, j;
        for (i = 0; i < ask_r.count && i < 64; i++) {
            if (key_fix_waiting(ask_r.fd[i]) > 0) {
                if (raw <= 0 && fixed <= 0) {
                    rd->count = 0;
                    fixed = 0;
                }
                for (j = 0; j < rd->count && rd->fd[j] != ask_r.fd[i]; j++) {
                }
                if (j == rd->count && rd->count < 64) {
                    rd->fd[rd->count++] = ask_r.fd[i];
                    fixed++;
                }
            }
        }
    }
    if (n < 40) {
        DbgPrint("114360: select #%d asked r=%u w=%u e=%u\n", n + 1, rd ? ask_r.count : 0,
                 wr ? ask_w.count : 0, ex ? ask_e.count : 0);
        DbgPrint("114360:   xenia %d (r=%u w=%u e=%u)\n", raw, raw_r, raw_w, raw_e);
        DbgPrint("114360:   fixed %d (r=%u w=%u e=%u)\n", fixed, rd ? rd->count : 0, wr ? wr->count : 0,
                 ex ? ex->count : 0);
    }
    return fixed;
}

/* ioctlsocket(FIONREAD) under Xenia. Xenia stores the waiting-byte count in the host's little-endian order,
   so the big-endian guest reads 40 as 0x28000000 and the 172-byte 0x20 character list as 0xAC000000 (logged
   next to recv(MSG_PEEK) of the same socket: 40, 36, 172). The 40- and 36-byte replies still got read
   because the game only needed "at least N"; 0xAC000000 is past what the client accepts, so it closed the
   lobby socket instead (FFXI-3101). Swap every FIONREAD count back. Like the other thunk patches this is for
   Xenia only (no instruction-cache flush), so the swap never runs on a console. xam ordinal 6. */
#define XFIONREAD 0x4004667F
typedef int (*ioctl_fn)(DWORD caller, DWORD s, DWORD cmd, DWORD *arg);
static ioctl_fn real_ioctl;
static int ioctl_logged;

static void console_take(DWORD caller, DWORD s);

static int ioctl_fixed(DWORD caller, DWORD s, DWORD cmd, DWORD *arg)
{
    int r;
    if (cmd == XFIONREAD && !on_xenia) {
        console_take(caller, s);   /* the client asks the count before reading: decide the 0x05 first */
    }
    r = real_ioctl(caller, s, cmd, arg);
    if (cmd == XFIONREAD && r == 0 && arg != NULL) {
        DWORD raw = *arg;
        *arg = (on_xenia ? _byteswap_ulong(raw) : raw) + key_fix_waiting(s);
        if (ioctl_logged++ < 20) {
            DbgPrint("114360: FIONREAD count %08X -> %u%s\n", raw, *arg, on_xenia ? " (Xenia byte order)" : "");
        }
    }
    return r;
}

/* recv() end of stream under Xenia. After the 0x0B world hand-off the lobby closes the connection. The
   client reads once more, gets 0 (end of stream) and throws a C++ exception; on a console its socket layer
   catches it (catch blocks around 0x841CB418) and sets the connection's state word to 6, "closed". Xenia
   does not unwind guest C++ exceptions ("Guest attempted to throw a C++ exception!"): execution runs on with
   garbage, a 0xBF7D6830-byte allocation fails and the game crashes (PC 0x8460F16C). Masking the end of
   stream alone avoided the crash but left the client polling the dead socket until FFXI-3113 (a timeout);
   reporting the socket in select's exception set did not help either. So do what the catch block does:
   report "nothing yet" (-1, WSAEWOULDBLOCK) and set the lobby connection's state to 6. The connection is
   FFXIMAIN_PROFILE.lobby_object off the lobby root, its state word lobby_state into it (40160203_0:
   *(*(0x8481FC78) + 0x13690) + 0x2F0D4; the lobby poll 0x841B8D60 reads both). FFXiMain only. */
#define LOBBY_STATE_CLOSED 6
#define XWSAEWOULDBLOCK 10035
static int eof_logged;

/* Lobby key answer 0x05. Retail PC/360 lobbies sent 0x28 bytes: header, key, expansions, features (XiPackets
   S2C_0x0005); LandSandBoat too. Project Crystal's lobby answers in the PS2 layout, 0x34 bytes (key, u64
   expansions, padding), and both the 2009 and the 2016 client then hang up without a word (FFXI-3101). Cut to
   0x28 bytes and re-hashed (MD5 over the packet with the 16-byte hash field zeroed, as the servers do), the
   lobby goes on to the character and world lists (seen through tools/lobby-proxy.py --fix-key).

   The client reads a packet in pieces: a 1-byte peek, the 4-byte size, FIONREAD, then the rest. So when a read
   finds a 0x34-byte 0x05 at the head of the stream, the whole packet is taken off the socket, cut to 0x28
   bytes, and served from `key_fix` until the client has read it all; FIONREAD and select count those bytes as
   waiting. One lobby socket at a time. Xenia finds it by peeking; a console does not peek (console_take). */
#define LOBBY_KEY_RETAIL 0x28
#define LOBBY_KEY_PS2 0x34
static struct {
    DWORD sock;
    BYTE data[LOBBY_KEY_PS2];
    int len;            /* bytes held for the client: the cut 0x05, or (console) bytes taken off and handed back */
    int pos;            /* bytes served */
} key_fix = {0, {0}, 0, 0};

static int key_fix_waiting(DWORD s)
{
    return key_fix.sock == s ? key_fix.len - key_fix.pos : 0;
}

/* Queue the retail form of a PS2-layout 0x05 (p, 0x34 bytes) for socket s. */
static void queue_retail_key(DWORD s, BYTE *p)
{
    BYTE digest[16];
    p[0] = LOBBY_KEY_RETAIL;
    memset(p + 12, 0, 16);
    md5(p, LOBBY_KEY_RETAIL, digest);
    memcpy(p + 12, digest, 16);
    memcpy(key_fix.data, p, LOBBY_KEY_RETAIL);
    key_fix.sock = s;
    key_fix.len = LOBBY_KEY_RETAIL;
    key_fix.pos = 0;
    DbgPrint("114360: lobby 0x05 in the PS2 layout (0x34 bytes) cut to the retail 0x28; expansions %02X%02X%02X%02X, "
             "services %02X%02X%02X%02X\n", p[0x23], p[0x22], p[0x21], p[0x20], p[0x27], p[0x26], p[0x25], p[0x24]);
}

static int is_ps2_key_head(const BYTE *p)
{
    return p[0] == LOBBY_KEY_PS2 && !p[1] && !p[2] && !p[3] && memcmp(p + 4, "IXFF", 4) == 0 && p[8] == 0x05 && !p[9]
           && !p[10] && !p[11];
}

/* Xenia: a PS2-layout 0x05 at the head of socket s (peeked)? Then take it off the socket and queue the retail
   one. */
static void take_lobby_key(DWORD caller, DWORD s)
{
    BYTE p[LOBBY_KEY_PS2];
    if (real_recv(caller, s, (char *)p, 12, XMSG_PEEK) != 12 || !is_ps2_key_head(p)
        || real_recv(caller, s, (char *)p, LOBBY_KEY_PS2, XMSG_PEEK) != LOBBY_KEY_PS2
        || real_recv(caller, s, (char *)p, LOBBY_KEY_PS2, 0) != LOBBY_KEY_PS2) {
        return;
    }
    queue_retail_key(s, p);
}

/* Console: the same without peeking (MSG_PEEK on the console's sockets lost bytes, and found nothing to cut on
   Crystal: FFXI-3113). Once a socket has 12 bytes waiting they are read off it. The PS2 0x05 is then read
   whole (up to 2 s for the rest) and cut; anything else is handed back to the client first, unchanged, and the
   socket is not looked at again. Runs from the client's FIONREAD (it asks the count before reading) and recv. */
static DWORD key_checked;   /* the last socket looked at */

static void console_take(DWORD caller, DWORD s)
{
    BYTE p[LOBBY_KEY_PS2];
    DWORD avail = 0;
    int got, r, tries;
    if (s == key_checked || key_fix_waiting(s) > 0 || real_ioctl(caller, s, XFIONREAD, &avail) != 0 || avail < 12) {
        return;
    }
    key_checked = s;
    got = real_recv(caller, s, (char *)p, 12, 0);
    if (got <= 0) {
        DbgPrint("114360: lobby socket %08X: reading its head failed (%d)\n", s, got);
        return;
    }
    if (got == 12 && is_ps2_key_head(p)) {
        for (tries = 0; got < LOBBY_KEY_PS2 && tries < 200; tries++) {
            r = real_recv(caller, s, (char *)p + got, LOBBY_KEY_PS2 - got, 0);
            if (r > 0) {
                got += r;
            } else {
                Sleep(10);
            }
        }
    }
    if (got == LOBBY_KEY_PS2) {
        queue_retail_key(s, p);
        return;
    }
    memcpy(key_fix.data, p, got);
    key_fix.sock = s;
    key_fix.len = got;
    key_fix.pos = 0;
    DbgPrint("114360: lobby socket %08X: no PS2 0x05; %d bytes handed back as read\n", s, got);
}

static int recv_fixed(DWORD caller, DWORD s, char *buf, int len, int flags)
{
    int r;
    if (!on_xenia) {
        console_take(caller, s);
    } else if (key_fix_waiting(s) == 0) {
        take_lobby_key(caller, s);
    }
    if (key_fix_waiting(s) > 0 && len > 0) {
        r = key_fix_waiting(s) < len ? key_fix_waiting(s) : len;
        memcpy(buf, key_fix.data + key_fix.pos, r);
        if (!(flags & XMSG_PEEK)) {
            key_fix.pos += r;
        }
        return r;
    }
    r = real_recv(caller, s, buf, len, flags);
    /* Console: what the lobby sends (error packets name their code), the first reads only. */
    if (!on_xenia && r > 0 && !(flags & XMSG_PEEK)) {
        static int recv_logged;
        if (recv_logged++ < 30) {
            char hex[3 * 32 + 1];
            int i, n = r < 32 ? r : 32;
            for (i = 0; i < n; i++) {
                _snprintf(hex + 3 * i, 4, "%02X ", (BYTE)buf[i]);
            }
            hex[3 * n] = '\0';
            DbgPrint("114360: recv socket %08X: %d bytes: %s\n", s, r, hex);
        }
    }
    if (r == 0 && len > 0 && on_xenia) {
        DWORD root = lobby_root();
        DWORD lobby = root ? *(volatile DWORD *)(root + ffximain_profile->lobby_object) : 0;
        if (lobby) {
            *(volatile DWORD *)(lobby + ffximain_profile->lobby_state) = LOBBY_STATE_CLOSED;
        }
        if (eof_logged++ < 20) {
            DbgPrint("114360: recv socket %08X: end of stream; lobby %08X state -> closed\n", s, lobby);
        }
        wsa_error_override = XWSAEWOULDBLOCK;
        r = -1;
    }
    return r;
}

/* The client version the lobby is told. FFXiMain's first lobby packet, 0x26 (152 bytes: u32 size, "IXFF",
   u32 command, 16-byte session hash, then the PlayOnline account, client code and content-auth blob), carries
   patch.ver at +0x74, 16 bytes: "40160203_0" for the 2016 build. The first digit is the platform (2 PS2,
   3 PC, 4 360) and the rest the data's release. Servers decide on it: LandSandBoat compares the first six
   characters (VER_LOCK), Project Crystal's lobby hands it to the world. The launcher reports
   report_version instead (apply_config): the preset's `version`, else the revision's rev:\version.txt
   (the data's own release, written by the revision tools), else patch.ver; a version starting with 3 or 4
   is reported in its PS2 form (first digit 2), the platform those servers expect. The client-to-server
   hash field is the session hash, not a digest of the packet, so the rewrite needs nothing else. xam
   ordinal 22. */
#define LOBBY_LOGIN_SIZE 0x98
#define LOBBY_VERSION_OFFSET 0x74
#define LOBBY_VERSION_SIZE 16
typedef int (*send_fn)(DWORD caller, DWORD s, const char *buf, int len, int flags);
static send_fn real_send;
static char report_version[LOBBY_VERSION_SIZE + 1];   /* "" = leave the packet alone */

static int send_fixed(DWORD caller, DWORD s, const char *buf, int len, int flags)
{
    char copy[LOBBY_LOGIN_SIZE];
    if (report_version[0] == '\0' || len != LOBBY_LOGIN_SIZE || memcmp(buf + 4, "IXFF", 4) != 0
        || (BYTE)buf[8] != 0x26) {
        return real_send(caller, s, buf, len, flags);
    }
    memcpy(copy, buf, len);
    memset(copy + LOBBY_VERSION_OFFSET, 0, strlen(report_version) + 1);
    memcpy(copy + LOBBY_VERSION_OFFSET, report_version, strlen(report_version));
    DbgPrint("114360: lobby login 0x26: version %.10s reported as %s\n", buf + LOBBY_VERSION_OFFSET, report_version);
    return real_send(caller, s, copy, len, flags);
}

/* sendto()/recvfrom() addresses under Xenia. The game sends its world login to 10.36.0.1:54230 (the 0x0B
   hand-off), but on the wire (dumpcap on any, 2026-10-31) Xenia sent it to 1.0.36.10:54995 out of the
   Wi-Fi: it reverses the four address bytes and swaps the two port bytes of a datagram's sockaddr. TCP
   connect is not affected. Hand Xenia a copy with both pre-reversed so the datagram goes where the game
   meant; replies need only their port swapped back (recvfrom_fixed). xam ordinals 24 and 20. Xbox
   sockaddr_in: u16 family, u16 port, u32 address, port and address in network order. */
typedef int (*sendto_fn)(DWORD caller, DWORD s, const char *buf, int len, int flags, const BYTE *to, int tolen);
typedef int (*recvfrom_fn)(DWORD caller, DWORD s, char *buf, int len, int flags, BYTE *from, int *fromlen);
static sendto_fn real_sendto;
static recvfrom_fn real_recvfrom;
static int sendto_logged, recvfrom_logged;

/* Swap the port bytes and reverse the address bytes of a sockaddr_in in place (its own inverse). */
static void flip_sockaddr(BYTE *sa)
{
    BYTE t;
    t = sa[2]; sa[2] = sa[3]; sa[3] = t;
    t = sa[4]; sa[4] = sa[7]; sa[7] = t;
    t = sa[5]; sa[5] = sa[6]; sa[6] = t;
}

/* The world login (0x00A, the first datagram to the map server, still unencrypted) names the client's platform
   at +0x54 of the packet: "PS2", "WIN" or the 360's own tag. With a preset `platform`, it is rewritten here,
   so a server that picks its packet layout by it (Project Crystal: PS2 gets its 2010 translation) treats the
   game as that client. Two checks cover the change: the packet's LoginPacketCheck (+0x04, the byte sum of
   +0x08..+0x5C, the packet's end) and the datagram's MD5 (its last 16 bytes, over everything after the
   0x1C-byte header). */
#define WORLD_HEADER 0x1C
#define LOGIN_SIZE 0x5C
#define LOGIN_PLATFORM 0x54

static int retag_world_login(const char *buf, int len, BYTE *out, int size)
{
    BYTE *p, sum = 0;
    int i;
    if (config_platform[0] == '\0' || len < WORLD_HEADER + LOGIN_SIZE + 16 || len > size
        || ((((const BYTE *)buf)[WORLD_HEADER] | ((const BYTE *)buf)[WORLD_HEADER + 1] << 8) & 0x1FF) != 0x00A) {
        return 0;
    }
    memcpy(out, buf, len);
    p = out + WORLD_HEADER;
    if (sendto_logged < 10) {
        DbgPrint("114360: world login names platform [%.4s], sending [%s]\n", p + LOGIN_PLATFORM, config_platform);
    }
    memset(p + LOGIN_PLATFORM, 0, 4);
    memcpy(p + LOGIN_PLATFORM, config_platform, strlen(config_platform) < 4 ? strlen(config_platform) : 4);
    for (i = 0x08; i < LOGIN_SIZE; i++) {
        sum = (BYTE)(sum + p[i]);
    }
    p[0x04] = sum;
    md5(p, len - WORLD_HEADER - 16, out + len - 16);
    return 1;
}

static int sendto_fixed(DWORD caller, DWORD s, const char *buf, int len, int flags, const BYTE *to, int tolen)
{
    static BYTE retagged[1500];
    BYTE copy[16];
    int r;
    if (retag_world_login(buf, len, retagged, sizeof(retagged))) {
        buf = (const char *)retagged;
    }
    if (!on_xenia || to == NULL || tolen < 8 || tolen > (int)sizeof(copy)) {
        return real_sendto(caller, s, buf, len, flags, to, tolen);
    }
    memcpy(copy, to, tolen);
    flip_sockaddr(copy);
    r = real_sendto(caller, s, buf, len, flags, copy, tolen);
    if (sendto_logged++ < 10) {
        DbgPrint("114360: sendto %u bytes -> %u.%u.%u.%u:%u = %d\n", len, to[4], to[5], to[6], to[7],
                 (to[2] << 8) | to[3], r);
    }
    return r;
}

/* On the receive side Xenia gets only the port wrong: with the full flip the log showed replies from
   1.0.36.10:54230, so the address arrives correct and only the two port bytes are swapped. */
static int recvfrom_fixed(DWORD caller, DWORD s, char *buf, int len, int flags, BYTE *from, int *fromlen)
{
    int r = real_recvfrom(caller, s, buf, len, flags, from, fromlen);
    if (on_xenia && r > 0 && from != NULL && fromlen != NULL && *fromlen >= 8) {
        BYTE t = from[2];
        from[2] = from[3];
        from[3] = t;
        if (recvfrom_logged++ < 10) {
            DbgPrint("114360: recvfrom %d bytes <- %u.%u.%u.%u:%u\n", r, from[4], from[5], from[6], from[7],
                     (from[2] << 8) | from[3]);
        }
    }
    return r;
}

/* FFXI content drives. PolCore (CONFIG.SYS on 000100:), FFXi.dll (R000100:\FFXiMain.dll), and FFXiMain
   (000100: .. 000111:, data) open the FFXI data as installed content packages through XamContentCreateEx
   (xam ordinal 601) and release them with XamContentClose (602). Each module's import thunk for both is
   redirected here: for an FFXI root ("0001nn" or "R0001nn") the drive is linked to the chosen revision's 0001
   folder instead, so the data is a plain folder per revision on Xenia and on a console alike. Other content
   (PlayOnline's 1000nn) goes to XAM unchanged. */
typedef DWORD (*content_create_fn)(DWORD user, const char *root, const void *data, DWORD flags, DWORD *disposition,
                                   DWORD *license, DWORD cache, ULARGE_INTEGER size, XOVERLAPPED *overlapped);
typedef DWORD (*content_close_fn)(const char *root, XOVERLAPPED *overlapped);
static content_create_fn real_content_create;
static content_close_fn real_content_close;

static int is_ffxi_root(const char *root)
{
    if (root[0] == 'R') {
        root++;
    }
    return strlen(root) == 6 && strncmp(root, "0001", 4) == 0 && root[4] >= '0' && root[4] <= '9' && root[5] >= '0'
           && root[5] <= '9';
}

/* Finish a call that the caller may have made asynchronous: report success through its XOVERLAPPED. */
static DWORD content_done(XOVERLAPPED *overlapped)
{
    if (overlapped == NULL) {
        return ERROR_SUCCESS;
    }
    overlapped->InternalLow = ERROR_SUCCESS;
    overlapped->InternalHigh = 0;
    overlapped->dwExtendedError = 0;
    if (overlapped->hEvent != NULL) {
        SetEvent(overlapped->hEvent);
    }
    return ERROR_IO_PENDING;
}

/* PlayOnline's system data package, content 100000: on a console the PlayOnline install puts its shared data
   there (db, dic, doc, font, icon, utf), and FFXiMain reads the dictionary as 100000:\dic. Xenia has no such
   package, so PolCore's XamContentCreateEx("100000") failed and the game logged ResolvePath(100000:\dic)
   failed - device not found. The disc's PlayOnline\data folder (game:\PlayOnline\data) is that package. */
#define POL_DATA_ROOT "100000"

static int is_mounted_root(const char *root)
{
    return root != NULL && (strcmp(root, POL_DATA_ROOT) == 0 || (is_ffxi_root(root) && revision_device[0] != '\0'));
}

static DWORD content_create(DWORD user, const char *root, const void *data, DWORD flags, DWORD *disposition,
                            DWORD *license, DWORD cache, ULARGE_INTEGER size, XOVERLAPPED *overlapped)
{
    char name[16], target[224];
    ANSI_STRING_X link, device;
    LONG status;
    if (!is_mounted_root(root)) {
        return real_content_create(user, root, data, flags, disposition, license, cache, size, overlapped);
    }
    _snprintf(name, sizeof(name), "\\??\\%s:", root);
    name[sizeof(name) - 1] = '\0';
    if (strcmp(root, POL_DATA_ROOT) == 0) {
        _snprintf(target, sizeof(target), "%s\\PlayOnline\\data", game_device);
    } else {
        _snprintf(target, sizeof(target), "%s\\0001", revision_device);
    }
    target[sizeof(target) - 1] = '\0';
    ansi(&link, name);
    ansi(&device, target);
    ObDeleteSymbolicLink(&link);
    status = ObCreateSymbolicLink(&link, &device);
    DbgPrint("114360: content %s: -> %s status=%08X\n", root, target, status);
    if (status < 0) {
        return ERROR_PATH_NOT_FOUND;
    }
    if (disposition != NULL) {
        *disposition = XCONTENT_OPENED_EXISTING;
    }
    if (license != NULL) {
        *license = 0;
    }
    return content_done(overlapped);
}

static DWORD content_close(const char *root, XOVERLAPPED *overlapped)
{
    char name[16];
    ANSI_STRING_X link;
    if (!is_mounted_root(root)) {
        return real_content_close(root, overlapped);
    }
    _snprintf(name, sizeof(name), "\\??\\%s:", root);
    name[sizeof(name) - 1] = '\0';
    ansi(&link, name);
    ObDeleteSymbolicLink(&link);
    return content_done(overlapped);
}

/* Shared data pool (tools/ffxi360-pool.py). A pooled install keeps each distinct game file once, as
   game:\data\NNN\MMM.dat, and gives every revision a table, rev\<revision>\files.tbl, from the path FFXiMain asks
   for ("ROM\120\83.DAT", "FTABLE.DAT", after the 0001nn: drive) to a pool file. FFXiMain opens its data with
   NtCreateFile, NtOpenFile and NtQueryFullAttributesFile (kernel ordinals 210, 223, 231); each is redirected
   here, and a path in the table goes to its pool file. Paths not in the table (CONFIG.SYS, patch.ver, SYS, USER,
   TEMP) and revisions without a table stay plain folders, as before.
   files.tbl: "114360P1", u32 count, u32 key size, then sorted entries of key (lowercase, NUL-padded) + u32 id,
   big-endian. */
#define DATA_TABLE_FILE REVISION_DRIVE "\\files.tbl"
#define DATA_KEY 40
typedef struct _DATA_ENTRY {
    char key[DATA_KEY];
    DWORD id;
} DATA_ENTRY;
static DATA_ENTRY *data_table;
static DWORD data_count;

/* Read the selected revision's table, if it has one. */
static void load_data_table(void)
{
    char head[16];
    DWORD count, key;
    FILE *f = fopen(DATA_TABLE_FILE, "rb");
    if (f == NULL) {
        DbgPrint("114360: no %s: the revision's data is a plain folder\n", DATA_TABLE_FILE);
        return;
    }
    if (fread(head, 1, 16, f) != 16 || memcmp(head, "114360P1", 8) != 0) {
        DbgPrint("114360: %s is not a data table\n", DATA_TABLE_FILE);
        fclose(f);
        return;
    }
    count = *(DWORD *)(head + 8);
    key = *(DWORD *)(head + 12);
    if (key != DATA_KEY || count == 0 || count > 1000000
        || (data_table = (DATA_ENTRY *)malloc(count * sizeof(DATA_ENTRY))) == NULL) {
        DbgPrint("114360: %s: unexpected size (%u entries, key %u)\n", DATA_TABLE_FILE, count, key);
        fclose(f);
        return;
    }
    if (fread(data_table, sizeof(DATA_ENTRY), count, f) != count) {
        DbgPrint("114360: %s is short\n", DATA_TABLE_FILE);
        free(data_table);
        data_table = NULL;
        fclose(f);
        return;
    }
    fclose(f);
    data_count = count;
    DbgPrint("114360: data table: %u files from the shared pool (game:\\data)\n", count);
}

/* Pool path for a game path ("000104:\ROM\120\83.DAT"), or NULL when it is not pooled. `out` holds the result. */
static const char *pooled_path(const ANSI_STRING_X *name, char *out, int size)
{
    char root[12], key[DATA_KEY];
    int i, n, colon = -1;
    DWORD lo, hi;
    if (data_count == 0 || name == NULL || name->Buffer == NULL) {
        return NULL;
    }
    n = name->Length;
    for (i = 0; i < n && i < (int)sizeof(root) - 1; i++) {
        if (name->Buffer[i] == ':') {
            colon = i;
            break;
        }
        root[i] = name->Buffer[i];
    }
    if (colon <= 0 || colon + 1 >= n || name->Buffer[colon + 1] != '\\' || n - colon - 2 >= DATA_KEY) {
        return NULL;
    }
    root[colon] = '\0';
    if (!is_ffxi_root(root)) {
        return NULL;
    }
    memset(key, 0, sizeof(key));
    for (i = colon + 2; i < n; i++) {
        char ch = name->Buffer[i];
        key[i - colon - 2] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
    }
    lo = 0;
    hi = data_count;
    while (lo < hi) {
        DWORD mid = (lo + hi) / 2;
        int c = memcmp(data_table[mid].key, key, DATA_KEY);
        if (c == 0) {
            _snprintf(out, size, "game:\\data\\%03u\\%03u.dat", data_table[mid].id / 1000, data_table[mid].id % 1000);
            out[size - 1] = '\0';
            return out;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

typedef struct _OBJECT_ATTRIBUTES_X {
    HANDLE RootDirectory;
    ANSI_STRING_X *ObjectName;
    ULONG Attributes;
} OBJECT_ATTRIBUTES_X;
typedef LONG (*nt_create_file_fn)(HANDLE *handle, DWORD access, OBJECT_ATTRIBUTES_X *attributes, void *io_status,
                                  void *allocation, DWORD file_attributes, DWORD share, DWORD disposition,
                                  DWORD options);
typedef LONG (*nt_open_file_fn)(HANDLE *handle, DWORD access, OBJECT_ATTRIBUTES_X *attributes, void *io_status,
                                DWORD share, DWORD options);
typedef LONG (*nt_query_attributes_fn)(OBJECT_ATTRIBUTES_X *attributes, void *info);
typedef struct _IO_STATUS_X {
    LONG Status;
    ULONG Information;
} IO_STATUS_X;
typedef LONG (*nt_read_file_fn)(HANDLE handle, HANDLE event, void *apc, void *apc_context, IO_STATUS_X *io_status,
                                void *buffer, ULONG length, LARGE_INTEGER *offset);
static nt_create_file_fn real_nt_create_file;
static nt_open_file_fn real_nt_open_file;
static nt_query_attributes_fn real_nt_query_attributes;
static nt_read_file_fn real_nt_read_file;
typedef LONG (*nt_close_fn)(HANDLE handle);
static nt_close_fn real_nt_close;
static int pooled_logged;

/* For a crash report: the last OPEN_RING file opens through the hooks (game path, pool file or "", result). Failed
   opens are also logged as they happen, OPEN_FAILED_LOGGED at most: the game probes for files that may not exist
   (patch files, user settings), so a failure alone is not an error. */
#define OPEN_RING 32
#define OPEN_FAILED_LOGGED 50
typedef struct _OPEN_RECORD {
    char name[48];
    char pooled[24];
    LONG status;
    char op;    /* 'C' NtCreateFile, 'O' NtOpenFile, 'Q' NtQueryFullAttributesFile */
} OPEN_RECORD;
static OPEN_RECORD open_ring[OPEN_RING];
static volatile LONG open_count;
static int open_failed_logged;

static void note_open(char op, const OBJECT_ATTRIBUTES_X *attributes, const char *pooled, LONG status)
{
    LONG n = InterlockedIncrement(&open_count) - 1;
    OPEN_RECORD *r = &open_ring[n % OPEN_RING];
    int len = 0;
    if (attributes != NULL && attributes->ObjectName != NULL && attributes->ObjectName->Buffer != NULL) {
        len = attributes->ObjectName->Length < (int)sizeof(r->name) - 1 ? attributes->ObjectName->Length
                                                                          : (int)sizeof(r->name) - 1;
        memcpy(r->name, attributes->ObjectName->Buffer, len);
    }
    r->name[len] = '\0';
    strncpy(r->pooled, pooled, sizeof(r->pooled) - 1);
    r->pooled[sizeof(r->pooled) - 1] = '\0';
    r->status = status;
    r->op = op;
    if (status < 0 && open_failed_logged < OPEN_FAILED_LOGGED) {
        open_failed_logged++;
        DbgPrint("114360: open %c %s%s%s failed %08X\n", op, r->name, pooled[0] ? " -> " : "", pooled, status);
    }
}

/* `attributes` with its name swapped for the pool file's, in `copy`, or `attributes` itself. `path` is left
   empty when the name is not pooled. */
static OBJECT_ATTRIBUTES_X *pool_attributes(OBJECT_ATTRIBUTES_X *attributes, OBJECT_ATTRIBUTES_X *copy,
                                            ANSI_STRING_X *name, char *path, int size)
{
    path[0] = '\0';
    if (attributes == NULL || pooled_path(attributes->ObjectName, path, size) == NULL) {
        return attributes;
    }
    if (pooled_logged++ < 10) {
        DbgPrint("114360: %.*s -> %s\n", attributes->ObjectName->Length, attributes->ObjectName->Buffer, path);
    }
    *copy = *attributes;
    ansi(name, path);
    copy->ObjectName = name;
    return copy;
}

/* Client patches (patches.h): the handles of open files that an enabled patch edits, and the read position of each
   for reads that give no offset. */
#define PATCHED_OPEN_MAX 8
static struct {
    HANDLE handle;
    const CLIENT_PATCH *patch;
    ULONG position;
} patched_open[PATCHED_OPEN_MAX];

static void note_patched_open(OBJECT_ATTRIBUTES_X *attributes, HANDLE handle, LONG status)
{
    const CLIENT_PATCH *p;
    int i;
    if (status < 0 || attributes == NULL || attributes->ObjectName == NULL) {
        return;
    }
    p = patch_for_file(attributes->ObjectName->Buffer, attributes->ObjectName->Length);
    if (p == NULL || !patch_enabled(p, config_patches)) {
        return;
    }
    for (i = 0; i < PATCHED_OPEN_MAX; i++) {
        if (patched_open[i].handle == NULL) {
            patched_open[i].handle = handle;
            patched_open[i].patch = p;
            patched_open[i].position = 0;
            DbgPrint("114360: patch %s: %s opened\n", p->id, p->file);
            return;
        }
    }
}

static LONG nt_read_file(HANDLE handle, HANDLE event, void *apc, void *apc_context, IO_STATUS_X *io_status,
                         void *buffer, ULONG length, LARGE_INTEGER *offset)
{
    LONG status = real_nt_read_file(handle, event, apc, apc_context, io_status, buffer, length, offset);
    int i, applied, mismatched = 0;
    ULONG start;
    for (i = 0; i < PATCHED_OPEN_MAX; i++) {
        if (patched_open[i].handle == NULL || patched_open[i].handle != handle) {
            continue;
        }
        if (status < 0 || status == 0x103 /* STATUS_PENDING */ || io_status == NULL) {
            return status;
        }
        start = offset != NULL ? (ULONG)offset->QuadPart : patched_open[i].position;
        patched_open[i].position = start + io_status->Information;
        applied = patch_apply(patched_open[i].patch, start, (unsigned char *)buffer, io_status->Information,
                              &mismatched);
        if (applied || mismatched) {
            DbgPrint("114360: patch %s: %d edits applied, %d not (bytes differ: another build)\n",
                     patched_open[i].patch->id, applied, mismatched);
        }
        return status;
    }
    return status;
}

static LONG nt_close(HANDLE handle)
{
    int i;
    for (i = 0; i < PATCHED_OPEN_MAX; i++) {
        if (patched_open[i].handle == handle) {
            patched_open[i].handle = NULL;
        }
    }
    return real_nt_close(handle);
}

static LONG nt_create_file(HANDLE *handle, DWORD access, OBJECT_ATTRIBUTES_X *attributes, void *io_status,
                           void *allocation, DWORD file_attributes, DWORD share, DWORD disposition, DWORD options)
{
    OBJECT_ATTRIBUTES_X copy;
    ANSI_STRING_X name;
    char path[48];
    LONG status = real_nt_create_file(handle, access, pool_attributes(attributes, &copy, &name, path, sizeof(path)),
                                      io_status, allocation, file_attributes, share, disposition, options);
    note_open('C', attributes, path, status);
    note_patched_open(attributes, handle != NULL ? *handle : NULL, status);
    return status;
}

static LONG nt_open_file(HANDLE *handle, DWORD access, OBJECT_ATTRIBUTES_X *attributes, void *io_status, DWORD share,
                         DWORD options)
{
    OBJECT_ATTRIBUTES_X copy;
    ANSI_STRING_X name;
    char path[48];
    LONG status = real_nt_open_file(handle, access, pool_attributes(attributes, &copy, &name, path, sizeof(path)),
                                    io_status, share, options);
    note_open('O', attributes, path, status);
    note_patched_open(attributes, handle != NULL ? *handle : NULL, status);
    return status;
}

static LONG nt_query_attributes(OBJECT_ATTRIBUTES_X *attributes, void *info)
{
    OBJECT_ATTRIBUTES_X copy;
    ANSI_STRING_X name;
    char path[48];
    LONG status = real_nt_query_attributes(pool_attributes(attributes, &copy, &name, path, sizeof(path)), info);
    note_open('Q', attributes, path, status);
    return status;
}

/* Crash report: an exception nobody handles is written to launcher.log before the system takes the title down
   (code, address, LR, stack pointer, the access-violation address, code-like words on the stack, the last file
   opens). The exception then goes on as before. */
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *e = info->ExceptionRecord;
    const CONTEXT *c = info->ContextRecord;
    const DWORD *sp = (const DWORD *)(DWORD)c->Gpr1;
    LONG i, n;
    DbgPrint("114360: CRASH: exception %08X at %p, LR %08X, SP %08X, thread %p\n", e->ExceptionCode,
             e->ExceptionAddress, c->Lr, (DWORD)c->Gpr1, GetCurrentThreadId());
    if (e->ExceptionCode == 0xC0000005 && e->NumberParameters >= 2) {
        DbgPrint("114360: CRASH: %s of address %08X\n", e->ExceptionInformation[0] ? "write" : "read",
                 (DWORD)e->ExceptionInformation[1]);
    }
    if (sp != NULL && (DWORD)sp >= 0x70000000) {
        for (i = 0, n = 0; i < 256 && n < 24; i++) {
            DWORD w = sp[i];
            if (w >= 0x82000000 && w < 0x85000000 && (w & 3) == 0) {
                DbgPrint("114360: CRASH: stack +%03X: %08X\n", i * 4, w);
                n++;
            }
        }
    }
    n = open_count;
    DbgPrint("114360: CRASH: last file opens (of %d):\n", n);
    for (i = n > OPEN_RING ? n - OPEN_RING : 0; i < n; i++) {
        const OPEN_RECORD *r = &open_ring[i % OPEN_RING];
        DbgPrint("114360: CRASH:   %c %s%s%s %08X\n", r->op, r->name, r->pooled[0] ? " -> " : "", r->pooled, r->status);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Incoming world packets: the client-side compatibility layer. FFXiMain's receive loop (40160203_0:
   0x841B4068) decrypts each datagram and, for each packet in it, calls up to two handlers from tables in the
   game object (FFXIMAIN_PROFILE.game_root): first *(game + packet_first + id * 4) (ids 0x000-0x1FF), then, in
   the normal play state, *(game + packet_handlers + id * 4) (ids below 0x11F), each as
   handler(connection, buffer, packet). By then the client has converted the packet to big-endian field by
   field; the header is u16 size (7 bits, in 4-byte units) | id (9 bits), then u16 sequence. The game fills
   both tables again at every login, so a watcher thread (hook_packets) keeps a wrapper in front of every
   handler. Each wrapper knows its own table slot, calls adjust_packet for packets the 2016 client reads
   differently from the client the server was written for, then the game's handler.

   TRACE_PACKETS 1 logs the first TRACE_PACKETS_COUNT packets in full (development). */
#ifndef TRACE_PACKETS
#define TRACE_PACKETS 0
#endif
#define TRACE_PACKETS_COUNT 300
#define PACKET_IDS 0x200
#define PACKET_MAIN_IDS 0x11F
/* Handlers are called with (connection, buffer, packet); wrappers forward eight register arguments so they
   stay transparent even for an entry that takes more. */
typedef long long (*packet_fn)(long long, long long, long long, long long, long long, long long, long long,
                               long long);
static packet_fn packet_orig[2][PACKET_IDS];   /* [0] first table, [1] main table */

/* A packet for this slot: its header names the slot's id (anything else in the tables is passed through). */
static int is_packet_for(const BYTE *packet, int slot)
{
    return packet != NULL && ((packet[0] << 8 | packet[1]) & 0x1FF) == slot;
}

static void log_packet(int table, int slot, const BYTE *packet)
{
    static LONG logged;
    int size, n, k;
    char line[3 * 32 + 1];
    if (!TRACE_PACKETS || (n = InterlockedIncrement(&logged)) > TRACE_PACKETS_COUNT) {
        return;
    }
    size = (packet[0] >> 1) * 4;
    DbgPrint("114360: packet #%d in 0x%03X size %d (%s)\n", n, slot, size, table ? "main" : "first");
    for (k = 0; k < size && k < 0x200; k++) {
        _snprintf(line + (k % 32) * 3, 4, "%02X ", packet[k]);
        if (k % 32 == 31 || k == size - 1) {
            DbgPrint("114360:   +%03X %s\n", k - k % 32, line);
        }
    }
}

/* Server packets the 2016 client reads differently from the client the server was written for, adjusted
   before the game's handler sees them (fields are big-endian here). Returns 0 to drop the packet.
     0x034 event start (EVENTNUM) without a target (UniqueNo at +0x04 = 0): LandSandBoat sends one right after
       0x01B at every login, with an all-zero body and a size field of 284 bytes (EVENTNUM is 0x68), although
       no event is running on the server. The 2016 client's handler (40160203_0: 0x841EB1A8) starts an empty
       event: the compass disappears and the left stick does not walk until B ends it, and LSB answers that
       EVENTEND with "Invalid GP_CLI_COMMAND_EVENTEND ... Not in an event". A real event always names its NPC
       or the player, so a target of 0 is dropped.
     0x00A zone-in, DeadCounter (u32 at +0xA4): DarkStar 2016 set it only for a dead character; LandSandBoat
       always sends 60 x (6 min + time until the home-point return), 0x03A020 for a living one. Kept only while
       the status byte (+0x1F) says dying (2) or dead (3), as the 2016 client was served.
     0x01E, 0x01F, 0x020 item quantity / item assign / item attributes, container byte at +0x08, +0x0A, +0x0E:
       the 2016 client has 10 bags (inventory .. Mog Safe 2; the 0x01C handler 0x841A15C0 copies 10 sizes) and
       its handlers (0x841A1618, 0x841A1690, 0x841A1740) write to entry container * 81 + slot of a 10 x 81 array
       of 0x2C-byte items without a bounds check. LandSandBoat also has wardrobes 2-8 and the recycle bin
       (containers 10-17); an item there would overwrite the bag sizes and counters after the array. Dropped:
       those bags simply do not exist for this client. */
#define CLIENT_BAGS 10
static int adjust_packet(int slot, BYTE *packet)
{
    if (slot == 0x034 && *(DWORD *)(packet + 4) == 0) {
        DbgPrint("114360: 0x034: event start without a target, dropped\n");
        return 0;
    }
    if ((slot == 0x01E || slot == 0x01F || slot == 0x020)
        && packet[slot == 0x01E ? 0x08 : slot == 0x01F ? 0x0A : 0x0E] >= CLIENT_BAGS) {
        DbgPrint("114360: 0x%03X: item in bag %u (the 2016 client has %u), dropped\n", slot,
                 packet[slot == 0x01E ? 0x08 : slot == 0x01F ? 0x0A : 0x0E], CLIENT_BAGS);
        return 0;
    }
    if (slot == 0x00A && packet[0x1F] != 2 && packet[0x1F] != 3 && *(DWORD *)(packet + 0xA4) != 0) {
        DbgPrint("114360: 0x00A: DeadCounter %u for a living character -> 0\n", *(DWORD *)(packet + 0xA4));
        *(DWORD *)(packet + 0xA4) = 0;
    }
    return 1;
}

/* Packets Project Crystal sends a PS2-tagged client (platform = PS2) in their 2010 layout (docs/crystal-compat.md).
   Only while the preset names PS2: with the game's own tag they arrive in the 2016 layout.

   Fixed-size packets that are only shorter: the 2016 client reads them to their 2016 size, past the end of the
   shorter packet into whatever the receive buffer held (with a 0x50-byte 0x037 seven "sword" status icons
   showed). Such a packet is handed on as a zero-padded copy at the 2016 size; the fields the 2010 layout lacks
   did not exist then, so zero is their meaning. Sizes from a 360-tag LandSandBoat session (work/packets.jsonl)
   against the PS2-tag one (work/ps2-crystal-packets.jsonl). Packets whose 2010 layout also moves fields are
   converted below. */
static const struct {
    WORD slot;
    WORD size;
} full_sizes[] = {
    {0x01B, 0x84},  /* job info: 0x64 in the 2010 layout, the same up to +0x63 */
};

/* The handler sees a packet after the client has byte-swapped its fields to big-endian at their 2016 offsets.
   0x00D (another player) has, from +0x3E, MonstrosityFlags u16, two name ids, Flags5, hitbox size, Flags6 u32
   (+0x44), then the model, GrapIDTbl[9] u16 (+0x48), and the name (+0x5A, up to 16 bytes). The 2010 layout has
   no fields between +0x3E and +0x48: the model is at +0x3E and the name at +0x50 (Crystal's charPc moves them 10
   bytes earlier). So the client swapped the 2010 model and name at the wrong places. Swapping the same fields
   again restores the bytes as sent; the model then moves to +0x48 (swapped there as the client would) and the
   name to +0x5A. Checked on a captured PS2-tag 0x00D: undoing u16 +0x3E, u32 +0x44 and u16 +0x48.. gave the
   model 0x1000/0x2008/0x3008/0x4008/0x5008/0x6015/0x7000/0x8000 and the name "Aza". */
#define CHAR_PC_MODEL 0x48
#define CHAR_PC_NAME 0x5A

static void swap16(BYTE *p)
{
    BYTE t = p[0];
    p[0] = p[1];
    p[1] = t;
}

static void swap32(BYTE *p)
{
    BYTE t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = t;
}

/* The 2016 client's swaps over +0x3E.., for the fields that lie within `size` bytes (an involution). */
static void char_pc_swaps(BYTE *p, int size)
{
    int k;
    if (size >= 0x40) {
        swap16(p + 0x3E);
    }
    if (size >= 0x48) {
        swap32(p + 0x44);
    }
    for (k = CHAR_PC_MODEL; k + 2 <= size && k < CHAR_PC_NAME; k += 2) {
        swap16(p + k);
    }
}

static BYTE *char_pc_to_2016(BYTE *packet, int size, BYTE *out)
{
    static BYTE raw[0x80];
    int model, name, full;
    if (size <= 0x3E || size > (int)sizeof(raw)) {
        return packet;              /* no model or name: the 2010 packet is a prefix of the 2016 one */
    }
    memcpy(raw, packet, size);
    char_pc_swaps(raw, size);       /* the bytes as the server sent them */
    model = size - 0x3E > 18 ? 18 : size - 0x3E;
    name = size > 0x50 ? size - 0x50 : 0;
    if (name > 16) {
        name = 16;
    }
    full = CHAR_PC_NAME + name;
    full = (full + 3) & ~3;
    memset(out, 0, full);
    memcpy(out, packet, 0x3E);      /* +0x00..+0x3D: the same fields in both layouts, already swapped */
    memcpy(out + CHAR_PC_MODEL, raw + 0x3E, model);
    memcpy(out + CHAR_PC_NAME, raw + 0x50, name);
    char_pc_swaps(out, CHAR_PC_NAME);
    out[0] = (BYTE)((full / 4) << 1 | (packet[0] & 1));
    return out;
}

/* The other packets Crystal's console translators (src/map/packets/console) change for the 2010 client, turned
   back into the layout the 2016 client takes (the one LandSandBoat sends any client today):

     0x037  status icons: one high bit per icon (u32 at +0x4C) instead of two (8 bytes of 2-bit fields)
     0x01C  bag sizes: 6 containers, u8 sizes at +0x04 and u16 sizes at +0x0A; 2016 has 10, u8s at +0x04 and
            u16s at +0x14 (0x34 bytes; swap 0x8417B170 swaps 16 u16s, handler 0x841A15C0 reads 10 of each)
     0x0DF  party member: TP as a percent (2016: 0-3000), alliance kind 2 (2016: 5), cut after the kind
     0x0DD, 0x0E2  party list: also TP and kind, a one-byte zone at +0x1F and the name at +0x20; 2016 has a u16
            zone at +0x20, job bytes, and the name at +0x28

   The hook sees each packet after the client's byte swap at the 2016 offsets (see 0x00D), so fields that sit
   inside a 2016 multi-byte field are swapped back first. The 2016 fields involved: 0x037 +0x4C.. are bytes;
   0x01C +0x04..+0x0D are bytes and +0x14..+0x27 u16; 0x0DD +0x20 is a u16 (the 2010 name's first two bytes). */
static DWORD tp_from_percent(DWORD tp)
{
    return tp > 300 ? 3000 : tp * 10;
}

static BYTE group_kind_2016(BYTE kind)
{
    return kind == 2 ? 5 : kind;
}

static void set_size(BYTE *p, int size)
{
    p[0] = (BYTE)((size / 4) << 1 | (p[0] & 1));
}

static BYTE *status_bits_to_2016(BYTE *packet, int size, BYTE *out)
{
    DWORD bits;
    int i;
    if (size != 0x50) {
        return packet;
    }
    memcpy(out, packet, 0x4C);
    memset(out + 0x4C, 0, 0x60 - 0x4C);
    bits = packet[0x4C] | (DWORD)packet[0x4D] << 8 | (DWORD)packet[0x4E] << 16 | (DWORD)packet[0x4F] << 24;
    for (i = 0; i < 32; i++) {
        if (bits >> i & 1) {
            out[0x4C + i / 4] |= (BYTE)(1 << (i % 4) * 2);
        }
    }
    set_size(out, 0x60);
    return out;
}

static BYTE *bag_sizes_to_2016(BYTE *packet, int size, BYTE *out)
{
    int i;
    if (size != 0x18) {
        return packet;
    }
    memset(out, 0, 0x34);
    memcpy(out, packet, 0x0A);          /* header and the six u8 sizes */
    for (i = 0; i < 6; i++) {           /* to big-endian (as swapped) at +0x14 */
        if (0x0A + i * 2 < 0x14) {      /* little-endian as sent */
            out[0x14 + i * 2] = packet[0x0B + i * 2];
            out[0x15 + i * 2] = packet[0x0A + i * 2];
        } else {                        /* +0x14: already swapped as the 2016 layout's first u16 */
            out[0x14 + i * 2] = packet[0x0A + i * 2];
            out[0x15 + i * 2] = packet[0x0B + i * 2];
        }
    }
    set_size(out, 0x34);
    return out;
}

static BYTE *group_attr_to_2016(BYTE *packet, int size, BYTE *out)
{
    if (size != 0x1C) {
        return packet;
    }
    memset(out, 0, 0x28);
    memcpy(out, packet, 0x1C);
    *(DWORD *)(out + 0x10) = tp_from_percent(*(DWORD *)(out + 0x10));
    out[0x18] = group_kind_2016(out[0x18]);
    set_size(out, 0x28);
    return out;
}

static BYTE *group_list_to_2016(BYTE *packet, int size, BYTE *out)
{
    BYTE zone;
    if (size <= 0x24 || size + 8 > 0x100) {
        return packet;
    }
    zone = packet[0x1F];
    memset(out, 0, size + 8);
    memcpy(out, packet, 0x1F);
    *(DWORD *)(out + 0x10) = tp_from_percent(*(DWORD *)(out + 0x10));
    out[0x1C] = group_kind_2016(out[0x1C]);
    out[0x20] = 0;                      /* u16 zone, big-endian as the client swapped it */
    out[0x21] = zone;
    memcpy(out + 0x28, packet + 0x20, size - 0x20);
    swap16(out + 0x28);                 /* the name's first two bytes sat in the swapped 2016 zone field */
    set_size(out, size + 8);
    return out;
}

/* The next three are rebuilt through the bytes as the server sent them. Each 2016 swap routine is copied here
   (FFXiMain's per-packet swaps, table 0x847837F0 by id); each is its own inverse, so applying it to the
   packet the client swapped gives the server's little-endian bytes back, which are converted to the 2016
   layout and swapped as the client would have. A routine swaps where the 2016 layout has fields, even past the
   end of a shorter 2010 packet (the receive buffer holds 0x200 bytes), so the copy takes those bytes too.

     0x017 chat (0x8417B030): u16 at +6. 2010: GM flag at +4, name at +5 (15 bytes), chat type at +0x14,
           text from +0x15. 2016 (LandSandBoat today): type +4, flags +5, zone u16 +6, name +8, text +0x17.
     0x03C shop list (0x8417CDB8): u16 +4, +6; then 12-byte entries from +8 while their start is inside the
           packet (at most 31): u32 price, u16 item, (u8 slot, u8), u16 skill, u16 guild. 2010 entries are 8
           bytes: price, item, slot.
     0x0C8 alliance table (0x84182AF8): 20 entries of 12 bytes from +8, stepping the size check by 10:
           u32 id, u16 index, u16 flags, u16 zone. 2010 entries are 8 bytes: id, index, a flag byte, a
           one-byte zone. Kind 2 (alliance) is 5 in 2016. */
#define SWAPPED_SLACK 0x100

static void swap_header(BYTE *p)
{
    swap16(p);
    swap16(p + 2);
}

static void swap_chat(BYTE *p, int size)
{
    swap_header(p);
    if (size >= 8) {
        swap16(p + 6);
    }
}

static void swap_shop_list(BYTE *p, int size)
{
    int at, n;
    swap_header(p);
    if (size < 6) {
        return;
    }
    swap16(p + 4);
    swap16(p + 6);
    for (at = 8, n = 0; at < size && n < 0x1F; at += 12, n++) {
        swap32(p + at);
        swap16(p + at + 4);
        swap16(p + at + 8);
        swap16(p + at + 10);
    }
}

static void swap_alliance(BYTE *p, int size)
{
    int check, n;
    swap_header(p);
    for (check = 8, n = 0; check < size && n < 20; check += 10, n++) {
        swap32(p + 8 + n * 12);
        swap16(p + 12 + n * 12);
        swap16(p + 14 + n * 12);
        swap16(p + 16 + n * 12);
    }
}

/* Little-endian header (id in the low 9 bits, size / 4 above), as the server sends it. */
static void set_header_le(BYTE *p, int id, int size)
{
    WORD w = (WORD)(id | (size / 4) << 9);
    p[0] = (BYTE)w;
    p[1] = (BYTE)(w >> 8);
}

static BYTE *chat_to_2016(BYTE *packet, int size, BYTE *out)
{
    static BYTE raw[0x100 + SWAPPED_SLACK];
    int text, full;
    if (size < 0x16 || size > 0x100) {
        return packet;
    }
    memcpy(raw, packet, size);
    swap_chat(raw, size);
    for (text = 0; 0x15 + text < size && raw[0x15 + text] != 0 && text < 0x96; text++) {
    }
    full = (0x17 + text + 1 + 3) & ~3;
    memset(out, 0, full);
    memcpy(out, raw, 4);
    out[4] = raw[0x14];                 /* chat type: the 2010 numbers are today's (Crystal's chatKind) */
    out[5] = raw[4] & 1;                /* GM flag */
    memcpy(out + 8, raw + 5, 15);
    memcpy(out + 0x17, raw + 0x15, text);
    set_header_le(out, 0x017, full);
    swap_chat(out, full);
    return out;
}

static BYTE *shop_list_to_2016(BYTE *packet, int size, BYTE *out)
{
    static BYTE raw[0x100 + SWAPPED_SLACK];
    int n, i, full;
    if (size < 0x10 || size > 0x100) {
        return packet;
    }
    n = (size - 8) / 8;
    full = 8 + n * 12;
    if (full > 0x100) {
        n = (0x100 - 8) / 12;
        full = 8 + n * 12;
    }
    memcpy(raw, packet, size + SWAPPED_SLACK);
    swap_shop_list(raw, size);
    memset(out, 0, full);
    memcpy(out, raw, 8);
    for (i = 0; i < n; i++) {
        memcpy(out + 8 + i * 12, raw + 8 + i * 8, 7);    /* price, item, slot; no skill or guild */
    }
    set_header_le(out, 0x03C, full);
    swap_shop_list(out, full);
    return out;
}

static BYTE *alliance_to_2016(BYTE *packet, int size, BYTE *out)
{
    static BYTE raw[0x100 + SWAPPED_SLACK];
    int i;
    if (size != 0xA8) {
        return packet;
    }
    memcpy(raw, packet, size + SWAPPED_SLACK);
    swap_alliance(raw, size);
    memset(out, 0, 0xF8);
    memcpy(out, raw, 8);
    out[4] = group_kind_2016(raw[4]);
    for (i = 0; i < 20; i++) {
        memcpy(out + 8 + i * 12, raw + 8 + i * 8, 7);    /* id, index, flags */
        out[8 + i * 12 + 8] = raw[8 + i * 8 + 7];         /* zone, u16 little-endian */
    }
    set_header_le(out, 0x0C8, 0xF8);
    swap_alliance(out, 0xF8);
    return out;
}

/* 0x0AC ability tables: bit tables, bytes only (the 2016 swap 0x84182528 swaps the header alone). The 2016
   client copies 0xE0 bytes from +4: weapon skills +0x04, job abilities +0x44, pet abilities +0x84, traits
   +0xC4 (today's layout). The 2010 packet holds one 0x80-byte table: job abilities 0-383, traits 0-111, pet
   abilities 512-767, weapon skills 0-255 (Crystal's commandData packs them so). */
static BYTE *ability_tables_to_2016(BYTE *packet, int size, BYTE *out)
{
    if (size != 0x84) {
        return packet;
    }
    memset(out, 0, 0xE4);
    memcpy(out, packet, 4);
    memcpy(out + 0x44, packet + 0x04, 0x30);
    memcpy(out + 0xC4, packet + 0x34, 0x0E);
    memcpy(out + 0x84, packet + 0x44, 0x20);
    memcpy(out + 0x04, packet + 0x64, 0x20);
    set_size(out, 0xE4);
    return out;
}

/* 0x0C9 inspect, general mode (+0x0A = 1): the 2016 handler (0x8419AF00) reads the 2010 layout except the two
   job levels, at +0x24/+0x25 instead of +0x23/+0x24. The swap routine (0x84182FF8, short packets through
   0x84182C28) leaves +0x14..+0x27 alone (the linkshell name, jobs and levels are bytes), so they move as
   they are; +0x23 is then the 16th name byte, which the 2010 name never fills. */
static BYTE *inspect_to_2016(BYTE *packet, int size, BYTE *out)
{
    if (size < 0x28 || packet[0x0A] != 1) {
        return packet;
    }
    memcpy(out, packet, size);
    out[0x25] = packet[0x24];
    out[0x24] = packet[0x23];
    out[0x23] = 0;
    return out;
}

/* 0x028 battle results: a bit stream from byte 5, least significant bit first, which the 2016 swap routine
   (0x8417B8A8) leaves alone past the header. The 2016 decoder (0x842E5CF8) reads today's widths; the 2010
   stream Crystal sends (battle2) has narrower result fields:
     caster 32, targets 6, results 4, command 4, argument 32, info 32; per target: id 32, results 4; per
     result: miss 3, kind 2, sub kind 11 (2016: 12), info 4 (5), scale 5, value 16 (17), message 10, bit 32
     (31), proc flag 1 + (6, 4, value 14 (17), 10), react flag 1 + (6, 4, 14, 10).
   Widened fields keep their value; the 32-bit field loses its top bit, which Crystal leaves clear. Byte 4 holds
   the stream's length in bytes, as today's server writes it. */
typedef struct _BITS {
    BYTE *data;
    int pos, end;   /* in bits */
} BITS;

static DWORD bits_get(BITS *b, int n)
{
    DWORD v = 0;
    int i;
    for (i = 0; i < n; i++, b->pos++) {
        if (b->pos < b->end && (b->data[b->pos >> 3] >> (b->pos & 7) & 1)) {
            v |= 1u << i;
        }
    }
    return v;
}

static void bits_put(BITS *b, DWORD v, int n)
{
    int i;
    for (i = 0; i < n; i++, b->pos++) {
        if (b->pos < b->end && (v >> i & 1)) {
            b->data[b->pos >> 3] |= (BYTE)(1 << (b->pos & 7));
        }
    }
}

#define BATTLE_MAX 0x1FC   /* the largest packet the 7-bit size field allows */

static BYTE *battle_to_2016(BYTE *packet, int size)
{
    static BYTE out[BATTLE_MAX];
    BITS in, w;
    DWORD targets, results, t, r, bytes, full;
    if (size <= 5) {
        return packet;
    }
    memset(out, 0, sizeof(out));
    memcpy(out, packet, 4);
    in.data = packet;
    in.pos = 5 * 8;
    in.end = size * 8;
    w.data = out;
    w.pos = 5 * 8;
    w.end = BATTLE_MAX * 8;
    bits_put(&w, bits_get(&in, 32), 32);                  /* caster */
    targets = bits_get(&in, 6);
    bits_put(&w, targets, 6);
    bits_put(&w, bits_get(&in, 4), 4);                    /* result count */
    bits_put(&w, bits_get(&in, 4), 4);                    /* command */
    bits_put(&w, bits_get(&in, 32), 32);                  /* argument */
    bits_put(&w, bits_get(&in, 32), 32);                  /* info */
    for (t = 0; t < targets && in.pos < in.end; t++) {
        bits_put(&w, bits_get(&in, 32), 32);              /* target */
        results = bits_get(&in, 4);
        bits_put(&w, results, 4);
        for (r = 0; r < results && in.pos < in.end; r++) {
            bits_put(&w, bits_get(&in, 3), 3);            /* miss */
            bits_put(&w, bits_get(&in, 2), 2);            /* kind */
            bits_put(&w, bits_get(&in, 11), 12);          /* sub kind */
            bits_put(&w, bits_get(&in, 4), 5);            /* info */
            bits_put(&w, bits_get(&in, 5), 5);            /* scale */
            bits_put(&w, bits_get(&in, 16), 17);          /* value */
            bits_put(&w, bits_get(&in, 10), 10);          /* message */
            bits_put(&w, bits_get(&in, 32) & 0x7FFFFFFF, 31);
            if (bits_get(&in, 1)) {
                bits_put(&w, 1, 1);
                bits_put(&w, bits_get(&in, 6), 6);
                bits_put(&w, bits_get(&in, 4), 4);
                bits_put(&w, bits_get(&in, 14), 17);      /* proc value */
                bits_put(&w, bits_get(&in, 10), 10);
            } else {
                bits_put(&w, 0, 1);
            }
            if (bits_get(&in, 1)) {
                bits_put(&w, 1, 1);
                bits_put(&w, bits_get(&in, 6), 6);
                bits_put(&w, bits_get(&in, 4), 4);
                bits_put(&w, bits_get(&in, 14), 14);
                bits_put(&w, bits_get(&in, 10), 10);
            } else {
                bits_put(&w, 0, 1);
            }
        }
    }
    bytes = (w.pos + 7) / 8;
    full = (bytes + 3) & ~3;
    if (w.pos > w.end || full > BATTLE_MAX) {
        return packet;
    }
    out[4] = (BYTE)bytes;
    set_size(out, (int)full);
    return out;
}

static BYTE *pad_short_packet(int slot, BYTE *packet)
{
    static BYTE padded[0x100];
    int size = (packet[0] >> 1) * 4, i;
    if (_stricmp(config_platform, "PS2") != 0) {
        return packet;
    }
    switch (slot) {
    case 0x00D:
        return char_pc_to_2016(packet, size, padded);
    case 0x037:
        return status_bits_to_2016(packet, size, padded);
    case 0x01C:
        return bag_sizes_to_2016(packet, size, padded);
    case 0x0DF:
        return group_attr_to_2016(packet, size, padded);
    case 0x0DD:
    case 0x0E2:
        return group_list_to_2016(packet, size, padded);
    case 0x017:
        return chat_to_2016(packet, size, padded);
    case 0x03C:
        return shop_list_to_2016(packet, size, padded);
    case 0x0C8:
        return alliance_to_2016(packet, size, padded);
    case 0x0AC:
        return ability_tables_to_2016(packet, size, padded);
    case 0x0C9:
        return inspect_to_2016(packet, size, padded);
    case 0x028:
        return battle_to_2016(packet, size);
    }
    for (i = 0; i < (int)(sizeof(full_sizes) / sizeof(full_sizes[0])); i++) {
        if (full_sizes[i].slot == slot && size < full_sizes[i].size) {
            memset(padded, 0, full_sizes[i].size);
            memcpy(padded, packet, size);
            set_size(padded, full_sizes[i].size);
            return padded;
        }
    }
    return packet;
}

/* Packet export (launcher.ini `packet_dump = <IPv4>:<port>`, editable in the menu): while set, every server
   packet the client handles is also sent as one UDP datagram to that address, for tools/packet-dump-recv.py.
   Datagram: "FXPD", u8 version 1, u8 direction (0 = server to client), u8 handler table (0 first, 1 main),
   u8 0, u16 packet id, u16 packet size, u32 GetTickCount, then the packet as the handler sees it (header and
   fields already big-endian), all big-endian. The socket comes from xam's NetDll_socket (ordinal 3) and the
   datagrams go through sendto_fixed, so Xenia's sockaddr byte swap is undone as for the game's own. */
#define DUMP_MAGIC 0x46585044   /* "FXPD" */
#define XAM_NETDLL_SOCKET 3
#define XNCALLER_TITLE 1
typedef int (*socket_fn)(DWORD caller, int af, int type, int protocol);
static int dump_socket = -1;

static void dump_packet(int table, int slot, const BYTE *packet)
{
    static int failed;
    BYTE datagram[16 + 0x200], to[16];
    int size = (packet[0] >> 1) * 4;
    if (dump_address == 0 || failed) {
        return;
    }
    if (dump_socket < 0) {
        HANDLE xam = NULL;
        PVOID create = NULL;
        if (XexGetModuleHandle("xam.xex", &xam) < 0 || XexGetProcedureAddress(xam, XAM_NETDLL_SOCKET, &create) < 0
            || create == NULL || (dump_socket = ((socket_fn)create)(XNCALLER_TITLE, AF_INET, SOCK_DGRAM,
                                                                     IPPROTO_UDP)) < 0) {
            DbgPrint("114360: packet dump: no socket\n");
            failed = 1;
            return;
        }
        DbgPrint("114360: packet dump to %u.%u.%u.%u:%u (socket %d)\n", dump_address >> 24, (dump_address >> 16) & 0xFF,
                 (dump_address >> 8) & 0xFF, dump_address & 0xFF, dump_port, dump_socket);
    }
    if (size > 0x200) {
        size = 0x200;
    }
    *(DWORD *)datagram = DUMP_MAGIC;
    datagram[4] = 1;
    datagram[5] = 0;
    datagram[6] = (BYTE)table;
    datagram[7] = 0;
    *(WORD *)(datagram + 8) = (WORD)slot;
    *(WORD *)(datagram + 10) = (WORD)size;
    *(DWORD *)(datagram + 12) = GetTickCount();
    memcpy(datagram + 16, packet, size);
    memset(to, 0, sizeof(to));
    *(WORD *)to = AF_INET;
    *(WORD *)(to + 2) = dump_port;
    *(DWORD *)(to + 4) = dump_address;
    sendto_fixed(XNCALLER_TITLE, dump_socket, (const char *)datagram, 16 + size, 0, to, sizeof(to));
}

/* Ability recasts with the PS2 tag. The 2010 client reads them from 0x062 (CLISTATUS2): 31 u32 entries from +4,
   seconds | (charge reduction << 16) | (recast id << 24), and Project Crystal fills them there (fillRecasts) and
   never sends 0x119. The 2016 client's 0x062 handler (0x841E7B50) reads only the skills at +0x80 and takes
   recasts from 0x119 (handler 0x841E7B80): 31 8-byte entries from +4, the first u32 packed the same way (timer,
   Calc1, timer id), the second Calc2 and padding, then the mount recast at +0xFC; size 0x104. Both swap
   routines (0x8417EB38, 0x841857A8) swap those words as u32s, so each 0x119 entry is the 0x062 word with
   Calc1 cleared (LandSandBoat leaves it 0), already in the client's byte order. The ids stay as sent: the
   2010 data's ability table uses the 2010 ids Crystal maps to. */
#define RECAST_SLOTS 31
#define RECAST_SIZE 0x104

static void recasts_from_status(long long a, long long b, const BYTE *status, long long d, long long e,
                                long long f, long long g, long long h)
{
    static BYTE recast[RECAST_SIZE];
    int i;
    if ((status[0] >> 1) * 4 < 4 + RECAST_SLOTS * 4 || packet_orig[1][0x119] == NULL) {
        return;
    }
    memset(recast, 0, sizeof(recast));
    recast[0] = (BYTE)((RECAST_SIZE / 4) << 1 | 0x119 >> 8);
    recast[1] = (BYTE)(0x119 & 0xFF);
    recast[2] = status[2];
    recast[3] = status[3];
    for (i = 0; i < RECAST_SLOTS; i++) {
        *(DWORD *)(recast + 4 + i * 8) = *(const DWORD *)(status + 4 + i * 4) & 0xFF00FFFF;
    }
    packet_orig[1][0x119](a, b, (long long)(DWORD)recast, d, e, f, g, h);
}

static long long packet_call(int table, int slot, long long a, long long b, long long c, long long d,
                             long long e, long long f, long long g, long long h)
{
    BYTE *packet = (BYTE *)(DWORD)c;
    long long result;
    if (is_packet_for(packet, slot)) {
        log_packet(table, slot, packet);
        dump_packet(table, slot, packet);
        packet = pad_short_packet(slot, packet);
        c = (long long)(DWORD)packet;
        if (!adjust_packet(slot, packet)) {
            return 0;
        }
    }
    result = packet_orig[table][slot](a, b, c, d, e, f, g, h);
    if (table == 1 && slot == 0x062 && _stricmp(config_platform, "PS2") == 0 && is_packet_for(packet, slot)) {
        recasts_from_status(a, b, packet, d, e, f, g, h);
    }
    return result;
}

/* One wrapper per table slot (the slot is compiled in, as for the network slot wrappers above). */
#define PKTW(t, i, k) static long long pkt_##t##_##i##_##k(long long a, long long b, long long c, long long d, \
    long long e, long long f, long long g, long long h) { return packet_call(t, (i) * 8 + (k), a, b, c, d, e, f, g, h); }
#define PKTW8(t, i) PKTW(t, i, 0) PKTW(t, i, 1) PKTW(t, i, 2) PKTW(t, i, 3) PKTW(t, i, 4) PKTW(t, i, 5) \
    PKTW(t, i, 6) PKTW(t, i, 7)
#define PKTW64(t, a, b, c, d, e, f, g, h) PKTW8(t, a) PKTW8(t, b) PKTW8(t, c) PKTW8(t, d) PKTW8(t, e) \
    PKTW8(t, f) PKTW8(t, g) PKTW8(t, h)
#define PKTP8(t, i) pkt_##t##_##i##_0, pkt_##t##_##i##_1, pkt_##t##_##i##_2, pkt_##t##_##i##_3, \
    pkt_##t##_##i##_4, pkt_##t##_##i##_5, pkt_##t##_##i##_6, pkt_##t##_##i##_7
#define PKTP64(t, a, b, c, d, e, f, g, h) PKTP8(t, a), PKTP8(t, b), PKTP8(t, c), PKTP8(t, d), PKTP8(t, e), \
    PKTP8(t, f), PKTP8(t, g), PKTP8(t, h)
#define PKT_TABLE(t) \
    PKTW64(t, 0, 1, 2, 3, 4, 5, 6, 7) PKTW64(t, 8, 9, 10, 11, 12, 13, 14, 15) \
    PKTW64(t, 16, 17, 18, 19, 20, 21, 22, 23) PKTW64(t, 24, 25, 26, 27, 28, 29, 30, 31) \
    PKTW64(t, 32, 33, 34, 35, 36, 37, 38, 39) PKTW64(t, 40, 41, 42, 43, 44, 45, 46, 47) \
    PKTW64(t, 48, 49, 50, 51, 52, 53, 54, 55) PKTW64(t, 56, 57, 58, 59, 60, 61, 62, 63) \
    static const packet_fn packet_wrappers_##t[PACKET_IDS] = { \
    PKTP64(t, 0, 1, 2, 3, 4, 5, 6, 7), PKTP64(t, 8, 9, 10, 11, 12, 13, 14, 15), \
    PKTP64(t, 16, 17, 18, 19, 20, 21, 22, 23), PKTP64(t, 24, 25, 26, 27, 28, 29, 30, 31), \
    PKTP64(t, 32, 33, 34, 35, 36, 37, 38, 39), PKTP64(t, 40, 41, 42, 43, 44, 45, 46, 47), \
    PKTP64(t, 48, 49, 50, 51, 52, 53, 54, 55), PKTP64(t, 56, 57, 58, 59, 60, 61, 62, 63) };
PKT_TABLE(0)
PKT_TABLE(1)
static const packet_fn *const packet_wrappers[2] = {packet_wrappers_0, packet_wrappers_1};

static int is_handler(DWORD p)
{
    return p >= ffximain_profile->code_start && p < ffximain_profile->code_end && (p & 3) == 0;
}

/* Put table `t`'s per-slot wrappers in front of every handler set in it (entries pointing into FFXiMain's
   code); returns how many. */
static int hook_table(volatile DWORD *table, int count, int t)
{
    int id, hooked = 0;
    for (id = 0; id < count; id++) {
        if (is_handler(table[id])) {
            packet_orig[t][id] = (packet_fn)table[id];
            table[id] = (DWORD)packet_wrappers[t][id];
            hooked++;
        }
    }
    return hooked;
}

static int table_used(volatile DWORD *table, int count)
{
    int id, used = 0;
    for (id = 0; id < count; id++) {
        used += is_handler(table[id]);
    }
    return used;
}

/* Wait for FFXiMain to build its game object and fill its handler tables, then keep every handler hooked for
   as long as the game runs: it fills the tables again at each login (entries already wrapped point into the
   launcher and are skipped). */
static DWORD WINAPI hook_packets(LPVOID unused)
{
    DWORD last_game = 0;
    for (;;) {
        DWORD game = *(volatile DWORD *)ffximain_profile->game_root;
        if (game != 0) {
            volatile DWORD *first = (volatile DWORD *)(game + ffximain_profile->packet_first);
            volatile DWORD *main_table = (volatile DWORD *)(game + ffximain_profile->packet_handlers);
            int a = hook_table(first, PACKET_IDS, 0), b = hook_table(main_table, PACKET_MAIN_IDS, 1);
            if (a + b != 0 || game != last_game) {
                DbgPrint("114360: game object %08X: hooked %d first and %d main packet handlers\n", game, a, b);
                last_game = game;
            }
        }
        Sleep(50);
    }
}

/* Equipment models of data newer than the client. FFXiMain maps a model number to a game file through a
   table per race and slot of (first file ID, model count) blocks (FFXIMAIN_PROFILE.model_races); 40160203_0
   has 5 blocks per slot and knows only the files of 2016, and of its three readers the first counts only four
   of the five blocks when it checks a model number. A revision with newer data carries the newer table as
   rev:\model-table.bin (tools/ffxi-model-table.py): "FXMT", u32 races (8), u32 slots (9), u32 blocks (5-8),
   then the (u32 first, u32 count) pairs, all big-endian. 2026 data needs 6 blocks.

   The table is kept here, and each reader's check and cascade are replaced by a jump into a stub written over
   its own unrolled code (install_model_lookup): the stub calls model_look (the model number, or model 0 when it
   is past every block) and model_file (the file, or none), then resumes the reader with the result. The
   client's own tables get the first five blocks, which the readers still use to find a slot's entry and to see
   whether its first block has a file. A table the launcher does not hold (other races: NPC models) keeps the
   client's five-block rules. Without the file the client keeps its own tables and code. */
#define MODEL_TABLE_FILE REVISION_DRIVE "\\model-table.bin"
#define MODEL_MAGIC 0x46584D54   /* "FXMT" */
#define MODEL_RACES 8
#define MODEL_SLOTS 9
#define MODEL_MAX_BLOCKS 8
#define MODEL_ENTRY (5 * 8)      /* the client's entry: 5 pairs */

static DWORD model_wide[MODEL_RACES][MODEL_SLOTS][MODEL_MAX_BLOCKS][2];
static int model_blocks;

/* The launcher's blocks for a client table entry, or NULL for a table it does not hold. */
static const DWORD (*model_blocks_for(const BYTE *entry))[2]
{
    int race;
    for (race = 0; race < MODEL_RACES; race++) {
        const BYTE *table = *(const BYTE **)(ffximain_profile->model_races + (race + 1) * 4);
        if (entry >= table && entry < table + MODEL_SLOTS * MODEL_ENTRY && (entry - table) % MODEL_ENTRY == 0) {
            return model_wide[race][(entry - table) / MODEL_ENTRY];
        }
    }
    return NULL;
}

/* The model number (look & 0xFFF) kept when it lies within the slot's blocks, else model 0. */
static DWORD model_look(const BYTE *entry, DWORD look)
{
    const DWORD (*blocks)[2] = model_blocks_for(entry);
    DWORD total = 0, model = look & 0xFFF;
    int k, n = blocks != NULL ? model_blocks : 5;
    for (k = 0; k < n; k++) {
        total += blocks != NULL ? blocks[k][1] : ((const DWORD *)entry)[k * 2 + 1];
    }
    return model < total ? look : look & ~0xFFF;
}

/* The file of the model, or 0xFFFFFFFF when its block has none (the reader then skips the slot). */
static DWORD model_file(const BYTE *entry, DWORD look)
{
    const DWORD (*blocks)[2] = model_blocks_for(entry);
    DWORD model = look & 0xFFF, first, count;
    int k, n = blocks != NULL ? model_blocks : 5;
    for (k = 0; k < n; k++) {
        first = blocks != NULL ? blocks[k][0] : ((const DWORD *)entry)[k * 2];
        count = blocks != NULL ? blocks[k][1] : ((const DWORD *)entry)[k * 2 + 1];
        if (model < count) {
            return first != 0 ? first + model : 0xFFFFFFFF;
        }
        model -= count;
    }
    return 0xFFFFFFFF;
}

static DWORD branch_to(DWORD from, DWORD to)
{
    return 0x48000000 | ((to - from) & 0x03FFFFFC);                       /* b to */
}

/* The stubs, over the readers' code (r30 = the client's entry, r27 = the look):
     check:    r3 = r30; r4 = r27; call model_look; r27 = r3; b check_resume
     cascade:  r3 = r30; r4 = r27; call model_file; if r3 == -1 b skip; r30 = r3; b cascade_resume
   Only volatile registers change; the readers' state lives in r14-r31. Written before FFXiMain first runs the
   readers, so Xenia translates the new code; the stubs are in FFXiMain's own code section, so a console can
   run them too. */
static void write_stub(DWORD at, DWORD target, DWORD resume, DWORD skip, DWORD result_reg)
{
    volatile DWORD *code = (volatile DWORD *)at;
    int n = 0;
    code[n++] = 0x7FC3F378;                                               /* or r3, r30, r30 */
    code[n++] = 0x7F64DB78;                                               /* or r4, r27, r27 */
    code[n++] = 0x3D800000 | (target >> 16);                              /* lis r12, target@h */
    code[n++] = 0x618C0000 | (target & 0xFFFF);                           /* ori r12, r12, target@l */
    code[n++] = 0x7D8903A6;                                               /* mtctr r12 */
    code[n++] = 0x4E800421;                                               /* bctrl */
    if (skip != 0) {
        code[n] = 0x2F03FFFF;                                             /* cmpwi cr6, r3, -1 */
        n++;
        code[n] = 0x419A0000 | ((skip - (at + n * 4)) & 0xFFFC);          /* beq cr6, skip */
        n++;
    }
    code[n++] = 0x7C601B78 | result_reg << 16;                            /* or result_reg, r3, r3 */
    code[n] = branch_to(at + n * 4, resume);
    n++;
    for (; n > 0; n -= 8) {
        __dcbst(0, (void *)(code + n - 1));
    }
    __sync();
}

static void install_model_lookup(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        const DWORD *site = ffximain_profile->model_sites[i];
        if (site[0] == 0) {
            continue;
        }
        write_stub(site[0], (DWORD)model_look, site[1], 0, 27);
        write_stub(site[2], (DWORD)model_file, site[3], site[4], 30);
    }
}

/* Packet size check: which server packets differ from the layouts the 2016 client knows. The receive loop byte-
   swaps every packet through a dispatcher (FFXIMAIN_PROFILE.swap_dispatch) that calls the packet's own routine
   and returns the size its layout expects; the loop only writes a mismatch into a buffer nobody prints. The
   dispatcher is replaced by swap_check, which does the same lookup and call and logs each id and size pair
   that differs from the expected size once:
     114360: size 0x01C: 0x64 bytes, 2016 layout 0x48
   A longer packet carries fields the 2016 client does not read; a shorter one leaves fields it reads unset. */
/* Today's servers (LandSandBoat) send a few packets whose fields moved since 2016, not just grew. Those are put
   into the 2016 layout here, in place and still little-endian, before the client's swap; a 2026 packet is
   never shorter than its 2016 layout, so this stays inside the packet. The header keeps the sent size, which
   the receive loop steps by. Known by their size, so a 2016-era server's packets are left alone.
     0x01C (0x64 bytes): u8[18] at +0x04 and u16[18] at +0x24; the 2016 layout has 16 slots of each, u8 at
           +0x04 and u16 at +0x14 (swap 0x8417B170), of which the handler reads 10. Bags 10-17 (wardrobes 2-8,
           recycle bin) do not exist in 2016; their items are dropped in adjust_packet. */
static void raw_to_2016(int slot, BYTE *p, int size)
{
    if (slot == 0x01C && size == 0x64) {
        memmove(p + 0x14, p + 0x24, 16 * 2);
        memset(p + 0x34, 0, 0x64 - 0x34);
    }
}

typedef long long (*swap_fn)(long long packet, long long size, long long flag);
#define SWAP_IDS 0x11F
#define SWAP_SEEN 4
static WORD swap_seen[SWAP_IDS][SWAP_SEEN];   /* sizes already reported per id */

static long long swap_check(long long id, long long packet, long long size, long long flag)
{
    int slot = (int)(id & 0xFFFF), i;
    swap_fn fn;
    long long expected;
    if (slot >= SWAP_IDS) {
        return size;
    }
    fn = ((swap_fn *)ffximain_profile->swap_table)[slot];
    if (fn == NULL) {
        return size;
    }
    raw_to_2016(slot, (BYTE *)(DWORD)packet, (int)size);
    expected = fn(packet, size, flag);
    if (expected != size) {
        for (i = 0; i < SWAP_SEEN && swap_seen[slot][i] != 0 && swap_seen[slot][i] != (WORD)size; i++) {
        }
        if (i < SWAP_SEEN && swap_seen[slot][i] == 0) {
            swap_seen[slot][i] = (WORD)size;
            DbgPrint("114360: size 0x%03X: 0x%X bytes, 2016 layout 0x%X\n", slot, (int)size, (int)expected);
        }
    }
    return expected;
}

static void install_size_check(void)
{
    if (ffximain_profile->swap_dispatch != 0) {
        write_jump(ffximain_profile->swap_dispatch, (DWORD)swap_check);
    }
}

/* Title screen: the 2016 client prints "ver.<patch.ver>" at the bottom right (FFXIMAIN_PROFILE.title_*). The
   launcher adds what it is running, ending at the same right edge. The copyright line ("(c) 2002-2026 SQUARE
   ENIX ...") runs across the screen about 2.8 lines above "ver.", so one line goes between them and the rest
   above it (rows counted up from "ver."):
     7  PolCore X2U 20110726_A
     6  Client XB2 40160203_0 EMULATION
     5  Data PS2-20100904_2-R2, DIRECT
     4  Network ProjCrystalPersonal: PS2, PlayOnline 20100601_1
        (c) 2002-2026 SQUARE ENIX CO., LTD. All Rights Reserved.
     1  Thunks PolCore 4E240911 x6, FFXi 554CA1CB x3, FFXiMain 56B0E65D x12
     0  ver.40160203_0
   The game's own strcat of patch.ver becomes a stub that calls title_fill and jumps over the rest of the
   block; the draw call goes through a trampoline in that skipped rest to title_draw. Both are inside
   FFXiMain's code section, so a console runs them too. The font skips bytes below 0x20 (no newlines), so each
   line is its own draw. */
#define TITLE_LINES 5
#define TITLE_TEXT 96
#define TITLE_BUFFER 0x100   /* the game's buffer at r1+0xE0 */
typedef void (*text_draw_fn)(int x, int y, const char *text, DWORD colour);
typedef int (*text_width_fn)(const char *text);
static char title_lines[TITLE_LINES][TITLE_TEXT] = {"", "", "", "", "Thunks"};   /* top to bottom */
static const int title_rows[TITLE_LINES] = {7, 6, 5, 4, 1};
static int title_line_height;

/* The stack lines for the chosen preset (main, after check_preset); the thunk line grows in patch_module. */
static void title_stack(const MENU_PRESET *p, const MENU_CHECK *check)
{
    int i;
    _snprintf(title_lines[0], TITLE_TEXT, "PolCore %s", check->pol);
    _snprintf(title_lines[1], TITLE_TEXT, "Client %s", check->client);
    _snprintf(title_lines[2], TITLE_TEXT, "Data %s, %s", p->revision, check->storage);
    _snprintf(title_lines[3], TITLE_TEXT, "Network %s: %s, PlayOnline %s", p->name,
              p->platform[0] ? p->platform : "XB2", p->pol_version[0] ? p->pol_version : POL_VERSION_DEFAULT);
    for (i = 0; i < TITLE_LINES; i++) {
        title_lines[i][TITLE_TEXT - 1] = '\0';
    }
}

/* patch_module: one module's build and how many of its import thunks the launcher redirected. */
static void title_thunk(const char *name, DWORD stamp, int count)
{
    char *line = title_lines[TITLE_LINES - 1];
    size_t used = strlen(line);
    _snprintf(line + used, TITLE_TEXT - used, "%s %.*s %08X x%d", used > 6 ? "," : "",
              (int)strcspn(name, "."), name, stamp, count);
    line[TITLE_TEXT - 1] = '\0';
}

/* Height of one text line: the font's "H" glyph, whose rectangle is shorts left, right, top, bottom at +0x20. */
static int title_height(void)
{
    DWORD font = *(volatile DWORD *)ffximain_profile->text_font;
    DWORD table = font != 0 ? *(volatile DWORD *)font : 0;
    DWORD glyph = table != 0 ? *(volatile DWORD *)(table + ('H' - 0x20) * 4) : 0;
    int h = glyph != 0 ? *(volatile short *)(glyph + 0x26) - *(volatile short *)(glyph + 0x24) : 0;
    DbgPrint("114360: title text line height %d\n", h);
    return h >= 6 && h <= 48 ? h + 2 : 14;
}

/* Replaces the strcat: buffer holds "ver.", append patch.ver as the game does. */
static void title_fill(char *buffer)
{
    DWORD version = *(volatile DWORD *)ffximain_profile->title_version;
    size_t used = strlen(buffer);
    if (version != 0 && used < TITLE_BUFFER - 1) {
        strncpy(buffer + used, (const char *)(version + 0x10), TITLE_BUFFER - 1 - used);
        buffer[TITLE_BUFFER - 1] = '\0';
    }
}

static void title_draw(int x, int y, const char *text, DWORD colour)
{
    text_draw_fn draw = (text_draw_fn)ffximain_profile->text_draw;
    text_width_fn width = (text_width_fn)ffximain_profile->text_width;
    int right = (short)x + width(text), i;
    draw(x, y, text, colour);
    if (title_line_height == 0) {
        title_line_height = title_height();
    }
    for (i = 0; i < TITLE_LINES; i++) {
        const char *s = title_lines[i];
        if (s[0] != '\0') {
            draw(right - width(s), (short)y - title_rows[i] * title_line_height, s, colour);
        }
    }
}

static void install_title(void)
{
    volatile DWORD *code = (volatile DWORD *)ffximain_profile->title_fill;
    DWORD at = ffximain_profile->title_fill, trampoline = at + 6 * 4, call = ffximain_profile->title_draw_call;
    if (at == 0) {
        return;
    }
    code[0] = 0x386100E0;                                                 /* addi r3, r1, 0xE0 */
    code[1] = 0x3D800000 | ((DWORD)title_fill >> 16);                     /* lis r12, title_fill@h */
    code[2] = 0x618C0000 | ((DWORD)title_fill & 0xFFFF);                  /* ori r12, r12, title_fill@l */
    code[3] = 0x7D8903A6;                                                 /* mtctr r12 */
    code[4] = 0x4E800421;                                                 /* bctrl */
    code[5] = branch_to(at + 5 * 4, ffximain_profile->title_resume);      /* b resume */
    code[6] = 0x3D800000 | ((DWORD)title_draw >> 16);                     /* lis r12, title_draw@h */
    code[7] = 0x618C0000 | ((DWORD)title_draw & 0xFFFF);                  /* ori r12, r12, title_draw@l */
    code[8] = 0x7D8903A6;                                                 /* mtctr r12 */
    code[9] = 0x4E800420;                                                 /* bctr */
    *(volatile DWORD *)call = branch_to(call, trampoline) | 1;            /* bl trampoline */
    __dcbst(0, (void *)code);
    __dcbst(0, (void *)(code + 9));
    __dcbst(0, (void *)call);
    __sync();
    DbgPrint("114360: title version line patched\n");
}

static void load_model_table(void)
{
    DWORD header[4];
    int race, slot, k;
    FILE *f = fopen(MODEL_TABLE_FILE, "rb");
    if (f == NULL) {
        DbgPrint("114360: no %s; FFXiMain keeps its own model table\n", MODEL_TABLE_FILE);
        return;
    }
    if (fread(header, sizeof(header), 1, f) != 1 || header[0] != MODEL_MAGIC || header[1] != MODEL_RACES
        || header[2] != MODEL_SLOTS || header[3] < 5 || header[3] > MODEL_MAX_BLOCKS) {
        DbgPrint("114360: %s is not a 8 x 9 x 5-8 model table\n", MODEL_TABLE_FILE);
        fclose(f);
        return;
    }
    model_blocks = (int)header[3];
    for (race = 0; race < MODEL_RACES; race++) {
        for (slot = 0; slot < MODEL_SLOTS; slot++) {
            if (fread(model_wide[race][slot], 8 * model_blocks, 1, f) != 1) {
                DbgPrint("114360: %s is short\n", MODEL_TABLE_FILE);
                fclose(f);
                model_blocks = 0;
                return;
            }
        }
    }
    fclose(f);
    for (race = 0; race < MODEL_RACES; race++) {
        /* Pointer list entry race + 1: the list's entry 0 is a second pointer to race 1's table. */
        DWORD *table = *(DWORD **)(ffximain_profile->model_races + (race + 1) * 4);
        for (slot = 0; slot < MODEL_SLOTS; slot++) {
            for (k = 0; k < 5; k++) {
                table[(slot * 5 + k) * 2] = model_wide[race][slot][k][0];
                table[(slot * 5 + k) * 2 + 1] = model_wide[race][slot][k][1];
            }
        }
    }
    install_model_lookup();
    DbgPrint("114360: model tables of %d races, %d blocks per slot, from %s; lookups patched\n", MODEL_RACES,
             model_blocks, MODEL_TABLE_FILE);
}

static LONG load_hook(const char *name, DWORD flags, DWORD min_version, HANDLE *module);

/* Import thunk redirections. Each module is patched as it is loaded, before its first instruction runs: the
   launcher loads PolCore itself; PolCore loads FFXi.dll (or the Viewer) and FFXi.dll loads FFXiMain, both
   through their XexLoadImage thunks, which lead to load_hook. Thunk addresses come from the module's build
   (engine.h). A hook without a module applies to every module that imports that export. */
typedef struct _THUNK_HOOK {
    const char *module;   /* file name, or NULL for every module */
    BYTE library;         /* ENGINE_XAM or ENGINE_KERNEL */
    WORD ordinal;
    void *hook;
    void **real;          /* receives the export's address; NULL when the hook calls it directly */
} THUNK_HOOK;
static const THUNK_HOOK thunk_hooks[] = {
    {NULL, ENGINE_XAM, 601, (void *)content_create, (void **)&real_content_create},   /* XamContentCreateEx */
    {NULL, ENGINE_XAM, 602, (void *)content_close, (void **)&real_content_close},     /* XamContentClose */
    {NULL, ENGINE_KERNEL, 409, (void *)load_hook, NULL},                               /* XexLoadImage */
    {"PolCore.xex", ENGINE_XAM, 420, (void *)launch_title_hook, (void **)&real_launch_title},   /* XamLoaderLaunchTitle */
    {"PolCore.xex", ENGINE_XAM, 611, (void *)content_launch_hook, (void **)&real_content_launch}, /* XamContentLaunchImage */
    {NULL, ENGINE_XAM, 27, (void *)wsa_error, (void **)&real_wsa_error},              /* NetDll_WSAGetLastError */
    {"FFXiMain.dll", ENGINE_XAM, 15, (void *)select_fixed, (void **)&real_select},     /* NetDll_select */
    {"FFXiMain.dll", ENGINE_XAM, 6, (void *)ioctl_fixed, (void **)&real_ioctl},        /* NetDll_ioctlsocket */
    {"FFXiMain.dll", ENGINE_XAM, 18, (void *)recv_fixed, (void **)&real_recv},         /* NetDll_recv */
    {"FFXiMain.dll", ENGINE_XAM, 24, (void *)sendto_fixed, (void **)&real_sendto},     /* NetDll_sendto */
    {"FFXiMain.dll", ENGINE_XAM, 22, (void *)send_fixed, (void **)&real_send},         /* NetDll_send */
    {"FFXiMain.dll", ENGINE_XAM, 20, (void *)recvfrom_fixed, (void **)&real_recvfrom}, /* NetDll_recvfrom */
    {"FFXiMain.dll", ENGINE_KERNEL, 210, (void *)nt_create_file, (void **)&real_nt_create_file},     /* NtCreateFile */
    {"FFXiMain.dll", ENGINE_KERNEL, 223, (void *)nt_open_file, (void **)&real_nt_open_file},         /* NtOpenFile */
    {"FFXiMain.dll", ENGINE_KERNEL, 231, (void *)nt_query_attributes, (void **)&real_nt_query_attributes}, /* NtQueryFullAttributesFile */
    {"FFXiMain.dll", ENGINE_KERNEL, 240, (void *)nt_read_file, (void **)&real_nt_read_file},         /* NtReadFile */
    {"FFXiMain.dll", ENGINE_KERNEL, 207, (void *)nt_close, (void **)&real_nt_close},                 /* NtClose */
};

/* Look up every hooked export once. Returns 0 when one is missing. */
static int resolve_exports(void)
{
    static const char *const libraries[] = {"xam.xex", "xboxkrnl.exe"};
    int i;
    for (i = 0; i < sizeof(thunk_hooks) / sizeof(thunk_hooks[0]); i++) {
        HANDLE library = NULL;
        PVOID address = NULL;
        const THUNK_HOOK *h = &thunk_hooks[i];
        if (h->real == NULL) {
            continue;
        }
        if (XexGetModuleHandle(libraries[h->library], &library) < 0
            || XexGetProcedureAddress(library, h->ordinal, &address) < 0 || address == NULL) {
            DbgPrint("114360: %s ordinal %u not found\n", libraries[h->library], h->ordinal);
            return 0;
        }
        *h->real = address;
    }
    return 1;
}

/* File name of a module path: "rev:\PolCore.xex", "000100:FFXi.dll", "R000100:\FFXiMain.dll". */
static const char *module_name(const char *path)
{
    const char *base = strrchr(path, '\\');
    if (base == NULL) {
        base = strchr(path, ':');
    }
    return base != NULL ? base + 1 : path;
}

/* Redirect a just-loaded module's thunks. Returns 0 for a build the launcher does not know, left unpatched
   (the start screen refuses such revisions, so this only happens to modules it does not check, such as the
   Viewer's). FFXiMain also selects the FFXiMain profile. */
static int patch_module(const char *path, HANDLE module)
{
    const char *name = module_name(path);
    DWORD stamp = engine_loaded_timestamp(module);
    const ENGINE_MODULE *build = engine_module(name, stamp);
    int i, count = 0;
    if (build == NULL) {
        DbgPrint("114360: %s (time stamp %08X): unknown build, not patched\n", name, stamp);
        return 0;
    }
    for (i = 0; i < sizeof(thunk_hooks) / sizeof(thunk_hooks[0]); i++) {
        const THUNK_HOOK *h = &thunk_hooks[i];
        DWORD thunk;
        if (h->module != NULL && _stricmp(h->module, name) != 0) {
            continue;
        }
        thunk = engine_thunk(build, h->library, h->ordinal);
        if (thunk != 0) {
            write_jump(thunk, (DWORD)h->hook);
            count++;
        }
    }
    if (_stricmp(name, "FFXiMain.dll") == 0) {
        ffximain_profile = engine_ffximain(stamp);
        if (ffximain_profile != NULL) {
            load_model_table();
            install_size_check();
            install_title();
            CreateThread(NULL, 0, hook_packets, NULL, 0, NULL);
        }
    }
    title_thunk(name, stamp, count);
    DbgPrint("114360: %s (time stamp %08X): %d import thunks redirected\n", name, stamp, count);
    return 1;
}

static LONG load_hook(const char *name, DWORD flags, DWORD min_version, HANDLE *module)
{
    LONG status = XexLoadImage(name, flags, min_version, module);
    DbgPrint("114360: XexLoadImage(%s) status=%08X\n", name, status);
    if (status >= 0 && name != NULL && module != NULL) {
        patch_module(name, *module);
    }
    return status;
}

/* SQINET start-up. FFXiMain's network calls into PolCore (for example slot +0x374, PolCore 0x8246D018) refuse
   with -2071 until PolCore's SQINET layer is up (flag 0x824B9BDC, set by 0x8246CC98). PolCore's own start
   path for it is POLCORE_PROFILE.sqinet_start (20110726_A: 0x82453128: lock, 0x82453050, unlock); its other
   caller is the Viewer's connection code (0x82454D80), so without the Viewer nothing runs it and the game
   stops on POL-2071 after its terms screen. Call it once PolCore has filled its interface table. */
typedef int (*net_start_fn)(void);
/* In a Viewer run (viewer_mode) the Viewer brings SQINET up itself, with its own connection mode; a Viewer
   run with the launcher's early start crashed (PolViewer 0x835975E4). */

static void start_sqinet(void)
{
    int status;
    if (viewer_mode) {
        DbgPrint("114360: Viewer run; leaving SQINET start to the Viewer\n");
        return;
    }
    status = ((net_start_fn)polcore_profile->sqinet_start)();
    DbgPrint("114360: PolCore SQINET start (0x%08X) returned %d\n", polcore_profile->sqinet_start, status);
}

static DWORD WINAPI trace_table(LPVOID unused)
{
    volatile PVOID *table = (volatile PVOID *)polcore_profile->table;
    int i, j, net_done = 0, sqinet_done = 0;
    /* Runs until every hook is in place, polling every 1 ms: PolCore fills the table and starts using it at
       once. */
    for (i = 0; i < 600000 && (orig_f24 == NULL || orig_1008 == NULL || orig_1834 == NULL || orig_ea0 == NULL
                               || orig_ac4 == NULL || orig_694 == NULL || !net_done || !sqinet_done); i++) {
        if (orig_694 == NULL && table[0x694 / 4] != NULL) {
            orig_694 = (slot_694_fn)table[0x694 / 4];
            table[0x694 / 4] = (PVOID)language_slot;
            DbgPrint("114360: hooked table+0x694 (was %p, config -lang %d)\n", orig_694, config_lang);
        }
        if (orig_f24 == NULL && table[0xF24 / 4] != NULL) {
            orig_f24 = (slot_f24_fn)table[0xF24 / 4];
            table[0xF24 / 4] = (PVOID)trace_f24;
            DbgPrint("114360: hooked table+0xF24 (was %p)\n", orig_f24);
        }
        if (orig_1008 == NULL && table[0x1008 / 4] != NULL) {
            orig_1008 = (slot_1008_fn)table[0x1008 / 4];
            table[0x1008 / 4] = (PVOID)trace_1008;
            DbgPrint("114360: hooked table+0x1008 (was %p)\n", orig_1008);
        }
        if (orig_1834 == NULL && table[0x1834 / 4] != NULL) {
            orig_1834 = (slot_1834_fn)table[0x1834 / 4];
            table[0x1834 / 4] = (PVOID)pad_port;
            DbgPrint("114360: hooked table+0x1834 (was %p)\n", orig_1834);
        }
        if (orig_ea0 == NULL && table[POL_CREDENTIAL_SLOT / 4] != NULL) {
            orig_ea0 = (slot_buf_fn)table[POL_CREDENTIAL_SLOT / 4];
            table[POL_CREDENTIAL_SLOT / 4] = (PVOID)pol_credential;
            DbgPrint("114360: hooked table+0xEA0 (was %p)\n", orig_ea0);
        }
        if (orig_fac == NULL && table[POL_RANDOM_SLOT / 4] != NULL) {
            orig_fac = (slot_buf_fn)table[POL_RANDOM_SLOT / 4];
            table[POL_RANDOM_SLOT / 4] = (PVOID)pol_random;
            DbgPrint("114360: hooked table+0xFAC (was %p)\n", orig_fac);
        }
        if (orig_ac4 == NULL && table[POL_TIME_SLOT / 4] != NULL) {
            orig_ac4 = (slot_buf_fn)table[POL_TIME_SLOT / 4];
            table[POL_TIME_SLOT / 4] = (PVOID)pol_time;
            DbgPrint("114360: hooked table+0xAC4 (was %p)\n", orig_ac4);
        }
        if (!TRACE_NET) {
            net_done = 1;
        }
        if (orig_1834 != NULL && !sqinet_done) {
            start_sqinet();
            sqinet_done = 1;
        }
        if (!net_done && orig_1834 != NULL) {
            int hooked = 0;
            for (j = 0; j < NET_COUNT; j++) {
                volatile PVOID *slot = &table[(NET_FIRST + j * 4) / 4];
                if (*slot != NULL && NET_FIRST + j * 4 != NET_SKIP) {
                    net_orig[j] = (any_fn)*slot;
                    *slot = (PVOID)net_wrappers[j];
                    hooked++;
                }
            }
            net_done = 1;
            DbgPrint("114360: hooked %d network slots +0x%03X..+0x%03X (not +0x%03X)\n", hooked, NET_FIRST,
                     NET_FIRST + NET_COUNT * 4 - 4, NET_SKIP);
        }
        Sleep(1);
    }
    return 0;
}

/* Read the first line of a text file into buf (empty when missing), without the line break. */
static void read_line(const char *path, char *buf, int size)
{
    FILE *f = fopen(path, "r");
    buf[0] = '\0';
    if (f != NULL) {
        if (fgets(buf, size, f) == NULL) {
            buf[0] = '\0';
        }
        fclose(f);
    }
    buf[strcspn(buf, "\r\n")] = '\0';
}

#define REV_ROOT "game:\\rev\\"

/* Fill menu.revisions with the folders under game:\rev. */
static void list_revisions(void)
{
    WIN32_FIND_DATA found;
    HANDLE h = FindFirstFile(REV_ROOT "*", &found);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && found.cFileName[0] != '.'
            && strlen(found.cFileName) < MENU_TEXT && menu.revision_count < MENU_MAX_REVISIONS) {
            strcpy(menu.revisions[menu.revision_count++], found.cFileName);
        }
    } while (FindNextFile(h, &found));
    FindClose(h);
}

/* Square Enix's product code for a version whose first digit names its platform, as on the patch servers: 2 PS2
   ("PS2", FFXI PS2.0001), 3 PC ("W20", W20.0001), 4 Xbox 360 ("XB2", XB2.0001). */
static const char *platform_code(const char *version)
{
    switch (version[0]) {
    case '2': return "PS2";
    case '3': return "W20";
    case '4': return "XB2";
    }
    return "???";
}

/* Install check (the menu's Storage row; install-check.txt and install-check-files.txt next to default.xex):
   whether a revision has every data file the game's own lists name. VTABLE<n>.DAT gives each file ID its ROM
   volume, FTABLE<n>.DAT
   its folder and file number, and the game opens "ROM<n>\<folder>\<file>.DAT". In a pooled revision each such
   path must be in files.tbl (or a plain file in the revision's 0001, which the launcher also opens) and its pool
   file must exist; in a plain revision the file itself. Every problem found is printed as a code with a count:
     E100  no PlayOnline\data: the game stops at the blue orb
     E301  files.tbl is damaged
     E302  not Xbox 360 data (no ROM9; PS2 data looks like this): the game quits or crashes on it
     E303  game files missing from the revision: the data it was installed from was incomplete
     E304  pool files missing: a copy to the console stopped partway
     E305  no VTABLE/FTABLE: not a game data folder
     E306  a ROM folder's file lists are missing (ROM2..ROM9; 360 and PC data have all nine): that folder is missing
     GOOD100  none of the above: every data file the game lists is there
   After the codes, two flags say what was found: the file lists' format, by VTABLE.DAT's size (F360 102961
   entries, FPC 109701, FPS2 90327, F? another), and the data's year from its version (Y2016 for 40160203_0, the
   digits after the platform: version.txt, else 0001\patch.ver).
   The counts of E303/E304 are file IDs (several IDs can name one file). install-check.txt holds each revision's
   result right under its name; install-check-files.txt names every missing file.
   Every revision is checked once per launcher start, on a thread so the menu (and its countdown and sign-in) does
   not wait: the row shows "checking..." until its revision is done. Starting the game stops the check. */
#define INSTALL_LOG "game:\\install-check.txt"          /* each revision's result: what people read and send */
#define INSTALL_FILES "game:\\install-check-files.txt"  /* every missing file, by revision */
#define INSTALL_CACHE MENU_MAX_REVISIONS
static struct {
    char revision[MENU_TEXT];
    char codes[64];
    volatile LONG done;
    volatile LONG progress, total;  /* file IDs checked so far, and to check */
    char shown[32];                 /* "checking 41230/79430", made when the menu asks */
} install_cache[INSTALL_CACHE];
static int install_cached;          /* entries named; set before the thread starts */
static volatile LONG install_stop;  /* the game is starting: stop checking */

static BYTE *read_whole(const char *path, DWORD *size)
{
    FILE *f = fopen(path, "rb");
    BYTE *data = NULL;
    long len;
    *size = 0;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
        && (data = (BYTE *)malloc(len)) != NULL) {
        if (fread(data, 1, len, f) == (size_t)len) {
            *size = (DWORD)len;
        } else {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    return data;
}

static const DATA_ENTRY *table_find(const DATA_ENTRY *entries, DWORD count, const char *path)
{
    char key[DATA_KEY];
    DWORD lo = 0, hi = count;
    memset(key, 0, sizeof(key));
    strncpy(key, path, DATA_KEY - 1);
    while (lo < hi) {
        DWORD mid = (lo + hi) / 2;
        int c = memcmp(entries[mid].key, key, DATA_KEY);
        if (c == 0) {
            return &entries[mid];
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

/* One of the game's file lists ("vtable", "ftable") for ROM volume n, through the table when there is one. */
static BYTE *list_file(const char *revision, const DATA_ENTRY *entries, DWORD count, int n, const char *kind,
                       DWORD *size)
{
    char rel[32], path[MENU_TEXT + 64];
    const DATA_ENTRY *e;
    if (n == 1) {
        _snprintf(rel, sizeof(rel), "%s.dat", kind);
    } else {
        _snprintf(rel, sizeof(rel), "rom%d\\%s%d.dat", n, kind, n);
    }
    rel[sizeof(rel) - 1] = '\0';
    if (entries != NULL && (e = table_find(entries, count, rel)) != NULL) {
        _snprintf(path, sizeof(path), "game:\\data\\%03u\\%03u.dat", e->id / 1000, e->id % 1000);
    } else {
        _snprintf(path, sizeof(path), REV_ROOT "%s\\0001\\%s", revision, rel);
    }
    path[sizeof(path) - 1] = '\0';
    return read_whole(path, size);
}

static void add_code(char *codes, int size, const char *code, unsigned count)
{
    int len = (int)strlen(codes);
    if (count > 1) {
        _snprintf(codes + len, size - len, "%s%s x%u", len ? " " : "", code, count);
    } else {
        _snprintf(codes + len, size - len, "%s%s", len ? " " : "", code);
    }
    codes[size - 1] = '\0';
}

/* Which pool files exist: one bit per pool number, from listing each game:\data\NNN folder once (one directory
   listing per thousand files instead of one lookup per file, which is slow on the console's drive). */
#define POOL_IDS 1000000
static BYTE *pool_have;
static int pool_listed = -1;   /* the last data\NNN folder listed */

static int pool_list(unsigned max_id)
{
    WIN32_FIND_DATA found;
    char pattern[32];
    int d;
    if (pool_have == NULL && (pool_have = (BYTE *)calloc(POOL_IDS / 8, 1)) == NULL) {
        return 0;
    }
    for (d = pool_listed + 1; d <= (int)(max_id / 1000) && d < POOL_IDS / 1000; d++) {
        HANDLE h;
        _snprintf(pattern, sizeof(pattern), "game:\\data\\%03d\\*", d);
        pattern[sizeof(pattern) - 1] = '\0';
        h = FindFirstFile(pattern, &found);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            const char *name = found.cFileName;
            if (strlen(name) == 7 && _stricmp(name + 3, ".dat") == 0 && name[0] >= '0' && name[0] <= '9'
                && name[1] >= '0' && name[1] <= '9' && name[2] >= '0' && name[2] <= '9') {
                unsigned id = d * 1000 + (name[0] - '0') * 100 + (name[1] - '0') * 10 + (name[2] - '0');
                pool_have[id >> 3] |= (BYTE)(1 << (id & 7));
            }
        } while (FindNextFile(h, &found));
        FindClose(h);
    }
    if ((int)(max_id / 1000) > pool_listed) {
        pool_listed = max_id / 1000;
    }
    return 1;
}

/* `progress` gets the number of file IDs checked so far and `total` how many there are; each change bumps
   menu.install_done so the menu shows it. */
static void install_check(const char *revision, char *codes, int size, volatile LONG *progress, volatile LONG *total)
{
    static int log_started;
    char rel[DATA_KEY], path[MENU_TEXT + 64], folder[DATA_KEY];
    BYTE *table = NULL, *vtables[14], *ftable;
    DWORD table_size = 0, count = 0, vsizes[14], fsize, i, start = GetTickCount(), lists_size = 0;
    char version[MENU_TEXT];
    const DATA_ENTRY *entries = NULL, *e;
    unsigned listed = 0, missing = 0, unpooled = 0, expected = 0, max_id = 0, pool_there = 0;
    int n, lists = 0, absent = 0, have[14], pooled = 0, folder_exists = 0;
    FILE *log = fopen(INSTALL_LOG, log_started ? "a" : "w");
    FILE *files = fopen(INSTALL_FILES, log_started ? "a" : "w");
    log_started = 1;
    codes[0] = '\0';
    folder[0] = '\0';
    memset(vtables, 0, sizeof(vtables));
    if (log != NULL) {
        fprintf(log, "rev\\%s\n", revision);
    }
    if (files != NULL) {
        fprintf(files, "rev\\%s\n", revision);
    }
    if (GetFileAttributes("game:\\PlayOnline\\data") == (DWORD)-1) {
        add_code(codes, size, "E100", 1);
        if (log != NULL) {
            fprintf(log, "  E100 no PlayOnline\\data\n");
        }
    }
    _snprintf(path, sizeof(path), REV_ROOT "%s\\files.tbl", revision);
    path[sizeof(path) - 1] = '\0';
    if (GetFileAttributes(path) != (DWORD)-1) {
        table = read_whole(path, &table_size);
        if (table == NULL || table_size < 16 || memcmp(table, "114360P1", 8) != 0 || *(DWORD *)(table + 12) != DATA_KEY
            || (count = *(DWORD *)(table + 8)) > (table_size - 16) / sizeof(DATA_ENTRY)) {
            add_code(codes, size, "E301", 1);
            if (log != NULL) {
                fprintf(log, "  E301 files.tbl is damaged\n");
            }
            count = 0;
        } else {
            entries = (const DATA_ENTRY *)(table + 16);
            for (i = 0; i < count; i++) {
                if (entries[i].id > max_id) {
                    max_id = entries[i].id;
                }
            }
            pooled = max_id < POOL_IDS && pool_list(max_id);
            for (i = 0; pooled && i < count; i++) {
                pool_there += (pool_have[entries[i].id >> 3] >> (entries[i].id & 7)) & 1;
            }
        }
    }
    /* The volumes' VTABLEs first: how many file IDs there are to check, for the count the menu shows. */
    memset(have, 0, sizeof(have));
    for (n = 1; n <= 13 && (table == NULL || entries != NULL) && !install_stop; n++) {
        vtables[n] = list_file(revision, entries, count, n, "vtable", &vsizes[n]);
        for (i = 0; vtables[n] != NULL && i < vsizes[n]; i++) {
            expected += vtables[n][i] == n;
        }
    }
    InterlockedExchange(total, expected);
    InterlockedIncrement(&menu.install_done);
    for (n = 1; n <= 13 && (table == NULL || entries != NULL) && !install_stop; n++) {
        ftable = vtables[n] != NULL ? list_file(revision, entries, count, n, "ftable", &fsize) : NULL;
        if (ftable != NULL) {
            lists++;
            have[n] = 1;
            if (n == 1) {
                lists_size = vsizes[n];
            }
            for (i = 0; i < vsizes[n] && 2 * i + 2 <= fsize && !install_stop; i++) {
                unsigned value;
                if (vtables[n][i] != n) {
                    continue;
                }
                value = ftable[2 * i] | ftable[2 * i + 1] << 8;
                if (n == 1) {
                    _snprintf(rel, sizeof(rel), "rom\\%u\\%u.dat", value >> 7, value & 0x7F);
                } else {
                    _snprintf(rel, sizeof(rel), "rom%d\\%u\\%u.dat", n, value >> 7, value & 0x7F);
                }
                rel[sizeof(rel) - 1] = '\0';
                /* The count for the menu; the menu itself is told at most 4 times a second, since each time it
                   re-reads the preset's program files from the same drive this check is reading. */
                if ((++listed & 255) == 0) {
                    static DWORD told;
                    InterlockedExchange(progress, listed);
                    if (GetTickCount() - told >= 250) {
                        told = GetTickCount();
                        InterlockedIncrement(&menu.install_done);
                    }
                }
                if (entries != NULL && (e = table_find(entries, count, rel)) != NULL) {
                    int there;
                    if (pooled) {
                        there = (pool_have[e->id >> 3] >> (e->id & 7)) & 1;
                    } else {
                        _snprintf(path, sizeof(path), "game:\\data\\%03u\\%03u.dat", e->id / 1000, e->id % 1000);
                        path[sizeof(path) - 1] = '\0';
                        there = GetFileAttributes(path) != (DWORD)-1;
                    }
                    if (!there) {
                        unpooled++;
                        if (files != NULL) {
                            fprintf(files, "  E304 %s (data\\%03u\\%03u.dat)\n", rel, e->id / 1000, e->id % 1000);
                        }
                    }
                    continue;
                }
                /* A plain file in the revision's 0001; a missing folder means every file in it is missing. */
                if (strncmp(rel, folder, strlen(folder)) != 0 || folder[0] == '\0') {
                    strcpy(folder, rel);
                    *strrchr(folder, '\\') = '\0';
                    _snprintf(path, sizeof(path), REV_ROOT "%s\\0001\\%s", revision, folder);
                    path[sizeof(path) - 1] = '\0';
                    folder_exists = GetFileAttributes(path) != (DWORD)-1;
                    strcat(folder, "\\");
                }
                if (folder_exists) {
                    _snprintf(path, sizeof(path), REV_ROOT "%s\\0001\\%s", revision, rel);
                    path[sizeof(path) - 1] = '\0';
                }
                if (!folder_exists || GetFileAttributes(path) == (DWORD)-1) {
                    missing++;
                    if (files != NULL) {
                        fprintf(files, "  E303 %s\n", rel);
                    }
                }
            }
        }
        free(ftable);
    }
    for (n = 1; n <= 13; n++) {
        free(vtables[n]);
    }
    /* The game started before the check was done: its files are being redirected for the game by now, so what
       is left of the check would be wrong. Record that it stopped, nothing else. */
    if (install_stop) {
        strcpy(codes, "check stopped");
        if (log != NULL) {
            fprintf(log, "  check stopped: the game was started first\n\n");
            fclose(log);
        }
        if (files != NULL) {
            fprintf(files, "  (check stopped)\n\n");
            fclose(files);
        }
        DbgPrint("114360: install check rev\\%s: stopped, the game was started first\n", revision);
        free(table);
        return;
    }
    for (n = 2; n <= 9; n++) {
        absent += !have[n];
    }
    if (lists == 0 && (table == NULL || entries != NULL)) {
        add_code(codes, size, "E305", 1);
    } else if (lists > 0 && absent == 1 && !have[9]) {
        add_code(codes, size, "E302", 1);
        if (log != NULL) {
            fprintf(log, "  E302 no ROM9: not Xbox 360 data (PS2 data stops at ROM8)\n");
        }
    } else if (lists > 0 && absent > 0) {
        add_code(codes, size, "E306", absent);
        for (n = 2; n <= 9; n++) {
            if (!have[n] && log != NULL) {
                fprintf(log, "  E306 ROM%d\\VTABLE%d.DAT / FTABLE%d.DAT missing: the ROM%d folder is missing\n", n, n, n, n);
            }
        }
    }
    if (missing > 0) {
        add_code(codes, size, "E303", missing);
    }
    if (unpooled > 0) {
        add_code(codes, size, "E304", unpooled);
    }
    if (codes[0] == '\0') {
        strcpy(codes, "GOOD100");
    }
    /* What was found: the file lists' format, the data's year. */
    if (lists > 0) {
        add_code(codes, size, lists_size == 102961 ? "F360" : lists_size == 109701 ? "FPC" : lists_size == 90327 ? "FPS2" : "F?", 1);
    }
    _snprintf(path, sizeof(path), REV_ROOT "%s\\version.txt", revision);
    path[sizeof(path) - 1] = '\0';
    read_line(path, version, sizeof(version));
    if (version[0] == '\0') {
        _snprintf(path, sizeof(path), REV_ROOT "%s\\0001\\patch.ver", revision);
        path[sizeof(path) - 1] = '\0';
        read_line(path, version, sizeof(version));
    }
    if (strlen(version) >= 4 && version[2] >= '0' && version[2] <= '9' && version[3] >= '0' && version[3] <= '9') {
        char year[8];
        _snprintf(year, sizeof(year), "Y20%c%c", version[2], version[3]);
        year[sizeof(year) - 1] = '\0';
        add_code(codes, size, year, 1);
    }
    /* The pool this revision needs: files its table names, and how many of them are in game:\data. */
    if (pooled) {
        char pool[32];
        _snprintf(pool, sizeof(pool), "pool %u/%u", pool_there, count);
        pool[sizeof(pool) - 1] = '\0';
        add_code(codes, size, pool, 1);
    }
    if (log != NULL) {
        fprintf(log, "  %s\n  %u file IDs checked, %u missing, %u pool files missing (%u ms)%s\n\n", codes, listed,
                missing, unpooled, GetTickCount() - start,
                missing || unpooled ? "; each missing file is in install-check-files.txt" : "");
        fclose(log);
    }
    if (files != NULL) {
        fprintf(files, "\n");
        fclose(files);
    }
    DbgPrint("114360: install check rev\\%s: %s (%u file IDs, %u missing, %u pool files missing, %u ms)\n", revision,
             codes, listed, missing, unpooled, GetTickCount() - start);
    free(table);
}

static DWORD WINAPI install_thread(LPVOID unused)
{
    int i, pass;
    (void)unused;
    /* The selected preset's revision first, so its row shows soonest; then the others. */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < install_cached && !install_stop; i++) {
            int selected = strcmp(install_cache[i].revision, menu.presets[menu.current].revision) == 0;
            if (install_cache[i].done || selected != (pass == 0)) {
                continue;
            }
            install_check(install_cache[i].revision, install_cache[i].codes, sizeof(install_cache[i].codes),
                          &install_cache[i].progress, &install_cache[i].total);
            InterlockedExchange(&install_cache[i].done, 1);
            InterlockedIncrement(&menu.install_done);
        }
    }
    return 0;
}

/* A revision's install check result: its codes, or "checking 41230/79430" (file IDs) while the thread is on it. */
static const char *install_codes(const char *revision)
{
    int i;
    if (install_cached == 0) {
        if (install_stop || menu.revision_count == 0) {
            return "not checked";
        }
        for (i = 0; i < menu.revision_count && i < INSTALL_CACHE; i++) {
            strcpy(install_cache[i].revision, menu.revisions[i]);
        }
        install_cached = i;
        CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
    }
    for (i = 0; i < install_cached; i++) {
        if (strcmp(install_cache[i].revision, revision) == 0) {
            if (install_cache[i].done) {
                return install_cache[i].codes;
            }
            if (install_cache[i].total > 0) {
                _snprintf(install_cache[i].shown, sizeof(install_cache[i].shown), "checking %ld/%ld",
                          install_cache[i].progress, install_cache[i].total);
            } else {
                strcpy(install_cache[i].shown, "checking...");
            }
            install_cache[i].shown[sizeof(install_cache[i].shown) - 1] = '\0';
            return install_cache[i].shown;
        }
    }
    return "E305";   /* the preset names a folder that is not in rev\ */
}

/* /errors on the chat page: every revision's install check (with what its codes mean and the first files
   install-check-files.txt names), then what went wrong in this run and the one before (launcher.log, launcher.1.log):
   crashes, data files the game could not find, refused sign-ins, the game quitting on its own. */
static const char *const install_code_text[][2] = {
    {"E100", "no PlayOnline\\data: the game stops at the blue orb; install again"},
    {"E301", "files.tbl is damaged; install again"},
    {"E302", "not Xbox 360 data (no ROM9; PS2 data): the 360 client cannot run it"},
    {"E303", "game files missing: the data it was installed from was incomplete (install again from complete data)"},
    {"E304", "pool files missing: a copy stopped partway (install again to the same place)"},
    {"E305", "no game file lists (VTABLE/FTABLE): not a game data folder"},
    {"E306", "ROM folders missing; install again from complete data"},
    {"GOOD100", "all good: every data file the game lists is there"},
    {"FPS2", "file lists in the PS2 format"},
    {"F?", "file lists of an unknown size"},
};

static void errors_in_log(const char *path, const char *label)
{
    char line[256], found[3][160];
    unsigned crashes = 0, not_found = 0, signin = 0, quit = 0, started = 0, n = 0, i;
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *text = strncmp(line, "114360: ", 8) == 0 ? line + 8 : line;
        text[strcspn(text, "\r\n")] = '\0';
        if (strncmp(text, "starting ", 9) == 0) {
            started = 1;
        } else if (strncmp(text, "CRASH: exception", 16) == 0) {
            if (crashes++ == 0 && n < 3) {
                _snprintf(found[n++], 160, "crashed: %s", text + 7);
            }
        } else if (strstr(text, " failed C000003A") != NULL && strstr(text, ":\\ROM") != NULL
                   && strstr(text, "TABLE") == NULL) {
            if (not_found++ == 0 && n < 3) {
                _snprintf(found[n++], 160, "data file not found: %.*s", (int)(strstr(text, " failed") - text), text);
            }
        } else if (strstr(text, "sign-in as") != NULL && strstr(text, "failed") != NULL) {
            if (signin++ == 0 && n < 3) {
                _snprintf(found[n++], 160, "%s", text);
            }
        } else if (strncmp(text, "game exit via", 13) == 0) {
            quit = 1;
        }
    }
    fclose(f);
    for (i = 0; i < n; i++) {
        found[i][159] = '\0';
    }
    if (n == 0 && !(quit && started && crashes == 0)) {
        pol_log(&pol_session, "%s: no errors%s", label, started ? "" : " (the game was not started)");
        return;
    }
    pol_log(&pol_session, "%s:%s", label, crashes ? "" : quit ? " the game quit by itself" : "");
    for (i = 0; i < n; i++) {
        pol_log(&pol_session, "  %s", found[i]);
    }
    if (not_found > 1) {
        pol_log(&pol_session, "  (%u data files not found in all)", not_found);
    }
}

static void menu_errors(void)
{
    char line[256], section[MENU_TEXT + 8];
    int i, k, shown;
    FILE *f;
    pol_log(&pol_session, "install check (details: install-check.txt next to default.xex):");
    if (install_cached == 0) {
        pol_log(&pol_session, "  not run yet");
    }
    for (i = 0; i < install_cached; i++) {
        const char *codes = install_cache[i].done ? install_cache[i].codes : "checking...";
        pol_log(&pol_session, "  rev\\%s: %s", install_cache[i].revision, codes);
        if (!install_cache[i].done || strncmp(codes, "GOOD100", 7) == 0) {
            continue;
        }
        for (k = 0; k < (int)(sizeof(install_code_text) / sizeof(install_code_text[0])); k++) {
            if (strstr(codes, install_code_text[k][0]) != NULL) {
                pol_log(&pol_session, "    %s %s", install_code_text[k][0], install_code_text[k][1]);
            }
        }
        /* The first files the check names for this revision. */
        if ((f = fopen(INSTALL_FILES, "r")) != NULL) {
            _snprintf(section, sizeof(section), "rev\\%s", install_cache[i].revision);
            section[sizeof(section) - 1] = '\0';
            shown = -1;
            while (fgets(line, sizeof(line), f) != NULL) {
                line[strcspn(line, "\r\n")] = '\0';
                if (shown < 0) {
                    shown = strcmp(line, section) == 0 ? 0 : -1;
                } else if (line[0] != ' ') {
                    break;
                } else if (strncmp(line, "  E", 3) == 0 && line[6] == ' ' && shown < 3) {
                    pol_log(&pol_session, "    %s", line + 2);
                    shown++;
                }
            }
            fclose(f);
        }
    }
    errors_in_log("game:\\launcher.log", "this run (launcher.log)");
    errors_in_log("game:\\launcher.1.log", "the run before (launcher.1.log)");
}

/* /patches [id] [on|off] (menu callback): the client patches (patches.h), each with its state for the selected preset
   and whether it applies to that preset's client (its file in the revision's 0001 folder has the size the patch
   was made for). With an id: that patch's details; with on/off: set it for the preset (saved with the menu). */
static void menu_patches(char *arg)
{
    MENU_PRESET *preset = menu.preset_count > 0 ? &menu.presets[menu.current] : NULL;
    char id[32], state[8], path[MAX_PATH];
    const CLIENT_PATCH *found = NULL;
    int i, k;
    id[0] = state[0] = '\0';
    sscanf(arg, "%31s %7s", id, state);
    if (preset == NULL) {
        pol_log(&pol_session, "patches: no preset selected");
        return;
    }
    if (client_patch_count == 0) {
        pol_log(&pol_session, "patches: none yet");
        return;
    }
    if (!id[0]) {
        pol_log(&pol_session, "client patches for [%s] (rev\\%s):", preset->name, preset->revision);
    }
    for (i = 0; i < client_patch_count; i++) {
        const CLIENT_PATCH *p = &client_patches[i];
        long size = -1;
        FILE *f;
        if (id[0] && _stricmp(id, p->id) != 0) {
            continue;
        }
        found = p;
        _snprintf(path, sizeof(path), REV_ROOT "%s\\0001\\%s", preset->revision, p->file);
        path[sizeof(path) - 1] = '\0';
        if ((f = fopen(path, "rb")) != NULL) {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fclose(f);
        }
        pol_log(&pol_session, "  %-10s %-3s %s (%s: %s)", p->id, patch_enabled(p, preset->patches) ? "on" : "off",
                p->title, p->file, size < 0 ? "not in this revision"
                : (unsigned long)size == p->file_size ? "applies" : "another build, not applied");
        for (k = 0; id[0] && k < 4 && p->detail[k] != NULL; k++) {
            pol_log(&pol_session, "    %s", p->detail[k]);
        }
    }
    if (id[0] && found == NULL) {
        pol_log(&pol_session, "patches: no patch named %s (/patches lists them)", id);
    } else if (found != NULL && (_stricmp(state, "on") == 0 || _stricmp(state, "off") == 0)) {
        patch_list_set(preset->patches, sizeof(preset->patches), found, _stricmp(state, "on") == 0);
        menu.changed = 1;
        pol_log(&pol_session, "patches: %s %s for [%s]; takes effect at the next game start", found->id, state,
                preset->name);
    }
}

/* What a preset's revision holds (menu callback). PolCore must be a build the launcher knows (engine.h): the
   start-up depends on it. FFXi.dll and FFXiMain may be any build; one without a profile runs as is, without the
   launcher's fixes (a first run of another era's client, whose Xenia log then gives its import tables).
   The DAT set's version is rev\<revision>\version.txt when the revision was built from another era, else
   0001\patch.ver; the revision is POOLED when it has a files.tbl. See MENU_CHECK for the texts. */
static void check_preset(const MENU_PRESET *preset, MENU_CHECK *out)
{
    static const char *const files[] = {"PolCore.xex", "0001\\FFXi.dll", "0001\\FFXiMain.dll"};
    const POLCORE_PROFILE *polcore = NULL;
    const FFXIMAIN_PROFILE *ffximain = NULL;
    DWORD main_stamp = 0;
    char path[MENU_TEXT + 32], data[MENU_TEXT];
    int i;
    if (preset->revision[0] == '\0') {
        _snprintf(out->problem, sizeof(out->problem), "Server [%s] has no revision", preset->name);
        out->problem[sizeof(out->problem) - 1] = '\0';
        return;
    }
    _snprintf(path, sizeof(path), REV_ROOT "%s\\version.txt", preset->revision);
    path[sizeof(path) - 1] = '\0';
    read_line(path, data, sizeof(data));
    if (data[0] == '\0') {
        _snprintf(path, sizeof(path), REV_ROOT "%s\\0001\\patch.ver", preset->revision);
        path[sizeof(path) - 1] = '\0';
        read_line(path, data, sizeof(data));
    }
    data[strcspn(data, " ")] = '\0';
    strcpy(out->data_version, data);
    _snprintf(out->data, sizeof(out->data), "%s %s", platform_code(data), data[0] ? data : "-");
    out->data[sizeof(out->data) - 1] = '\0';
    _snprintf(path, sizeof(path), REV_ROOT "%s\\files.tbl", preset->revision);
    path[sizeof(path) - 1] = '\0';
    _snprintf(out->storage, sizeof(out->storage), "%s  %s", GetFileAttributes(path) != (DWORD)-1 ? "POOLED" : "DIRECT",
              install_codes(preset->revision));
    out->storage[sizeof(out->storage) - 1] = '\0';
    for (i = 0; i < 3; i++) {
        DWORD stamp;
        _snprintf(path, sizeof(path), REV_ROOT "%s\\%s", preset->revision, files[i]);
        path[sizeof(path) - 1] = '\0';
        stamp = engine_file_timestamp(path);
        if (stamp == 0) {
            _snprintf(out->problem, sizeof(out->problem), "rev\\%s has no %s", preset->revision, files[i]);
        } else if (i == 0 && (engine_module("PolCore.xex", stamp) == NULL || (polcore = engine_polcore(stamp)) == NULL)) {
            _snprintf(out->problem, sizeof(out->problem), "No fixes for this PolCore (build %08X)", stamp);
        } else {
            if (i == 2) {
                main_stamp = stamp;
                ffximain = engine_ffximain(stamp);
            }
            continue;
        }
        out->problem[sizeof(out->problem) - 1] = '\0';
        return;
    }
    /* Only 360 builds of PolCore and FFXiMain run here: PlayOnline's 360 product is X2U-1000. */
    _snprintf(out->pol, sizeof(out->pol), "X2U %s", polcore->label);
    out->pol[sizeof(out->pol) - 1] = '\0';
    if (ffximain != NULL) {
        _snprintf(out->client, sizeof(out->client), "%s %s %s", platform_code(ffximain->label), ffximain->label,
                  strcmp(platform_code(ffximain->label), platform_code(data)) == 0 ? "NATIVE" : "EMULATION");
    } else {
        _snprintf(out->client, sizeof(out->client), "XB2 build %08X (no fixes)", main_stamp);
    }
    out->client[sizeof(out->client) - 1] = '\0';
}

/* The version the lobby login reports (send_fixed): the preset's `version`, else rev\<revision>\version.txt,
   else the data's version (MENU_CHECK.data_version); 3xxx (PC) and 4xxx (360) are
   reported as 2xxx (PS2), or as 3xxx when the preset names the PC platform. */
static void choose_report_version(const MENU_PRESET *preset, const char *patch_ver)
{
    char path[MENU_TEXT + 32], v[MENU_TEXT];
    strcpy(v, preset->version);
    if (v[0] == '\0') {
        _snprintf(path, sizeof(path), REV_ROOT "%s\\version.txt", preset->revision);
        path[sizeof(path) - 1] = '\0';
        read_line(path, v, sizeof(v));
    }
    if (v[0] == '\0') {
        strcpy(v, patch_ver);
        v[strcspn(v, " ")] = '\0';
    }
    if (v[0] == '2' || v[0] == '3' || v[0] == '4') {
        v[0] = _stricmp(preset->platform, "WIN") == 0 ? '3' : '2';
    }
    copy_trimmed(report_version, LOBBY_VERSION_SIZE, v);
    DbgPrint("114360: lobby login reports version [%s]\n", report_version);
}

/* How this launch handles PlayOnline, for the menu's Mode line (at most 50 characters fit). */
static void product_mode(DWORD product, char *mode, int size)
{
    if (product == 0) {
        _snprintf(mode, size, "relaunched by the PlayOnline Viewer");
    } else if (viewer_mode) {
        _snprintf(mode, size, "PlayOnline Viewer (product %u)", product);
    } else {
        _snprintf(mode, size, "direct start: PlayOnline answered locally");
    }
    mode[size - 1] = '\0';
}

void __cdecl main(void)
{
    ANSI_STRING_X link, device;
    POL_LAUNCH_DATA launch;
    HANDLE polcore = NULL;
    PVOID entry = NULL;
    LONG status;
    int choice;
    char mode[96], pol_folder[sizeof(game_device) + 16];

    DbgPrint("114360: launcher started\n");
    SetUnhandledExceptionFilter(crash_filter);
    detect_xenia();
    find_game_device();
    load_config();

    /* PlayOnline system and user data. The Viewer normally mounts these before starting a game; without them
       FFXiMain falls back to the placeholder drive names "NOSYS:" / "NOUSR:" (seen in the Xenia log) and stops
       with POL-2071 after the terms screen. Point both at game:\PlayOnline (the disc's PlayOnline folder plus
       empty pub\home00 and usr\home00). */
    ansi(&link, "\\??\\NOSYS:");
    _snprintf(pol_folder, sizeof(pol_folder), "%s\\PlayOnline", game_device);
    pol_folder[sizeof(pol_folder) - 1] = '\0';
    ansi(&device, pol_folder);
    status = ObCreateSymbolicLink(&link, &device);
    DbgPrint("114360: NOSYS: -> %s status=%08X\n", device.Buffer, status);
    ansi(&link, "\\??\\NOUSR:");
    status = ObCreateSymbolicLink(&link, &device);
    DbgPrint("114360: NOUSR: -> %s status=%08X\n", device.Buffer, status);

    {
        /* A relaunch from the Viewer (Play on a title) arrives with the Viewer's own launch data, which PolCore
           reads to start that title with the signed-in PlayOnline session. Keep it untouched; only a cold
           start gets launch data from game:\product.txt. */
        BYTE existing[0x400];
        DWORD size = 0;
        if (XGetLaunchDataSize(&size) == ERROR_SUCCESS && size != 0 && size <= sizeof(existing)
            && XGetLaunchData(existing, size) == ERROR_SUCCESS
            && strncmp((const char *)existing, RETRY_LAUNCH_NAME, sizeof(RETRY_LAUNCH_NAME)) != 0) {
            DWORD k;
            DbgPrint("114360: relaunched with %u bytes of launch data; leaving it for PolCore\n", size);
            for (k = 0; k < size; k += 16) {
                DbgPrint("114360:   +%03X %08X %08X %08X %08X\n", k, *(DWORD *)(existing + k),
                         *(DWORD *)(existing + k + 4), *(DWORD *)(existing + k + 8), *(DWORD *)(existing + k + 12));
            }
            launch.product = 0;
        } else {
            /* The game ended and trace_f24 restarted the launcher: come back to a waiting menu, not a countdown
               that would start the game again on its own. */
            menu.returned = size >= sizeof(RETRY_LAUNCH_NAME) && size <= sizeof(existing)
                            && XGetLaunchData(existing, size) == ERROR_SUCCESS
                            && strncmp((const char *)existing, RETRY_LAUNCH_NAME, sizeof(RETRY_LAUNCH_NAME)) == 0;
            memset(&launch, 0, sizeof(launch));
            strcpy(launch.name, "FFXI");
            launch.one = 1;
            launch.product = FFXI_PRODUCT_ID;
            {
                /* Development switch: game:\product.txt holding "1000" starts the PlayOnline Viewer (product 1000)
                   through PolCore instead of FFXI, for logging in through a PlayOnline server such as OpenLobby. */
                FILE *f = fopen("game:\\product.txt", "r");
                if (f != NULL) {
                    unsigned long p = 0;
                    if (fscanf(f, "%lu", &p) == 1 && p != 0) {
                        launch.product = p;
                        strcpy(launch.name, p == 1000 ? "PlayOnlineViewer" : "FFXI");
                        viewer_mode = p == 1000;
                    }
                    fclose(f);
                }
            }
            status = (LONG)XSetLaunchData(&launch, sizeof(launch));
            direct_mode = !viewer_mode;
            DbgPrint("114360: launch data product=%u status=%08X%s\n", launch.product, status,
                     direct_mode ? " (direct start: PlayOnline channel answered locally)" : "");
        }
    }
    product_mode(launch.product, mode, sizeof(mode));
    if (launch.product != 0) {
        /* A cold start (or the restart after the game ended): let the player pick the server. A relaunch from
           the Viewer plays what launcher.ini names. */
        list_revisions();
        if (menu.preset_count == 0) {
            MENU_PRESET *p = &menu.presets[menu.preset_count++];
            strcpy(p->name, "local");
            strcpy(p->address, "127.0.0.1");
            strcpy(p->revision, menu.revision_count > 0 ? menu.revisions[0] : "");
            menu.changed = 1;
        }
        menu.check = check_preset;
        menu.pol = &pol_session;
        menu.signin = menu_signin;
        menu.register_id = menu_register;
        menu.register_code = menu_register_code;
        menu.errors = menu_errors;
        menu.patches = menu_patches;
        menu.mode = mode;
        choice = menu_run(&menu);
        if (menu.changed) {
            save_config();
        }
        if (choice == MENU_EXIT) {
            DbgPrint("114360: menu: Exit\n");
            return;
        }
    }
    install_stop = 1;
    apply_config();
    {
        MENU_CHECK check;
        memset(&check, 0, sizeof(check));
        if (menu.preset_count == 0) {
            strcpy(check.problem, "launcher.ini has no server preset");
        } else {
            check_preset(&menu.presets[menu.current], &check);
        }
        DbgPrint("114360: starting %s: data %s, client %s, PlayOnline %s, %s%s%s\n", config_server, check.data,
                 check.client, check.pol, check.storage, check.problem[0] ? "; stopped: " : "", check.problem);
        if (check.problem[0] != '\0' || !select_revision()) {
            return;
        }
        choose_report_version(&menu.presets[menu.current], check.data_version);
        title_stack(&menu.presets[menu.current], &check);
    }

    status = XexLoadImage(REVISION_DRIVE "\\PolCore.xex", XEX_LOADIMG_FLAG_TITLE_DLL, 0, &polcore);
    DbgPrint("114360: XexLoadImage(%s\\PolCore.xex) status=%08X\n", REVISION_DRIVE, status);
    if (status < 0 || !resolve_exports() || !patch_module(REVISION_DRIVE "\\PolCore.xex", polcore)) {
        return;
    }
    polcore_profile = engine_polcore(engine_loaded_timestamp(polcore));
    if (polcore_profile == NULL) {
        DbgPrint("114360: no PolCore profile for this build\n");
        return;
    }
    status = XexGetProcedureAddress(polcore, POLCORE_ORDINAL_MAIN, &entry);
    DbgPrint("114360: PolCore ordinal %d status=%08X address=%p\n", POLCORE_ORDINAL_MAIN, status, entry);
    if (status < 0 || entry == NULL) {
        return;
    }
    CreateThread(NULL, 0, trace_table, NULL, 0, NULL);
    status = ((polcore_main_fn)entry)(0, NULL, NULL);
    DbgPrint("114360: PolCore main returned %d\n", status);
}
