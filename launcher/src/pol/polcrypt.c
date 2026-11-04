/* PlayOnline sign-in crypto (polcrypt.h), ported from Project Crystal's POLCommon/SqCrypto.cs and Blowfish.cs. */
#include <string.h>
#include "md5.h"
#include "polcrypt.h"

static const unsigned char b32_encode[32] = {
    0x4e, 0x34, 0x33, 0x4f, 0x56, 0x48, 0x42, 0x4a, 0x31, 0x59, 0x32, 0x43, 0x30, 0x57, 0x53, 0x58,
    0x45, 0x44, 0x35, 0x51, 0x46, 0x49, 0x4c, 0x52, 0x5a, 0x4d, 0x55, 0x54, 0x41, 0x50, 0x47, 0x4b};
static const unsigned char b32_decode[92] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0c, 0x08, 0x0a, 0x02, 0x01, 0x12, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x1c, 0x06, 0x0b, 0x11, 0x10, 0x14, 0x1e, 0x05, 0x15, 0x07, 0x1f, 0x16, 0x19, 0x00, 0x03,
    0x1d, 0x13, 0x17, 0x0e, 0x1b, 0x1a, 0x04, 0x0d, 0x0f, 0x09, 0x18, 0x00};
static const unsigned char b64_encode[64] = {
    0x54, 0x53, 0x47, 0x38, 0x49, 0x6e, 0x63, 0x57, 0x33, 0x48, 0x46, 0x4b, 0x6f, 0x6b, 0x4f, 0x67,
    0x37, 0x39, 0x71, 0x7a, 0x65, 0x43, 0x6d, 0x5a, 0x73, 0x32, 0x79, 0x42, 0x59, 0x45, 0x51, 0x56,
    0x41, 0x55, 0x78, 0x52, 0x35, 0x72, 0x62, 0x77, 0x69, 0x34, 0x50, 0x40, 0x6a, 0x4d, 0x44, 0x4c,
    0x74, 0x70, 0x76, 0x61, 0x64, 0x30, 0x66, 0x5f, 0x4a, 0x31, 0x68, 0x6c, 0x4e, 0x36, 0x75, 0x58};
static const unsigned char b64_decode[124] = {
    0x40, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x35, 0x39, 0x19, 0x08, 0x29, 0x24, 0x3d, 0x10, 0x03, 0x11, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x2B, 0x20, 0x1B, 0x15, 0x2E, 0x1D, 0x0A, 0x02, 0x09, 0x04, 0x38, 0x0B, 0x2F, 0x2D, 0x3C, 0x0E,
    0x2A, 0x1E, 0x23, 0x01, 0x00, 0x21, 0x1F, 0x07, 0x3F, 0x1C, 0x17, 0xFF, 0xFF, 0xFF, 0xFF, 0x37,
    0xFF, 0x33, 0x26, 0x06, 0x34, 0x14, 0x36, 0x0F, 0x3A, 0x28, 0x2C, 0x0D, 0x3B, 0x16, 0x05, 0x0C,
    0x31, 0x12, 0x25, 0x18, 0x30, 0x3E, 0x32, 0x27, 0x22, 0x1A, 0x13, 0x00};
static const unsigned char char_to_id[92] = {
    0xD8, 0x35, 0xE8, 0xD4, 0x66, 0x82, 0x64, 0x98, 0xD9, 0xA8, 0x87, 0x75, 0x65, 0x70, 0x5A, 0x8A,
    0x3F, 0x62, 0x80, 0x29, 0x44, 0xDE, 0x7C, 0xA5, 0x89, 0x4E, 0x57, 0x59, 0xD3, 0x51, 0xAD, 0xAC,
    0x86, 0x95, 0x80, 0xEC, 0x17, 0xE4, 0x85, 0xF1, 0x8C, 0x0C, 0x66, 0xF1, 0x7C, 0xC0, 0x7C, 0xBB,
    0x1F, 0x20, 0x1C, 0x1B, 0x1E, 0x23, 0x1A, 0x21, 0x1D, 0x22, 0x22, 0xFC, 0xE4, 0x66, 0xDA, 0x61,
    0x0B, 0x03, 0x0F, 0x13, 0x0C, 0x00, 0x01, 0x0A, 0x16, 0x14, 0x07, 0x02, 0x11, 0x06, 0x09, 0x04,
    0x12, 0x10, 0x15, 0x0D, 0x0B, 0x19, 0x08, 0x0E, 0x18, 0x05, 0x17, 0x00};
