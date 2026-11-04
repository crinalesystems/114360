/* PlayOnline session over the auth server's IRC and the profile server (polirc.h). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "md5.h"
#include "polirc.h"

/* The NICK credential's 21-character client signature: a build of its own, so OpenLobby keeps this launcher's
   token apart from the Viewer's (TTTTTtIS... on the 360) for the same account. */
#define LAUNCHER_CLIENT_SIG "TTTTT114360LAUNCHERTT"

#define AUTH_PORT 51240
#define PROFILE_PORT 51220
#define SIGNIN_TIMEOUT_MS 8000

void pol_log(POL_SESSION *s, const char *fmt, ...)
{
    char *line = s->log[s->log_count % POL_LOG_LINES];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line, POL_LOG_TEXT, fmt, ap);
    va_end(ap);
    line[POL_LOG_TEXT - 1] = '\0';
    s->log_count++;
}

static void fail(POL_SESSION *s, const char *why)
{
    _snprintf(s->error, sizeof(s->error), "%s", why);
    s->error[sizeof(s->error) - 1] = '\0';
    s->state = POL_FAILED;
    pol_log(s, "PlayOnline: %s", why);
}

/* One IRC line: the message, its 4-character checksum, CR LF; encrypted (from the start of the keystream) once
   the session key is known. */
static int send_line(POL_SESSION *s, const char *message)
{
    char buf[600], sum[5];
    int len = (int)strlen(message);
    if (len > (int)sizeof(buf) - 7) {
        len = (int)sizeof(buf) - 7;
    }
    memcpy(buf, message, len);
    pol_checksum_str((const unsigned char *)buf, len, sum);
    memcpy(buf + len, sum, 4);
    buf[len + 4] = '\r';
    buf[len + 5] = '\n';
    len += 6;
    if (s->crypto_ready) {
        pol_crypt64(&s->cipher, (unsigned char *)buf, len, 1);
    }
    return s->net->send(s->sock, buf, len) == len;
}

/* The next complete line out of s->in, decrypted, without its checksum. Each line the server sends is encrypted
   on its own from the start of the keystream, and the cipher never touches CR or LF, so lines can be cut out of
   the ciphertext before decrypting. Returns 1 with `out` filled, 0 when no full line is buffered. */
static int next_line(POL_SESSION *s, char *out, int size)
{
    int i, n;
    for (i = 0; i < s->in_len && s->in[i] != '\n'; i++) {
    }
    if (i == s->in_len) {
        return 0;
    }
    n = i;
    if (n > 0 && s->in[n - 1] == '\r') {
        n--;
    }
    if (s->crypto_ready) {
        pol_crypt64(&s->cipher, (unsigned char *)s->in, n, 1);
    }
    n = n >= 4 ? n - 4 : 0;
    if (n > size - 1) {
        n = size - 1;
    }
    memcpy(out, s->in, n);
    out[n] = '\0';
    memmove(s->in, s->in + i + 1, s->in_len - i - 1);
    s->in_len -= i + 1;
    return 1;
}

static int fill(POL_SESSION *s, int timeout_ms)
{
    int r;
    if (s->in_len >= (int)sizeof(s->in)) {
        s->in_len = 0;      /* a line longer than the buffer: drop it */
    }
    r = s->net->recv(s->sock, s->in + s->in_len, (int)sizeof(s->in) - s->in_len, timeout_ms);
    if (r > 0) {
        s->in_len += r;
    }
    return r;
}

/* The words of ":prefix COMMAND a b :trailing" (prefix optional); returns their count. `line` is cut up. */
static int split(char *line, char **word, int max, char **trailing)
{
    int n = 0;
    char *p = line;
    *trailing = NULL;
    while (*p && n < max) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (*p == ':' && n > 0) {
            *trailing = p + 1;
            break;
        }
        word[n++] = p;
        while (*p && *p != ' ') {
            p++;
        }
        if (*p) {
            *p++ = '\0';
        }
    }
    return n;
}

/* The sender's PlayOnline ID from a ":UXXXXXXXX!..." prefix (unscrambled), or the prefix itself. */
static void sender(const char *prefix, char *out, int size)
{
    char nick[16];
    int n = 0;
    if (prefix[0] == ':') {
        prefix++;
    }
    while (prefix[n] && prefix[n] != '!' && prefix[n] != '@' && n < (int)sizeof(nick) - 1) {
        nick[n] = prefix[n];
        n++;
    }
    nick[n] = '\0';
    if (n == 9 && nick[0] == 'U') {
        pol_unscramble_id(nick, out);
    } else {
        _snprintf(out, size, "%s", nick);
        out[size - 1] = '\0';
    }
}

/* ---- Friends (Project Crystal POLProfile and POLAuth; OpenLobby speaks the same protocol) ----

   A PlayOnline ID as a number: base 36 over the characters below, high bits for volume and domain (0 here).
   In message headers and notices both IDs are XORed with the server's message key (POL_CRYPTKEY_MESSAGE). The
   public Crystal sources ship it as zero, the live Crystal server uses the value below; the server's first
   notice after sign-in (our own status: source = our ID with the key, target 0) gives it away, so it is
   learned from there and this is only the guess until then. */
#define POL_MESSAGE_KEY_HI 0x1C273E45u
#define POL_MESSAGE_KEY_LO 0x67891133u
static const char POL_ID_CHARS[] = "EFKAOYMJVNGTDSWBQLPCIRHZXU6328401795";

