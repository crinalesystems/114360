#ifndef POLIRC_H
#define POLIRC_H

/* A PlayOnline session: sign-in on the auth server (IRC with PlayOnline crypto, TCP 51240), the content
   credential from the profile server (TCP 51220), then chat over the same IRC connection. The protocol is
   Project Crystal's (POLAuth, POLProfile; their reference client is POLCommon/PolClient). Portable: sockets come
   from a POL_NET the caller supplies (the launcher's XNet one, or POSIX in tools/polirc-test.c). */

#include "polcrypt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Connect to IPv4 `ip` (host order) and `port`; returns a handle > 0, or <= 0. */
    long (*connect)(pol_u32 ip, unsigned short port);
    int (*send)(long sock, const void *buf, int len);
    /* Wait up to `timeout_ms` for data: > 0 bytes read, 0 timeout, < 0 closed or failed. */
    int (*recv)(long sock, void *buf, int len, int timeout_ms);
    void (*close)(long sock);
} POL_NET;

#define POL_LOG_LINES 48
#define POL_LOG_TEXT 160

enum { POL_OFFLINE, POL_SIGNING_IN, POL_ONLINE, POL_FAILED };

#define POL_MAX_FRIENDS 32
typedef struct {
    char pol_id[9];
    int online;                     /* 1 online, 0 offline, -1 not heard yet */
} POL_FRIEND_STATE;

typedef struct {
    const POL_NET *net;
    long sock;
    pol_u32 server;                 /* auth/profile server, host order */
    int state;
    char error[96];
    char pol_id[9];
    char nick[10];                  /* scrambled ID */
    char init_base32[128];          /* the server's InitData, as sent */
    pol_u32 login_time;             /* from InitData */
    pol_u32 client_ip;              /* our address as the server sees it, host order */
    unsigned short client_port;
    POL_CIPHER cipher;
    int crypto_ready;
    /* content credential for the game (0x26 +0x34 and the lobby MD5 secret) */
    int have_credential;
    unsigned char random[16];
    unsigned char auth[0x40];
    /* receive buffer and chat log (ring) */
    char in[2048];
    int in_len;
    char log[POL_LOG_LINES][POL_LOG_TEXT];
    int log_count;                  /* lines written in total; the newest is log[(log_count - 1) % POL_LOG_LINES] */
    /* friends: status from the server's notices, friend requests received, our message sequence */
    POL_FRIEND_STATE friend_state[POL_MAX_FRIENDS];
    int friend_state_count;
    POL_FRIEND_STATE requests[POL_MAX_FRIENDS];
    int request_count;
    pol_u32 message_seq;
    char handle_name[17];           /* our handle, loaded or made on first friend use */
    pol_u64 message_key;            /* the server's message key, from its first notice */
    int message_key_known;
} POL_SESSION;

/* A PlayOnline version the launcher can sign in as. Servers follow the PlayOnline they were written against:
   Project Crystal the PS2-era one, OpenLobby 20110726_A, the last 360 PlayOnline before the 2015 notice.
   They differ in how much of the server's InitData base32 salts the password digest (`salt_chars`, 0 = all);
   checked against both servers, 2026-11-03. */
typedef struct {
    const char *version;            /* X2U version, "20110726_A" */
    const char *servers;            /* servers known to want it, for the menu */
    int salt_chars;
} POL_VERSION;

#define POL_VERSION_COUNT 2
extern const POL_VERSION pol_versions[POL_VERSION_COUNT];

/* The entry for `version`, or NULL. "" and unknown versions are NULL; callers fall back to POL_VERSION_DEFAULT. */
const POL_VERSION *pol_version_find(const char *version);

/* The PlayOnline the game's own PolCore is (the only PolCore build the launcher runs, engine.c). */
#define POL_VERSION_DEFAULT "20110726_A"

/* Sign in as `pol_id`/`password` on `server` the way PlayOnline `version` does, and fetch the content
   credential. Blocking (a few seconds); returns 1 on success, else 0 with s->error set. */
int pol_signin(POL_SESSION *s, const POL_NET *net, pol_u32 server, const POL_VERSION *version, const char *pol_id,
               const char *password);

/* Read what has arrived (waiting at most `timeout_ms`), answer PING, log chat lines. Returns 0 when the
   connection is gone. */
int pol_poll(POL_SESSION *s, int timeout_ms);

/* Chat: `target` is a channel ("#name") or a PlayOnline ID; text is sent as PRIVMSG. JOIN/PART for channels. */
int pol_say(POL_SESSION *s, const char *target, const char *text);

/* Friend list (profile server requests 0x203/0x206, friend messages through 0x301; notices from the auth
   server carry friend requests and online status). */
typedef struct {
    int slot;
    char pol_id[9];
    char name[16];
    int temporary;                  /* asked, not yet accepted */
    int blacklist;
    int online;
} POL_FRIEND;
int pol_friends(POL_SESSION *s, POL_FRIEND *out, int max);
int pol_friend_add(POL_SESSION *s, const char *pol_id, const char *name);
int pol_friend_accept(POL_SESSION *s, const char *pol_id, const char *name);
int pol_friend_remove(POL_SESSION *s, const char *pol_id);
int pol_join(POL_SESSION *s, const char *channel);
int pol_part(POL_SESSION *s, const char *channel);

/* One of the account's content IDs, from the profile server's character list (request 0x103). */
typedef struct {
    unsigned content_class;         /* 1 = FFXI */
    pol_u32 sub_id;                 /* (world << 16) | character, 0 = unused */
    pol_u32 content_id;             /* low 32 bits */
    char name[17];
    char info[65];
} POL_CHARACTER;

/* The account's content IDs (blocking, its own profile server connection); returns how many, or -1. */
int pol_characters(POL_SESSION *s, POL_CHARACTER *out, int max);

/* Add a line to the chat log (also used for the session's own notices). */
void pol_log(POL_SESSION *s, const char *fmt, ...);

void pol_signout(POL_SESSION *s);

#ifdef __cplusplus
}
#endif

#endif