static const char id_to_char[36] = {
    0x45, 0x46, 0x4b, 0x41, 0x4f, 0x59, 0x4d, 0x4a, 0x56, 0x4e, 0x47, 0x54, 0x44, 0x53, 0x57, 0x42,
    0x51, 0x4c, 0x50, 0x43, 0x49, 0x52, 0x48, 0x5a, 0x58, 0x55, 0x36, 0x33, 0x32, 0x38, 0x34, 0x30,
    0x31, 0x37, 0x39, 0x35};

/* Contents-auth file encryption key (Project Crystal's lobby, Utils.DecryptAuthPassword). */
#ifdef _MSC_VER
#define CONTENTS_AUTH_KEY 0x0A9E3EBAB72483CBui64
#else
#define CONTENTS_AUTH_KEY 0x0A9E3EBAB72483CBULL
#endif

static pol_u32 be32(const unsigned char *p)
{
    return ((pol_u32)p[0] << 24) | ((pol_u32)p[1] << 16) | ((pol_u32)p[2] << 8) | p[3];
}

static pol_u32 le32(const unsigned char *p)
{
    return ((pol_u32)p[3] << 24) | ((pol_u32)p[2] << 16) | ((pol_u32)p[1] << 8) | p[0];
}

static pol_u64 le64(const unsigned char *p)
{
    return ((pol_u64)le32(p + 4) << 32) | le32(p);
}

static void put_le64(unsigned char *p, pol_u64 v)
{
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (unsigned char)(v >> (8 * i));
    }
}

/* ---- Blowfish (PlayOnline's key schedule) ---- */

static pol_u32 bf_f(const POL_CIPHER *c, pol_u32 x)
{
    return (((c->s[0][x >> 24] + c->s[1][(x >> 16) & 0xFF]) ^ c->s[2][(x >> 8) & 0xFF]) + c->s[3][x & 0xFF]);
}

static void bf_encipher(const POL_CIPHER *c, pol_u32 *xl, pol_u32 *xr)
{
    pol_u32 l = *xl, r = *xr, t;
    int i;
    for (i = 0; i < 16; i++) {
        l ^= c->p[i];
        r ^= bf_f(c, l);
        t = l;
        l = r;
        r = t;
    }
    t = l;
    l = r;
    r = t;
    r ^= c->p[16];
    l ^= c->p[17];
    *xl = l;
    *xr = r;
}

/* The tables come from a 0x1000-byte buffer: the key, then MD5 of everything so far, 16 bytes at a time; the
   first MD5 from the standard state, the rest from zero (MD5Hash.DoFinal(..., doSqBug: true)). */
void pol_cipher_init(POL_CIPHER *c, const unsigned char key[8], pol_u32 rkey1, pol_u32 rkey2)
{
    static unsigned char hb[0x1000];
    static const unsigned long zero_iv[4] = {0, 0, 0, 0};
    unsigned char digest[16];
    int cur = 8, i, j, n;
    pol_u32 l, r;
    memset(hb, 0, sizeof(hb));
    memcpy(hb, key, 8);
    while (cur < 0x1000) {
        md5_iv(hb, cur, digest, cur == 8 ? 0 : zero_iv);
        n = 0x1000 - cur < 16 ? 0x1000 - cur : 16;
        memcpy(hb + cur, digest, n);
        cur += 16;
    }
    for (i = 0; i < 18; i++) {
        c->p[i] = be32(hb + i);
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 256; j++) {
            c->s[i][j] = be32(hb + 4 * (i * 256 + j));
        }
    }
    /* Key mixing over the buffer's first 72 bytes (Blowfish.InitializeBlowfish with key = the whole buffer). */
    for (i = 0; i < 18; i++) {
        c->p[i] ^= be32(hb + 4 * i);
    }
    for (i = 0; i < 18; i += 2) {
        l = r = 0;
        bf_encipher(c, &l, &r);
        c->p[i] = l;
        c->p[i + 1] = r;
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 256; j += 2) {
            l = r = 0;
            bf_encipher(c, &l, &r);
            c->s[i][j] = l;
            c->s[i][j + 1] = r;
        }
    }
    memcpy(c->key, key, 8);
    c->rkey1 = rkey1;
    c->rkey2 = rkey2;
    c->xl = rkey1;
    c->xr = rkey2;
    c->pos = 0;
}