static pol_u64 message_key(POL_SESSION *s)
{
    return s->message_key_known ? s->message_key : (pol_u64)POL_MESSAGE_KEY_HI << 32 | POL_MESSAGE_KEY_LO;
}

/* "ABCD1234" -> its number, or 0 for an ID with other characters. */
static pol_u64 pol_id_number(const char *id)
{
    pol_u64 v = 0;
    int i;
    for (i = 0; i < 8; i++) {
        const char *c = id[i] ? strchr(POL_ID_CHARS, id[i] >= 'a' && id[i] <= 'z' ? id[i] - 32 : id[i]) : NULL;
        if (c == NULL) {
            return 0;
        }
        v = v * 36 + (pol_u64)(c - POL_ID_CHARS);
    }
    return v;
}

static void pol_id_string(pol_u64 v, char out[9])
{
    int i;
    v &= ((pol_u64)1 << 41) - 1;
    for (i = 7; i >= 0; i--) {
        out[i] = POL_ID_CHARS[v % 36];
        v /= 36;
    }
    out[8] = '\0';
}

static pol_u64 get64(const unsigned char *p)
{
    return (pol_u64)(p[0] | (pol_u32)p[1] << 8 | (pol_u32)p[2] << 16 | (pol_u32)p[3] << 24)
         | (pol_u64)(p[4] | (pol_u32)p[5] << 8 | (pol_u32)p[6] << 16 | (pol_u32)p[7] << 24) << 32;
}

static void put64(unsigned char *p, pol_u64 v)
{
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (unsigned char)(v >> (8 * i));
    }
}

/* Message header (0x48 bytes, POLCommon MessageHeader): +0x00 source ID, +0x08 target ID (both with the key),
   +0x10 sender name[16] or, in a status notice, status (+0x11 status, 0 = offline), +0x18 source handle,
   +0x1C friend list position, +0x20 subject[16], +0x30 sequence, +0x34 time, +0x38 payload length, +0x3E u16:
   data type (7 bits), message type (5 bits), code (2), online, valid; +0x40 content ID, +0x42 bit 0 = from
   the profile server. */
#define MSG_HEADER 0x48
enum { MSG_FRIEND_REQUEST = 0x01, MSG_FRIEND_ACCEPT = 0x09, MSG_FRIEND_DECLINE = 0x0A, MSG_FRIEND_DECLINE2 = 0x0B,
       MSG_FRIEND_DECLINE_TEXT = 0x0C, MSG_STATUS = 0x1F };

static int message_type(const unsigned char *h)
{
    return ((h[0x3E] | h[0x3F] << 8) >> 7) & 0x1F;
}

