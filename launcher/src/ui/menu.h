/* Launcher menu: a full-screen text menu (Direct3D, X11 bitmap fonts, microui layout) where the player picks
   and edits a server preset before the game starts. */
#ifndef MENU_H
#define MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_MAX_PRESETS 16
#define MENU_MAX_REVISIONS 16
#define MENU_TEXT 64

#include "polirc.h"

typedef struct {
    char name[32];
    char address[MENU_TEXT];    /* IPv4 address or host name */
    char revision[MENU_TEXT];   /* folder under game:\rev */
    char version[MENU_TEXT];    /* client version the lobby login reports; "" = the revision's, PS2 form */
    char credential[MENU_TEXT]; /* PlayOnline sign-in file under game:\ (tools/pol-signin); "" = none */
    char pol_id[16];            /* PlayOnline ID to sign in with on the console; "" = no sign-in (bridge) */
    char pol_password[32];
    char platform[8];           /* platform the world login (0x00A) names: "PS2", "WIN"; "" = the game's own */
    char pol_version[16];       /* PlayOnline version to sign in as (polirc.h pol_versions); "" = the game's own */
    char patches[MENU_TEXT];    /* client patches on/off (patches.h): "water", "-water"; "" = each patch's default */
} MENU_PRESET;

/* What the launcher knows about one preset's revision, each with Square Enix's platform code: the DAT set
   ("PS2 20100904_2": the data's own version and platform), the client ("XB2 40160203_0 NATIVE", or EMULATION when it
   runs another platform's data), PlayOnline ("X2U 20110726_A"), the storage ("POOLED": the shared pool through
   rev\<revision>\files.tbl, "DIRECT": a plain folder), the bare data version for the lobby report, and why it cannot
   start (empty when it can). */
typedef struct {
    char pol[48];
    char client[96];
    char data[MENU_TEXT];
    char data_version[MENU_TEXT];
    char storage[80];           /* "POOLED" or "DIRECT", then the install check: "GOOD100 F360 Y2016" or its codes */
    char problem[160];
} MENU_CHECK;

typedef struct {
    MENU_PRESET presets[MENU_MAX_PRESETS];
    int preset_count;
    int current;                        /* the preset to play on */
    char language[16];                  /* japanese, english, 0-3; "" = PlayOnline's choice */
    char font[16];                      /* font name from font.h; "" = the first */
    int autostart;                      /* seconds before starting on its own; 0 = wait for the player */
    char packet_dump[MENU_TEXT];        /* "IPv4:port" to export server packets to; "" = off */
    char revisions[MENU_MAX_REVISIONS][MENU_TEXT];   /* folders found under game:\rev */
    int revision_count;
    const char *mode;                   /* how PlayOnline is handled, shown at the bottom */
    volatile LONG install_done;         /* revisions whose install check finished (launcher.c); the menu re-reads */
    void (*check)(const MENU_PRESET *preset, MENU_CHECK *out);
    void (*errors)(void);               /* /errors: the install check and the last runs' failures, into the chat log */
    void (*patches)(char *arg);         /* /patches [id] [on|off]: client patches (patches.h), into the chat log */
    int changed;                        /* set by the menu when anything worth saving was edited */
    int returned;                       /* back from the game: no countdown, the menu waits */
    char channel[32];                   /* PlayOnline chat channel ("#name") */
    POL_SESSION *pol;                   /* the PlayOnline session (launcher.c) */
    /* Sign in with the preset's PlayOnline ID on its server; 1 = signed in. The menu calls it before starting
       a preset that has a PlayOnline ID, and from the chat page. */
    int (*signin)(const MENU_PRESET *preset);
    /* Make a new PlayOnline ID on the preset's server (its sign-up pages): 1 = made, its ID in `pol_id`;
       2 = the server mailed a verification code, finished by register_code; 0 = refused (`error`). */
    int (*register_id)(const MENU_PRESET *preset, const char *handle, const char *password, const char *code,
                       char *pol_id, char *error, int error_size);
    int (*register_code)(const char *code, char *pol_id, char *error, int error_size);
} MENU;

#define MENU_EXIT 0
#define MENU_START 1

/* Run the menu until the player starts the game (or the countdown runs out) or chooses Exit. Releases the
   Direct3D device before returning, so the game can create its own; the last frame stays on screen until
   the game presents. Returns MENU_START at once (nothing shown) when Direct3D cannot be set up. */
int menu_run(MENU *menu);

#ifdef __cplusplus
}
#endif

#endif