void pol_crypt64(POL_CIPHER *c, unsigned char *buf, int size, int restart)
{
    pol_u32 a = restart ? 0 : c->pos, xl = restart ? c->rkey1 : c->xl, xr = restart ? c->rkey2 : c->xr;
    int i;
    for (i = 0; i < size; i++) {
        unsigned char in = buf[i], out;
        pol_u64 ks;
        a &= 7;
        if (a == 0) {
            bf_encipher(c, &xl, &xr);
        }
        ks = (((pol_u64)xr << 32) | xl) >> (a * 8);
        out = (unsigned char)(ks ^ in);
        if (in != 0x0D && in != 0x0A && out != 0x0D && out != 0x0A) {
            buf[i] = out;
        }
        a++;
    }
    c->xl = xl;
    c->xr = xr;
    c->pos = a;
}

/* ---- base32 / base64 ---- */

int pol_base32_encode(const unsigned char *in, int size, char *out)
{
    int i = 0, o = 0;
    unsigned char t[5];
    while (i < size) {
        int k, left = size - i < 5 ? size - i : 5;
        memset(t, 0, 5);
        for (k = 0; k < left; k++) {
            t[k] = in[i + k];
        }
        out[o++] = b32_encode[t[0] >> 3];
        out[o++] = b32_encode[(t[0] & 7) << 2 | (t[1] >> 6)];
        out[o++] = b32_encode[(t[1] >> 1) & 0x1f];
        out[o++] = b32_encode[(t[1] & 1) << 4 | (t[2] >> 4)];
        out[o++] = b32_encode[(t[2] & 0xf) << 1 | (t[3] >> 7)];
        out[o++] = b32_encode[(t[3] >> 2) & 0x1f];
        out[o++] = b32_encode[(t[3] & 3) << 3 | (t[4] >> 5)];
        out[o++] = b32_encode[t[4] & 0x1f];
        i += 5;
    }
    out[o] = '\0';
    return o;
}

static unsigned char b32_value(char ch)
{
    unsigned char u = (unsigned char)ch;
    return u < sizeof(b32_decode) ? b32_decode[u] : 0xff;
}

int pol_base32_decode(const char *in, int len, unsigned char *out)
{
    int i, o = 0;
    for (i = 0; i + 8 <= len; i += 8) {
        unsigned char c0 = b32_value(in[i]), c1 = b32_value(in[i + 1]), c2 = b32_value(in[i + 2]),
                      c3 = b32_value(in[i + 3]), c4 = b32_value(in[i + 4]), c5 = b32_value(in[i + 5]),
                      c6 = b32_value(in[i + 6]), c7 = b32_value(in[i + 7]);
        out[o + 0] = (unsigned char)(c0 << 3 | c1 >> 2);
        out[o + 1] = (unsigned char)((c2 | c1 << 5) << 1 | c3 >> 4);
        out[o + 2] = (unsigned char)(c4 >> 1 | c3 << 4);
        out[o + 3] = (unsigned char)((c5 | c4 << 5) << 2 | c6 >> 3);
        out[o + 4] = (unsigned char)(c7 | c6 << 5);
        o += 5;
    }
    return o;
}

/* Full 3-byte groups, then the leftover bytes zero-filled; with `padding` the characters that carry no data
   are dropped (SqCrypto.EncodeBase64). */
int pol_base64_encode(const unsigned char *in, int size, int padding, char *out)
{
    int i = 0, o = 0;
    while (i + 3 <= size) {
        pol_u32 v = (pol_u32)in[i] << 16 | (pol_u32)in[i + 1] << 8 | in[i + 2];
        out[o++] = b64_encode[v >> 18 & 0x3F];
        out[o++] = b64_encode[v >> 12 & 0x3F];
        out[o++] = b64_encode[v >> 6 & 0x3F];
        out[o++] = b64_encode[v & 0x3F];
        i += 3;
    }
    if (i < size) {
        int left = size - i, k;
        pol_u32 v = 0;
        for (k = 0; k < 3; k++) {
            v = v << 8 | (k < left ? in[i + k] : 0);
        }
        out[o++] = b64_encode[v >> 18 & 0x3F];
        out[o++] = b64_encode[v >> 12 & 0x3F];
        out[o++] = b64_encode[v >> 6 & 0x3F];
        out[o++] = b64_encode[v & 0x3F];
        if (padding) {
            o -= left == 1 ? 2 : 1;
        }
    }
    out[o] = '\0';
    return o;
}