static int friend_index(POL_FRIEND_STATE *list, int count, const char *id)
{
    int i;
    for (i = 0; i < count; i++) {
        if (strcmp(list[i].pol_id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/* A message header for us: 0x60+ characters of PlayOnline base64 with no spaces, marked valid, whose target is
   our ID or nobody (status notices). The profile server's notifier relays these as PRIVMSGs. */
static int addressed_to_us(POL_SESSION *s, const char *text)
{
    unsigned char h[0x60];
    pol_u64 t;
    if ((int)strlen(text) < 0x60 || strchr(text, ' ') != NULL || pol_base64_decode(text, 0x60, h) < MSG_HEADER) {
        return 0;
    }
    if (!(h[0x3F] & 0x80)) {
        return 0;                   /* the header's valid bit */
    }
    t = get64(h + 8);               /* nobody: 0, raw or with the key; else our ID */
    return t == 0 || (t ^ message_key(s)) == 0 || (t ^ message_key(s)) == pol_id_number(s->pol_id);
}

/* A notice from the server: a message header in PlayOnline base64 (0x60 characters), then its payload. */
static void friend_notice(POL_SESSION *s, const char *text, int from_server)
{
    unsigned char h[0x60];
    char from[9], name[17];
    int type, i;
    if ((int)strlen(text) < 0x60 || pol_base64_decode(text, 0x60, h) < MSG_HEADER) {
        return;
    }
    type = message_type(h);
    if (from_server && type == MSG_STATUS && get64(h + 8) == 0 && !s->message_key_known) {
        s->message_key = get64(h) ^ pol_id_number(s->pol_id);
        s->message_key_known = 1;
        return;
    }
    pol_id_string(get64(h) ^ message_key(s), from);
    if (type == MSG_STATUS) {
        int online = h[0x11] != 0;
        i = friend_index(s->friend_state, s->friend_state_count, from);
        if (i < 0 && s->friend_state_count < POL_MAX_FRIENDS) {
            i = s->friend_state_count++;
            strcpy(s->friend_state[i].pol_id, from);
            s->friend_state[i].online = -1;
        }
        if (i >= 0 && s->friend_state[i].online != online) {
            if (s->friend_state[i].online != -1 || online) {
                pol_log(s, "friend %s is %s", from, online ? "online" : "offline");
            }
            s->friend_state[i].online = online;
        }
        return;
    }
    memcpy(name, h + 0x10, 16);
    name[16] = '\0';
    if (type == MSG_FRIEND_REQUEST) {
        if (friend_index(s->requests, s->request_count, from) < 0 && s->request_count < POL_MAX_FRIENDS) {
            strcpy(s->requests[s->request_count].pol_id, from);
            s->requests[s->request_count++].online = -1;
        }
        pol_log(s, "%s (%s) asks to be your friend: /friend accept %s", name[0] ? name : from, from, from);
    } else if (type == MSG_FRIEND_ACCEPT) {
        pol_log(s, "%s accepted your friend request", from);
    } else if (type >= MSG_FRIEND_DECLINE && type <= MSG_FRIEND_DECLINE_TEXT) {
        pol_log(s, "%s declined your friend request", from);
    }
}

/* Handle one line once signed in: PING, chat and channel traffic. */
static void handle(POL_SESSION *s, char *line)
{
    char *w[8], *text, who[24];
    int n;
    if (strncmp(line, "PING", 4) == 0) {
        char pong[128];
        _snprintf(pong, sizeof(pong), "PONG%s", line + 4);
        pong[sizeof(pong) - 1] = '\0';
        send_line(s, pong);
        return;
    }
    n = split(line, w, 8, &text);
    if (n >= 2 && w[0][0] == ':') {
        sender(w[0], who, sizeof(who));
        if ((strcmp(w[1], "PRIVMSG") == 0 || strcmp(w[1], "NOTICE") == 0) && n >= 3 && text) {
            if (strcmp(w[1], "NOTICE") == 0 && w[2][0] != '#' && strchr(who, '.') != NULL) {
#ifdef POL_DEBUG_NOTICES
                pol_log(s, "notice from %s: %s", who, text);
#endif
                friend_notice(s, text, 1);  /* the server's binary notifications: our own status first */
                return;
            }
            if (strcmp(w[1], "NOTICE") == 0 && w[2][0] != '#' && strcmp(who, "pol") == 0) {
                /* The login gate record (`:pol!~x@... NOTICE <nick> :<base64>`: lobby address, unread count,
                   account flags) that ends the welcome. It is for the Viewer; the sign-in is already logged. */
                return;
            }
            if (w[2][0] == '#' || w[2][0] == '&') {
                pol_log(s, "[%s] %s: %s", w[2], who, text);
            } else if (addressed_to_us(s, text)) {
                friend_notice(s, text, 0);  /* a message header the profile server relays as a PRIVMSG */
            } else {
                pol_log(s, "%s >> %s", who, text);
            }
        } else if (strcmp(w[1], "JOIN") == 0) {
            pol_log(s, "%s joined %s", who, n >= 3 ? w[2] : (text ? text : ""));
        } else if (strcmp(w[1], "PART") == 0 || strcmp(w[1], "QUIT") == 0) {
            pol_log(s, "%s left %s", who, n >= 3 ? w[2] : "");
        } else if (strcmp(w[1], "KICK") == 0) {
            pol_log(s, "kicked: %s", text ? text : "");
        } else if (w[1][0] >= '4' && w[1][0] <= '5' && strlen(w[1]) == 3 && strcmp(w[1], "422") != 0) {
            pol_log(s, "server %s %s", w[1], text ? text : "");     /* 422 is only "no message of the day" */
        }
    }
}

/* One profile server request on its own connection: hello, then request `op` with `body`, then the answer's
   object into `obj`. Each connection uses its own copy of the session cipher, which the profile server keys
   from the sign-in too. Returns the object's length, or -1. */
static int profile_request(POL_SESSION *s, unsigned op, const unsigned char *body, int body_len,
                           unsigned char *obj, int obj_max)
{
    static POL_CIPHER c;
    static unsigned char out[0x800];        /* the largest request: the handle list store, 0x648 bytes */
    unsigned char hello[0x28], header[0x28], answer[0x18];
    long sock;
    int got, len, r;
    pol_u32 objlen;
    if (body_len > (int)sizeof(out)) {
        return -1;
    }
    memcpy(&c, &s->cipher, sizeof(c));
    sock = s->net->connect(s->server, PROFILE_PORT);
    if (sock <= 0) {
        return -1;
    }
    memset(hello, 0, sizeof(hello));
    hello[4] = 1;
    hello[6] = (unsigned char)s->client_port;
    hello[7] = (unsigned char)(s->client_port >> 8);
    hello[8] = (unsigned char)s->client_ip;
    hello[9] = (unsigned char)(s->client_ip >> 8);
    hello[10] = (unsigned char)(s->client_ip >> 16);
    hello[11] = (unsigned char)(s->client_ip >> 24);
    s->net->send(sock, hello, sizeof(hello));
    for (got = 0; got < 0x18; got += r) {
        r = s->net->recv(sock, answer + got, 0x18 - got, SIGNIN_TIMEOUT_MS);
        if (r <= 0) {
            s->net->close(sock);
            return -1;
        }
    }
    if (answer[0] != 0x81) {
        s->net->close(sock);
        return -1;
    }
    memset(header, 0, sizeof(header));
    header[0] = 0x02;                       /* request: type, opcode high byte, opcode low (u16), object size */
    header[1] = (unsigned char)(op >> 8);
    header[2] = (unsigned char)op;
    header[4] = (unsigned char)body_len;
    header[5] = (unsigned char)(body_len >> 8);
    pol_crypt64(&c, header, sizeof(header), 1);
    s->net->send(sock, header, sizeof(header));
    if (body_len > 0) {
        memcpy(out, body, body_len);
        pol_crypt64(&c, out, body_len, 0);
        s->net->send(sock, out, body_len);
    }
    for (got = 0; got < 0x18; got += r) {
        r = s->net->recv(sock, answer + got, 0x18 - got, SIGNIN_TIMEOUT_MS);
        if (r <= 0) {
            s->net->close(sock);
            return -1;
        }
    }
    pol_crypt64(&c, answer, 0x18, 1);
    objlen = answer[4] | (pol_u32)answer[5] << 8 | (pol_u32)answer[6] << 16 | (pol_u32)answer[7] << 24;
    if (answer[0] != 0x83 || answer[1] != 0 || objlen > (pol_u32)obj_max) {
        s->net->close(sock);
        return -1;
    }
    len = (int)objlen;
    for (got = 0; got < len; got += r) {
        r = s->net->recv(sock, obj + got, len - got, SIGNIN_TIMEOUT_MS);
        if (r <= 0) {
            s->net->close(sock);
            return -1;
        }
    }
    s->net->close(sock);
    if (len > 0) {
        pol_crypt64(&c, obj, len, 0);
    }
    return len;
}

/* Request 0x405 (status update, "online"): its answer starts with the session's random value. */
static int fetch_random(POL_SESSION *s)
{
    unsigned char status[0x28], obj[0x40];
    memset(status, 0, sizeof(status));
    status[0x10 + 6] = 1;                   /* StatusInfo.OpenStat: online */
    if (profile_request(s, 0x405, status, sizeof(status), obj, sizeof(obj)) < 0x14) {
        return 0;
    }
    memcpy(s->random, obj, 16);
    return 1;
}

/* Request 0x103 (LoadCharacterList): u32 count, 4 bytes, then count records of 0x68 bytes (POLProfile
   CharacterData): +0x08 u16 content class, +0x0C u32 content sub ID, +0x10 u64 content ID, +0x18 name[16],
   +0x28 info[64]. */
int pol_characters(POL_SESSION *s, POL_CHARACTER *out, int max)
{
    static unsigned char obj[8 + 32 * 0x68];
    int len, count, i;
    if (s->state != POL_ONLINE) {
        return -1;
    }
    len = profile_request(s, 0x103, NULL, 0, obj, sizeof(obj));
    if (len < 8) {
        return -1;
    }
    count = obj[0] | obj[1] << 8;
    if (count > (len - 8) / 0x68) {
        count = (len - 8) / 0x68;
    }
    if (count > max) {
        count = max;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *r = obj + 8 + i * 0x68;
        out[i].content_class = r[8] | r[9] << 8;
        out[i].sub_id = r[0x0C] | (pol_u32)r[0x0D] << 8 | (pol_u32)r[0x0E] << 16 | (pol_u32)r[0x0F] << 24;
        out[i].content_id = r[0x10] | (pol_u32)r[0x11] << 8 | (pol_u32)r[0x12] << 16 | (pol_u32)r[0x13] << 24;
        memcpy(out[i].name, r + 0x18, 16);
        out[i].name[16] = '\0';
        memcpy(out[i].info, r + 0x28, 64);
        out[i].info[64] = '\0';
    }
    return count;
}

/* Friend list record (0xA8 bytes, POLProfile FriendData): +0x00 u64 bits: 0 valid, 4 blacklist, 5-6 level, 7-12
   the friend's handle, 13-50 handle ID (the server fills it), 57-60 group, 62 temporary (not yet accepted);
   +0x08 slot, +0x09 shown position, +0x10 the friend's ID (without the key), +0x18 name[16], then 8 character
   records the server fills. */
#define FRIEND_RECORD 0xA8
#define FRIEND_SLOTS 100
#define FRIEND_TEMPORARY ((pol_u64)1 << 62)

static int load_friends(POL_SESSION *s, POL_FRIEND *out, int max)
{
    static unsigned char obj[8 + FRIEND_SLOTS * FRIEND_RECORD];
    int len, count, i, n = 0;
    if (s->state != POL_ONLINE) {
        return -1;
    }
    len = profile_request(s, 0x203, NULL, 0, obj, sizeof(obj));
    if (len < 8) {
        return -1;
    }
    count = obj[0] | obj[1] << 8;
    if (count > (len - 8) / FRIEND_RECORD) {
        count = (len - 8) / FRIEND_RECORD;
    }
    for (i = 0; i < count && n < max; i++) {
        const unsigned char *r = obj + 8 + i * FRIEND_RECORD;
        pol_u64 bits = get64(r);
        int k;
        if (!(bits & 1)) {
            continue;
        }
        out[n].slot = r[8];
        out[n].blacklist = (int)(bits >> 4 & 1);
        out[n].temporary = (bits & FRIEND_TEMPORARY) != 0;
        pol_id_string(get64(r + 0x10), out[n].pol_id);
        memcpy(out[n].name, r + 0x18, 15);
        out[n].name[15] = '\0';
        k = friend_index(s->friend_state, s->friend_state_count, out[n].pol_id);
        out[n].online = k >= 0 && s->friend_state[k].online == 1;
        n++;
    }
    return n;
}

int pol_friends(POL_SESSION *s, POL_FRIEND *out, int max)
{
    return load_friends(s, out, max);
}

/* Request 0x206 (StoreFriendList) with one record: a header of two index lists (0xC8 and 0x64 bytes, unused by
   the server) and the record count at +0x12C, the records, 8 bytes of trailer. An invalid record deletes. */
static int store_friend(POL_SESSION *s, int slot, pol_u64 id, const char *name, pol_u64 bits)
{
    static unsigned char body[0x130 + FRIEND_RECORD + 8];
    unsigned char answer[0x200];
    unsigned char *r = body + 0x130;
    memset(body, 0, sizeof(body));
    body[0x12C] = 1;
    put64(r, bits);
    r[8] = (unsigned char)slot;
    r[9] = (unsigned char)slot;
    put64(r + 0x10, id);
    if (name != NULL) {
        strncpy((char *)r + 0x18, name, 15);
    }
    return profile_request(s, 0x206, body, sizeof(body), answer, sizeof(answer)) >= 0;
}

/* Request 0x301 (WriteFile) of a message into the target's box, "O/m/<the header in PlayOnline base64>": the
   server files it and passes the header on to the target as a notice. File operation (0x198 bytes): +0 our
   handle, +1 theirs, +8 the target's ID, +0x10 path[0x180], +0x190 offset, +0x194 length; then the text,
   "<title>\x07<text>\0". The header's payload length counts the text with its NUL. */
static int write_message(POL_SESSION *s, pol_u64 target, int type, const char *subject, const char *title,
                         const char *text)
{
    static unsigned char body[0x198 + 0x200];
    unsigned char h[MSG_HEADER], answer[0x40];
    int tlen = (int)strlen(title), xlen = (int)strlen(text), plen = tlen + 1 + xlen + 1;
    pol_u32 bits;
    if (plen > 0x200) {
        return 0;
    }
    memset(h, 0, sizeof(h));
    put64(h, pol_id_number(s->pol_id) ^ message_key(s));
    put64(h + 8, target ^ message_key(s));
    memcpy(h + 0x10, s->pol_id, 8);                         /* sender: our handle; the ID stands in for it */
    strncpy((char *)h + 0x20, subject, 15);
    s->message_seq++;
    h[0x30] = (unsigned char)s->message_seq;
    h[0x31] = (unsigned char)(s->message_seq >> 8);
    h[0x34] = (unsigned char)s->login_time;
    h[0x35] = (unsigned char)(s->login_time >> 8);
    h[0x36] = (unsigned char)(s->login_time >> 16);
    h[0x37] = (unsigned char)(s->login_time >> 24);
    h[0x38] = (unsigned char)plen;
    h[0x39] = (unsigned char)(plen >> 8);
    bits = (pol_u32)type << 7 | 0x4000 | 0x8000;            /* online, valid */
    h[0x3E] = (unsigned char)bits;
    h[0x3F] = (unsigned char)(bits >> 8);
    memset(body, 0, sizeof(body));
    put64(body + 8, target);
    memcpy(body + 0x10, "O/m/", 4);
    pol_base64_encode(h, MSG_HEADER, 0, (char *)body + 0x14);
    body[0x194] = (unsigned char)plen;
    body[0x195] = (unsigned char)(plen >> 8);
    memcpy(body + 0x198, title, tlen);
    body[0x198 + tlen] = 0x07;
    memcpy(body + 0x198 + tlen + 1, text, xlen);
    return profile_request(s, 0x301, body, 0x198 + plen, answer, sizeof(answer)) >= 0;
}

static int free_slot(const POL_FRIEND *list, int n)
{
    int slot, i;
    for (slot = 0; slot < FRIEND_SLOTS; slot++) {
        for (i = 0; i < n && list[i].slot != slot; i++) {
        }
        if (i == n) {
            return slot;
        }
    }
    return -1;
}

static POL_FRIEND friend_scratch[FRIEND_SLOTS];
/* Friend links name a handle (a PlayOnline screen name; an account has up to 64), on both sides: the server
   refuses a friend entry for someone without one. The Viewer makes the first handle when an account is set up;
   an account made elsewhere (Crystal's web sign-up) has none, so one is made here, named after the ID.
     0x009 LoadHandleList: u32 count, 4 bytes, records of 0x88 (+0 position, +0x10 name[16]).
     0x008 StoreHandleList: 0x40 bytes of shown positions (by creation position), then 0x40 updates of 0x18
           bytes (+0 mode: 1 store, 2 delete; +1 position; +2 open level; +8 name[16]). */
static int ensure_handle(POL_SESSION *s)
{
    static unsigned char obj[8 + 64 * 0x88], body[0x648];
    int len;
    if (s->handle_name[0]) {
        return 1;
    }
    len = profile_request(s, 0x009, NULL, 0, obj, sizeof(obj));
    if (len >= 8 + 0x88 && (obj[0] | obj[1] << 8) > 0) {
        int count = obj[0] | obj[1] << 8, i;
        char name[17];
        memcpy(s->handle_name, obj + 8 + 0x10, 16);
        s->handle_name[16] = '\0';
        for (i = 0; i < count && 8 + (i + 1) * 0x88 <= len; i++) {
            memcpy(name, obj + 8 + i * 0x88 + 0x10, 16);
            name[16] = '\0';
            pol_log(s, "PlayOnline: handle %d: %s", i + 1, name);
        }
        return 1;
    }
    if (len < 0) {
        return 0;
    }
    memset(body, 0, sizeof(body));
    for (len = 0; len < 0x40; len++) {
        body[len] = (unsigned char)len;
    }
    body[0x40] = 1;
    memcpy(body + 0x40 + 8, s->pol_id, 8);
    if (profile_request(s, 0x008, body, sizeof(body), obj, sizeof(obj)) < 0) {
        return 0;
    }
    memcpy(s->handle_name, s->pol_id, 9);
    pol_log(s, "PlayOnline: made the handle %s for this account", s->handle_name);
    return 1;
}

static int on_list(POL_SESSION *s, const char *pol_id)
{
    int n = load_friends(s, friend_scratch, FRIEND_SLOTS), i;
    for (i = 0; i < n; i++) {
        if (strcmp(friend_scratch[i].pol_id, pol_id) == 0) {
            return 1;
        }
    }
    return 0;
}


/* Ask `pol_id` to be a friend: our side is stored as a temporary entry, then a friend request message goes to
   them. The link becomes real when they accept (their accept message reaches the server). */
int pol_friend_add(POL_SESSION *s, const char *pol_id, const char *name)
{
    pol_u64 id = pol_id_number(pol_id);
    int n, i, slot;
    if (id == 0 || strlen(pol_id) != 8) {
        pol_log(s, "friend: %s is not a PlayOnline ID (8 letters and digits)", pol_id);
        return 0;
    }
    n = load_friends(s, friend_scratch, FRIEND_SLOTS);
    if (n < 0) {
        pol_log(s, "friend: the profile server did not answer");
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(friend_scratch[i].pol_id, pol_id) == 0) {
            pol_log(s, "friend: %s is already on your list%s", pol_id,
                    friend_scratch[i].temporary ? " (waiting for them to accept)" : "");
            return 0;
        }
    }
    slot = free_slot(friend_scratch, n);
    if (slot < 0 || !ensure_handle(s) || !store_friend(s, slot, id, name && name[0] ? name : pol_id, 1 | FRIEND_TEMPORARY)) {
        pol_log(s, "friend: could not add %s to your list", pol_id);
        return 0;
    }
    if (!on_list(s, pol_id)) {
        pol_log(s, "friend: the server did not keep %s (no such ID, or it has no PlayOnline handle yet)", pol_id);
        return 0;
    }
    if (!write_message(s, id, MSG_FRIEND_REQUEST, "Friend request", "Friend request",
                       "Please add me to your friend list.")) {
        pol_log(s, "friend: could not send the request to %s", pol_id);
        return 0;
    }
    pol_log(s, "friend request sent to %s", pol_id);
    return 1;
}

/* Accept `pol_id`'s friend request: keep them on our list, then send the accept message, which makes the
   server confirm both sides. */
int pol_friend_accept(POL_SESSION *s, const char *pol_id, const char *name)
{
    pol_u64 id = pol_id_number(pol_id);
    int n, i, slot = -1;
    if (id == 0 || strlen(pol_id) != 8) {
        pol_log(s, "friend: %s is not a PlayOnline ID", pol_id);
        return 0;
    }
    n = load_friends(s, friend_scratch, FRIEND_SLOTS);
    if (n < 0) {
        pol_log(s, "friend: the profile server did not answer");
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(friend_scratch[i].pol_id, pol_id) == 0) {
            slot = friend_scratch[i].slot;
        }
    }
    if (slot < 0) {
        slot = free_slot(friend_scratch, n);
        if (slot < 0 || !ensure_handle(s)
            || !store_friend(s, slot, id, name && name[0] ? name : pol_id, 1 | FRIEND_TEMPORARY)) {
            pol_log(s, "friend: could not add %s to your list", pol_id);
            return 0;
        }
    }
    if (!write_message(s, id, MSG_FRIEND_ACCEPT, "Friend accepted", "Friend accepted",
                       "Your friend request was accepted.")) {
        pol_log(s, "friend: could not send the acceptance to %s", pol_id);
        return 0;
    }
    i = friend_index(s->requests, s->request_count, pol_id);
    if (i >= 0) {
        s->requests[i] = s->requests[--s->request_count];
    }
    pol_log(s, "%s is now your friend", pol_id);
    return 1;
}

int pol_friend_remove(POL_SESSION *s, const char *pol_id)
{
    int n, i;
    n = load_friends(s, friend_scratch, FRIEND_SLOTS);
    for (i = 0; i < n; i++) {
        if (strcmp(friend_scratch[i].pol_id, pol_id) == 0) {
            if (!store_friend(s, friend_scratch[i].slot, 0, NULL, 0)) {
                pol_log(s, "friend: could not remove %s", pol_id);
                return 0;
            }
            pol_log(s, "%s removed from your friend list", pol_id);
            return 1;
        }
    }
    pol_log(s, "friend: %s is not on your list", pol_id);
    return 0;
}

/* Each entry's rules come from the server that uses it: the whole InitData as salt is what Project Crystal
   checks (POLAuth Client.DoLogin, SqCrypto.GeneratePOLPasswordMD5) and what signed in there on 2026-11-03;
   20100601_1 is the 360 PlayOnline of that era (Square Enix's patch server, X2U-1000), whose own PolCore has not
   been disassembled to confirm it. 40 characters is 20110726_A's, which OpenLobby checks. */
const POL_VERSION pol_versions[POL_VERSION_COUNT] = {
    {"20100601_1", "Project Crystal", 0},
    {"20110726_A", "OpenLobby", 40},
};

const POL_VERSION *pol_version_find(const char *version)
{
    int i;
    for (i = 0; i < POL_VERSION_COUNT; i++) {
        if (strcmp(pol_versions[i].version, version) == 0) {
            return &pol_versions[i];
        }
    }
    return NULL;
}

/* The log survives a new sign-in (a second login, /login): only the session part is reset. */
static void reset_session(POL_SESSION *s)
{
    static char log[POL_LOG_LINES][POL_LOG_TEXT];
    int count = s->log_count;
    memcpy(log, s->log, sizeof(log));
    memset(s, 0, sizeof(*s));
    memcpy(s->log, log, sizeof(log));
    s->log_count = count;
}

int pol_signin(POL_SESSION *s, const POL_NET *net, pol_u32 server, const POL_VERSION *version, const char *pol_id,
               const char *password)
{
    char line[600], *w[8], *text, msg[600], md5hex[33];
    int n, waited = 0, sent_user = 0, redirects = 0;
    unsigned short current_port = AUTH_PORT;
    reset_session(s);
    s->net = net;
    s->server = server;
    s->state = POL_SIGNING_IN;
    _snprintf(s->pol_id, sizeof(s->pol_id), "%s", pol_id);
    s->pol_id[8] = '\0';
    pol_scramble_id(s->pol_id, s->nick);
    pol_log(s, "PlayOnline: signing in as %s (PlayOnline %s)", s->pol_id, version->version);
    s->sock = net->connect(server, AUTH_PORT);
    if (s->sock <= 0) {
        fail(s, "cannot reach the PlayOnline server");
        return 0;
    }
    while (waited < SIGNIN_TIMEOUT_MS) {
        while (next_line(s, line, sizeof(line))) {
            if (s->crypto_ready && strncmp(line, "PING", 4) != 0) {
                pol_log(s, "PlayOnline: < %s", line);   /* the server's lines once readable, up to the welcome */
            }
            n = split(line, w, 8, &text);
            if (n >= 4 && strcmp(w[1], "300") == 0 && !sent_user) {
                unsigned char init[96], modulus[32];
                char rsa[64];
                pol_base32_decode(w[3], (int)strlen(w[3]), init);
                /* A login directory (OpenLobby's 51240, like SE's) answers with a redirect record: an auth node's
                   address at +8 and port at +12 (0 = 10440), then closes. Dial that port on the configured server
                   (the advertised address can be a LAN one the client cannot reach) and start over there.
                   Project Crystal fills the record on every greeting with its own address and 51240, the port
                   already in use (POLAuth Irc/Client.cs: "redirect" to itself): that means carry on here, or the
                   sign-in would dial 51240 again and get the same greeting. */
                if ((init[8] | init[9] | init[10] | init[11]) != 0 && redirects < 2) {
                    unsigned short port = (unsigned short)(init[12] << 8 | init[13]);
                    if (port == 0) {
                        port = 10440;
                    }
                    if (port != current_port) {
                        pol_log(s, "PlayOnline: directory redirect to %u.%u.%u.%u:%u, dialling port %u", init[8],
                                init[9], init[10], init[11], port, port);
                        net->close(s->sock);
                        s->in_len = 0;
                        redirects++;
                        current_port = port;
                        s->sock = net->connect(server, port);
                        if (s->sock <= 0) {
                            fail(s, "cannot reach the PlayOnline auth server");
                            return 0;
                        }
                        break;
                    }
                }
                _snprintf(s->init_base32, sizeof(s->init_base32), "%s", w[3]);
                s->init_base32[sizeof(s->init_base32) - 1] = '\0';
                s->login_time = (pol_u32)init[0] << 24 | (pol_u32)init[1] << 16 | (pol_u32)init[2] << 8 | init[3];
                s->client_ip = (pol_u32)init[4] << 24 | (pol_u32)init[5] << 16 | (pol_u32)init[6] << 8 | init[7];
                s->client_port = (unsigned short)(init[0x14] << 8 | init[0x15]);
                pol_rsa_modulus_le(modulus);
                pol_base64_encode(modulus, 32, 1, rsa);
                _snprintf(msg, sizeof(msg), "USER x 8 * :%s", rsa);
                send_line(s, msg);
                sent_user = 1;
            } else if (n >= 4 && strcmp(w[1], "300") == 0 && !s->crypto_ready) {
                unsigned char sealed[64], key[8], rev[8], modulus[32];
                int i;
                if (pol_base64_decode(w[3], (int)strlen(w[3]), sealed) < 32) {
                    fail(s, "bad session key from the server");
                    net->close(s->sock);
                    return 0;
                }
                pol_rsa_session_key(sealed, key);
                for (i = 0; i < 8; i++) {
                    rev[i] = key[7 - i];
                }
                pol_rsa_modulus_le(modulus);
                pol_cipher_init(&s->cipher, rev,
                                modulus[0] | (pol_u32)modulus[1] << 8 | (pol_u32)modulus[2] << 16 | (pol_u32)modulus[3] << 24,
                                modulus[4] | (pol_u32)modulus[5] << 8 | (pol_u32)modulus[6] << 16 | (pol_u32)modulus[7] << 24);
                s->crypto_ready = 1;
                pol_password_md5(s->init_base32, version->salt_chars, password, md5hex);
                {
                    /* PolCore's third field: client signature [21] + an 11-character token that is fixed for one
                       account and password + a 4-character per-session nonce. OpenLobby binds the token to the
                       signature on the first login whose password digest checks out and wants it on every
                       login after; Crystal ignores it. The token here is SE-base64 of md5(ID ":" password). */
                    char seed[64], token[24], nonce[8];
                    unsigned char digest[16], stamp[3];
                    _snprintf(seed, sizeof(seed), "%s:%s", s->pol_id, password);
                    seed[sizeof(seed) - 1] = '\0';
                    md5(seed, (unsigned int)strlen(seed), digest);
                    pol_base64_encode(digest, 9, 0, token);
                    stamp[0] = (unsigned char)(s->login_time >> 16);
                    stamp[1] = (unsigned char)(s->login_time >> 8);
                    stamp[2] = (unsigned char)s->login_time;
                    pol_base64_encode(stamp, 3, 0, nonce);
                    _snprintf(msg, sizeof(msg), "NICK %s:%s:%s%.11s%.4s", s->nick, md5hex, LAUNCHER_CLIENT_SIG, token,
                              nonce);
                }
                send_line(s, msg);
            } else if (n >= 2 && strcmp(w[1], "001") == 0) {
                if (!fetch_random(s)) {
                    fail(s, "signed in, but the profile server gave no credential");
                    net->close(s->sock);
                    return 0;
                }
                pol_contents_auth(s->random, s->login_time, s->cipher.key, s->client_ip, s->client_port, s->auth);
                s->have_credential = 1;
                s->state = POL_ONLINE;
                pol_log(s, "PlayOnline: signed in as %s", s->pol_id);
                ensure_handle(s);   /* others can only add a friend who has a handle */
                return 1;
            } else if (n >= 2 && (strcmp(w[1], "KICK") == 0 || (strlen(w[1]) == 3 && (w[1][0] == '4' || w[1][0] == '5')))) {
                fail(s, "sign-in refused (wrong PlayOnline ID or password?)");
                net->close(s->sock);
                return 0;
            } else if (n >= 1 && strcmp(w[0], "ERROR") == 0) {
                /* Project Crystal turns a wrong password away with "ERROR :Closing Link: [unknown@ip] (POL <code>)"
                   (AdminError 0xC9) and no numeric. */
                fail(s, "sign-in refused: wrong password or PlayOnline version");
                net->close(s->sock);
                return 0;
            } else if (strncmp(line, "PING", 4) == 0) {
                handle(s, line);
            }
        }
        if (fill(s, 250) < 0) {
            fail(s, "the PlayOnline server closed the connection");
            net->close(s->sock);
            return 0;
        }
        waited += 250;
    }
    fail(s, "no answer from the PlayOnline server");
    net->close(s->sock);
    return 0;
}

int pol_poll(POL_SESSION *s, int timeout_ms)
{
    char line[600];
    int r;
    if (s->state != POL_ONLINE) {
        return 0;
    }
    r = fill(s, timeout_ms);
    while (next_line(s, line, sizeof(line))) {
        handle(s, line);
    }
    if (r < 0) {
        s->state = POL_OFFLINE;
        s->net->close(s->sock);
        pol_log(s, "PlayOnline: connection closed");
        return 0;
    }
    return 1;
}

int pol_say(POL_SESSION *s, const char *target, const char *text)
{
    char msg[600], nick[10];
    if (s->state != POL_ONLINE || !target[0] || !text[0]) {
        return 0;
    }
    if (target[0] == '#' || target[0] == '&') {
        _snprintf(msg, sizeof(msg), "PRIVMSG %s :%s", target, text);
        pol_log(s, "[%s] %s: %s", target, s->pol_id, text);
    } else {
        pol_scramble_id(target, nick);
        _snprintf(msg, sizeof(msg), "PRIVMSG %s :%s", nick, text);
        pol_log(s, "%s << %s", target, text);
    }
    msg[sizeof(msg) - 1] = '\0';
    return send_line(s, msg);
}

/* JOIN, then MODE +n: Crystal's POLAuth (Channel.SendPrvMsg) only relays channel messages while the channel has
   mode n set, otherwise it answers 404 "Cannot send to channel". Whoever creates a channel is its operator, so
   the first member sets it; for later members the MODE is refused and harmless. */
int pol_join(POL_SESSION *s, const char *channel)
{
    char msg[128];
    if (s->state != POL_ONLINE) {
        return 0;
    }
    _snprintf(msg, sizeof(msg), "JOIN %s", channel);
    msg[sizeof(msg) - 1] = '\0';
    if (!send_line(s, msg)) {
        return 0;
    }
    _snprintf(msg, sizeof(msg), "MODE %s +n", channel);
    msg[sizeof(msg) - 1] = '\0';
    return send_line(s, msg);
}

int pol_part(POL_SESSION *s, const char *channel)
{
    char msg[128];
    if (s->state != POL_ONLINE) {
        return 0;
    }
    _snprintf(msg, sizeof(msg), "PART %s", channel);
    msg[sizeof(msg) - 1] = '\0';
    return send_line(s, msg);
}

void pol_signout(POL_SESSION *s)
{
    if (s->state == POL_ONLINE) {
        send_line(s, "QUIT :114360");
        s->net->close(s->sock);
    }
    s->state = POL_OFFLINE;
    s->have_credential = 0;
}