static unsigned char b64_value(char ch)
{
    unsigned char u = (unsigned char)ch;
    return u < sizeof(b64_decode) ? b64_decode[u] : 0;
}

/* 4-character groups; a short last group is filled with 'T' (value 0), as SqCrypto.DecodeBase64 does. */
int pol_base64_decode(const char *in, int len, unsigned char *out)
{
    int i, o = 0;
    for (i = 0; i < len; i += 4) {
        char g[4];
        int k;
        pol_u32 v;
        for (k = 0; k < 4; k++) {
            g[k] = i + k < len ? in[i + k] : 'T';
        }
        v = b64_value(g[3]) | (b64_value(g[2]) | (b64_value(g[1]) | (pol_u32)b64_value(g[0]) << 6) << 6) << 6;
        out[o++] = (unsigned char)(v >> 16);
        out[o++] = (unsigned char)(v >> 8);
        out[o++] = (unsigned char)v;
    }
    return o;
}

/* ---- IRC line checksum ---- */

void pol_checksum_str(const unsigned char *data, int size, char out[5])
{
    pol_u32 sum = 0, tail = 0;
    int i = 0;
    while (i <= size - 4) {
        sum += le32(data + i);
        i += 4;
    }
    while (i < size) {
        tail = tail >> 8 | (pol_u32)data[i++] << 24;
    }
    sum += tail;
    out[0] = (char)(((sum >> 26) & 0x3F) + 0x3F);
    out[1] = (char)(((sum >> 20) & 0x3F) + 0x3F);
    out[2] = (char)(((sum >> 14) & 0x3F) + 0x3F);
    out[3] = (char)(((sum >> 8) & 0x3F) + 0x3F);
    out[4] = '\0';
}

/* ---- POL IDs ---- */

/* 8 ID characters <-> the 42-bit number they stand for (SqCrypto.PolProDataToPolId / PolIdToPolProData). */
static pol_u64 id_value(const char *chars)
{
    pol_u64 id = 0;
    int i;
    for (i = 0; i < 8; i++) {
        unsigned char ch = (unsigned char)chars[i];
        id = id * 0x24 + (ch < sizeof(char_to_id) ? char_to_id[ch] : 0);
    }
    return id;
}

static void id_chars(pol_u64 value, char *out)
{
    pol_u64 last;
    int i;
    value &= ((pol_u64)1 << 42) - 1;
    for (i = 7; i >= 0; i--) {
        last = value;
        value /= 0x24;
        out[i] = id_to_char[(int)(last - value * 0x24)];
    }
}

void pol_scramble_id(const char *pol_id, char out[10])
{
    pol_u64 id = id_value(pol_id) | (pol_u64)0xC0000000 << 16;
    int i;
    for (i = 4; i >= 0; i--) {
        pol_u64 shifter = (pol_u64)0xFF << (i * 8);
        id ^= (id >> 8) & shifter;
    }
    out[0] = 'U';
    id_chars(id, out + 1);
    out[9] = '\0';
}

void pol_unscramble_id(const char *nick, char out[9])
{
    pol_u64 id = id_value(nick + 1) | (pol_u64)0xC0000000 << 16, shifter = 0xFF00;
    int i;
    for (i = 0; i < 5; i++) {
        id ^= (id & shifter) >> 8;
        shifter <<= 8;
    }
    id_chars(id, out);
    out[8] = '\0';
}

void pol_password_md5(const char *init_base32, int salt_chars, const char *password, char out[33])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char buf[256], digest[16];
    /* The salt is the server's InitData base32 (its line checksum already removed): all of it for the PS2-era
       PlayOnline Project Crystal implements, the first 40 characters (25 bytes) for 20110726_A (polirc.h,
       pol_versions). */
    int a = (int)strlen(init_base32), b = (int)strlen(password), i;
    if (salt_chars > 0 && a > salt_chars) {
        a = salt_chars;
    }
    if (a > (int)sizeof(buf)) {
        a = (int)sizeof(buf);
    }
    if (a + b > (int)sizeof(buf)) {
        b = (int)sizeof(buf) - a;
    }
    memcpy(buf, init_base32, a);
    memcpy(buf + a, password, b);
    md5(buf, a + b, digest);
    for (i = 0; i < 16; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[32] = '\0';
}

/* ---- Contents authentication ---- */

static const unsigned char file_encode[256] = {
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87};

typedef struct {
    pol_u64 sbox[32];
    pol_u32 done, sum;
} FILE_CRYPT;

static void file_init(FILE_CRYPT *f, pol_u64 seed)
{
    pol_u32 hi = (pol_u32)(seed >> 32), lo = (pol_u32)seed;
    unsigned char b[8];
    int i;
    lo = lo << 8 | lo >> 24;
    hi = hi >> 16 | hi << 16;
    put_le64(b, (pol_u64)lo << 32 | hi);
    b[0] = (unsigned char)(b[0] + 0x45);
    for (i = 1; i < 8; i++) {
        unsigned char t = (unsigned char)(b[i] + b[i - 1] - 0x2c);
        b[i] = (unsigned char)((unsigned char)(b[i - 1] << 2) ^ t ^ 0x45);
    }
    f->sbox[0] = le64(b);
    for (i = 1; i < 32; i++) {
        f->sbox[i] = f->sbox[i - 1] * 5;
    }
    f->done = f->sum = 0;
}

static void file_block(FILE_CRYPT *f, const unsigned char *src, unsigned char *dst)
{
    pol_u64 data = le64(src), shifted, box = f->sbox[(f->done >> 3) & 0x1F];
    unsigned char x1 = (unsigned char)((f->done >> 3) ^ 0x45), tmp[8];
    int i, j;
    f->sum += (pol_u32)(src[0] + src[4]);
    data = data >> 32 | data << 32;
    shifted = ((pol_u64)f->done << 10) | f->done;
    shifted = (shifted << 10) | f->done;
    shifted = (shifted << 10) | f->done;
    shifted += 0xA1652347;
    data ^= box;
    data ^= shifted;
    data += box;
    put_le64(tmp, data);
    for (i = 0; i < 8; i++) {
        unsigned char x2 = tmp[i];
        for (j = 0; j < 8; j++) {
            x2 = file_encode[(unsigned char)((unsigned char)(box >> (j * 8)) + x2)];
        }
        x1 = (unsigned char)(x1 ^ x2);
        dst[i] = x1;
    }
    f->done += 8;
}

/* SqFileEncryption.Encrypt of `size` (a multiple of 8) bytes, then the size-and-checksum block after them. */
static void file_encrypt(unsigned char *buf, int size, pol_u64 seed)
{
    FILE_CRYPT f;
    unsigned char tail[8];
    int i;
    file_init(&f, seed);
    for (i = 0; i < size; i += 8) {
        file_block(&f, buf + i, buf + i);
    }
    put_le64(tail, (pol_u64)f.sum << 32 | f.done);
    file_block(&f, tail, buf + size);
}

void pol_contents_auth(const unsigned char random[16], pol_u32 login_time, const unsigned char blowkey[8],
                       pol_u32 client_ip_be, unsigned short client_port, unsigned char out[0x40])
{
    unsigned char work[0x1C], auth[0x48];
    int i;
    memcpy(work, random, 16);
    for (i = 0; i < 4; i++) {
        work[0x10 + i] = (unsigned char)(login_time >> (8 * i));
    }
    memcpy(work + 0x14, blowkey, 8);
    memset(auth, 0, sizeof(auth));
    md5(work, sizeof(work), auth + 0x18);
    auth[4 + 2] = (unsigned char)client_port;
    auth[4 + 3] = (unsigned char)(client_port >> 8);
    auth[4 + 4] = (unsigned char)(client_ip_be >> 24);
    auth[4 + 5] = (unsigned char)(client_ip_be >> 16);
    auth[4 + 6] = (unsigned char)(client_ip_be >> 8);
    auth[4 + 7] = (unsigned char)client_ip_be;
    auth[0] = (unsigned char)(auth[37] ^ auth[27]);
    auth[1] = 0;
    file_encrypt(auth, 0x28, CONTENTS_AUTH_KEY);
    memmove(auth + 4, auth, 0x30);
    for (i = 5; i < 0x34; i++) {
        auth[i] ^= auth[i - 1];
    }
    auth[0] = auth[14];
    auth[1] = (unsigned char)(auth[12] ^ auth[6]);
    auth[2] = (unsigned char)(auth[10] ^ auth[8]);
    auth[3] = (unsigned char)(auth[13] ^ auth[9]);
    memcpy(out, auth, 0x40);
}

/* ---- RSA (fixed 256-bit key) ---- */

/* 16-bit limbs, least significant first, one spare limb for carries. */
#define LIMBS 17
typedef unsigned long BIG[LIMBS];

static const unsigned char rsa_n[32] = {
    0x8b, 0xc7, 0x75, 0x93, 0xe0, 0xfc, 0x9f, 0x7b, 0x03, 0x4b, 0x99, 0x01, 0xe9, 0x69, 0xcd, 0x9c,
    0xf9, 0x5d, 0x82, 0x20, 0xf6, 0x30, 0x66, 0xfd, 0x86, 0x56, 0x2a, 0x1f, 0x9b, 0x8e, 0xab, 0xe3};
static const unsigned char rsa_d[32] = {
    0x66, 0xf3, 0xde, 0x28, 0x39, 0xd4, 0xaa, 0x16, 0xd7, 0x43, 0x2c, 0x43, 0xd9, 0xbb, 0x1b, 0xff,
    0x88, 0xbe, 0xbd, 0xbb, 0x67, 0xae, 0xc6, 0x4c, 0x9b, 0x8f, 0x3d, 0x34, 0x47, 0x9d, 0x80, 0x77};

static void big_from_be(BIG x, const unsigned char *b)
{
    int i;
    for (i = 0; i < 16; i++) {
        x[i] = (unsigned long)b[31 - 2 * i] | (unsigned long)b[30 - 2 * i] << 8;
    }
    x[16] = 0;
}

static int big_cmp(const BIG a, const BIG b)
{
    int i;
    for (i = LIMBS - 1; i >= 0; i--) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

static void big_sub(BIG a, const BIG b)
{
    unsigned long borrow = 0;
    int i;
    for (i = 0; i < LIMBS; i++) {
        unsigned long t = a[i] - b[i] - borrow;
        borrow = (t >> 16) & 1;
        a[i] = t & 0xFFFF;
    }
}

/* a = (a + b) mod n, with a, b < n. */
static void big_addmod(BIG a, const BIG b, const BIG n)
{
    unsigned long carry = 0;
    int i;
    for (i = 0; i < LIMBS; i++) {
        unsigned long t = a[i] + b[i] + carry;
        a[i] = t & 0xFFFF;
        carry = t >> 16;
    }
    if (big_cmp(a, n) >= 0) {
        big_sub(a, n);
    }
}

/* r = a * b mod n by doubling (b's bits from the top). */
static void big_mulmod(BIG r, const BIG a, const BIG b, const BIG n)
{
    BIG acc;
    int bit;
    memset(acc, 0, sizeof(acc));
    for (bit = 255; bit >= 0; bit--) {
        big_addmod(acc, acc, n);
        if ((b[bit / 16] >> (bit % 16)) & 1) {
            big_addmod(acc, a, n);
        }
    }
    memcpy(r, acc, sizeof(acc));
}

void pol_rsa_modulus_le(unsigned char out[32])
{
    int i;
    for (i = 0; i < 32; i++) {
        out[i] = rsa_n[31 - i];
    }
}

void pol_rsa_session_key(const unsigned char in[32], unsigned char out[8])
{
    BIG n, d, c, m;
    unsigned char block[32];
    int bit, i;
    big_from_be(n, rsa_n);
    big_from_be(d, rsa_d);
    big_from_be(c, in);
    memset(m, 0, sizeof(m));
    m[0] = 1;
    for (bit = 255; bit >= 0; bit--) {
        big_mulmod(m, m, m, n);
        if ((d[bit / 16] >> (bit % 16)) & 1) {
            big_mulmod(m, m, c, n);
        }
    }
    for (i = 0; i < 16; i++) {
        block[31 - 2 * i] = (unsigned char)m[i];
        block[30 - 2 * i] = (unsigned char)(m[i] >> 8);
    }
    /* PolCore keeps the low 8 bytes of the result and checks no padding. Crystal sends a PKCS#1 v1.5 block
       whose message is those 8 bytes; OpenLobby sends an all-zero token, so the key is 8 zero bytes. */
    memcpy(out, block + 24, 8);
}
