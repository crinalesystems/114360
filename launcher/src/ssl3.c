/* SSL 3.0 client (ssl3.h). RFC 6101: ClientHello offering SSL_RSA_WITH_RC4_128_MD5 (0x0004), the server's
   Certificate (its RSA key, taken from the first certificate without checking it) and ServerHelloDone, then
   ClientKeyExchange (the 48-byte pre-master secret, PKCS #1 v1.5 type 2, RSA-encrypted with no length prefix),
   ChangeCipherSpec and Finished. Records after that are RC4 with an SSL 3.0 MD5 MAC; incoming MACs are not
   checked. */
#include <string.h>
#include "ssl3.h"
#include "md5.h"
#include "sha1.h"

#ifdef _MSC_VER
typedef unsigned __int64 u64;
#else
typedef unsigned long long u64;
#endif
typedef unsigned int u32;

#define HANDSHAKE_MAX 0x4000
#define RECORD_MAX (0x4000 + 2048)
#define BN_LIMBS 128                    /* RSA keys up to 4096 bits */

/* ---- Randomness: MD5 over a seed pool and a counter (enough for a key that protects a sign-up form) -------- */

static unsigned char pool[16];
static u32 pool_counter;

void ssl3_seed(const void *data, int len)
{
    unsigned char buf[16 + 256];
    int n = len < 256 ? len : 256;
    memcpy(buf, pool, 16);
    memcpy(buf + 16, data, n);
    md5(buf, 16 + n, pool);
}

static void random_bytes(unsigned char *out, int len, int nonzero)
{
    unsigned char buf[20], digest[16];
    int i = 0, k;
    while (i < len) {
        memcpy(buf, pool, 16);
        pool_counter++;
        buf[16] = (unsigned char)pool_counter;
        buf[17] = (unsigned char)(pool_counter >> 8);
        buf[18] = (unsigned char)(pool_counter >> 16);
        buf[19] = (unsigned char)(pool_counter >> 24);
        md5(buf, 20, digest);
        for (k = 0; k < 16 && i < len; k++) {
            if (!nonzero || digest[k] != 0) {
                out[i++] = digest[k];
            }
        }
    }
}

/* ---- RSA public operation: Montgomery modular exponentiation, 32-bit limbs, least significant first ---------- */

static void bn_from_bytes(u32 *out, int limbs, const unsigned char *be, int len)
{
    int i;
    memset(out, 0, limbs * sizeof(u32));
    for (i = 0; i < len && i / 4 < limbs; i++) {
        out[i / 4] |= (u32)be[len - 1 - i] << (8 * (i % 4));
    }
}

static void bn_to_bytes(unsigned char *be, int len, const u32 *in)
{
    int i;
    for (i = 0; i < len; i++) {
        be[len - 1 - i] = (unsigned char)(in[i / 4] >> (8 * (i % 4)));
    }
}

static int bn_ge(const u32 *a, const u32 *b, int k)
{
    int i;
    for (i = k - 1; i >= 0; i--) {
        if (a[i] != b[i]) {
            return a[i] > b[i];
        }
    }
    return 1;
}

static void bn_sub(u32 *a, const u32 *b, int k)
{
    u64 borrow = 0;
    int i;
    for (i = 0; i < k; i++) {
        u64 d = (u64)a[i] - b[i] - borrow;
        a[i] = (u32)d;
        borrow = (d >> 32) & 1;
    }
}

/* out = a * b / R mod n (CIOS). */
static void mont_mul(u32 *out, const u32 *a, const u32 *b, const u32 *n, u32 n0inv, int k)
{
    static u32 t[BN_LIMBS + 2];
    int i, j;
    u64 s;
    u32 c, m;
    memset(t, 0, (k + 2) * sizeof(u32));
    for (i = 0; i < k; i++) {
        c = 0;
        for (j = 0; j < k; j++) {
            s = (u64)t[j] + (u64)a[j] * b[i] + c;
            t[j] = (u32)s;
            c = (u32)(s >> 32);
        }
        s = (u64)t[k] + c;
        t[k] = (u32)s;
        t[k + 1] = (u32)(s >> 32);
        m = t[0] * n0inv;
        s = (u64)t[0] + (u64)m * n[0];
        c = (u32)(s >> 32);
        for (j = 1; j < k; j++) {
            s = (u64)t[j] + (u64)m * n[j] + c;
            t[j - 1] = (u32)s;
            c = (u32)(s >> 32);
        }
        s = (u64)t[k] + c;
        t[k - 1] = (u32)s;
        t[k] = t[k + 1] + (u32)(s >> 32);
    }
    if (t[k] != 0 || bn_ge(t, n, k)) {
        bn_sub(t, n, k);
    }
    memcpy(out, t, k * sizeof(u32));
}

/* out = in ^ e mod n, all big-endian bytes of the modulus' length. */
static int rsa_public(unsigned char *out, const unsigned char *in, const unsigned char *mod, int mod_len,
                      const unsigned char *exp, int exp_len)
{
    static u32 n[BN_LIMBS], r2[BN_LIMBS], x[BN_LIMBS], b[BN_LIMBS], one[BN_LIMBS];
    int k = (mod_len + 3) / 4, i, bit;
    u32 inv;
    if (k > BN_LIMBS || mod_len < 64) {
        return 0;
    }
    bn_from_bytes(n, k, mod, mod_len);
    if ((n[0] & 1) == 0) {
        return 0;
    }
    inv = n[0];                                 /* n[0]^-1 mod 2^32 by Newton's method, then negated */
    for (i = 0; i < 5; i++) {
        inv *= 2 - n[0] * inv;
    }
    inv = (u32)0 - inv;
    memset(r2, 0, sizeof(r2));                  /* R^2 mod n, R = 2^(32k), by doubling 1 */
    r2[0] = 1;
    for (i = 0; i < 64 * k; i++) {
        u32 carry = r2[k - 1] >> 31;
        int j;
        for (j = k - 1; j > 0; j--) {
            r2[j] = r2[j] << 1 | r2[j - 1] >> 31;
        }
        r2[0] <<= 1;
        if (carry || bn_ge(r2, n, k)) {
            bn_sub(r2, n, k);
        }
    }
    bn_from_bytes(b, k, in, mod_len);
    mont_mul(b, b, r2, n, inv, k);              /* base in Montgomery form */
    memset(one, 0, sizeof(one));
    one[0] = 1;
    mont_mul(x, one, r2, n, inv, k);            /* 1 in Montgomery form */
    for (i = 0; i < exp_len; i++) {
        for (bit = 7; bit >= 0; bit--) {
            mont_mul(x, x, x, n, inv, k);
            if (exp[i] >> bit & 1) {
                mont_mul(x, x, b, n, inv, k);
            }
        }
    }
    mont_mul(x, x, one, n, inv, k);
    bn_to_bytes(out, mod_len, x);
    return 1;
}

/* ---- RC4 ------------------------------------------------------------------------------------------------ */

typedef struct {
    unsigned char s[256];
    unsigned char i, j;
} RC4;

static void rc4_init(RC4 *r, const unsigned char *key, int len)
{
    int i;
    unsigned char j = 0, t;
    for (i = 0; i < 256; i++) {
        r->s[i] = (unsigned char)i;
    }
    for (i = 0; i < 256; i++) {
        j = (unsigned char)(j + r->s[i] + key[i % len]);
        t = r->s[i];
        r->s[i] = r->s[j];
        r->s[j] = t;
    }
    r->i = r->j = 0;
}

static void rc4_crypt(RC4 *r, unsigned char *p, int len)
{
    int k;
    unsigned char t;
    for (k = 0; k < len; k++) {
        r->i++;
        r->j = (unsigned char)(r->j + r->s[r->i]);
        t = r->s[r->i];
        r->s[r->i] = r->s[r->j];
        r->s[r->j] = t;
        p[k] ^= r->s[(unsigned char)(r->s[r->i] + r->s[r->j])];
    }
}

/* ---- The connection ------------------------------------------------------------------------------------- */

static const POL_NET *net;
static long sock;
static int encrypting, decrypting, closed;
static RC4 rc4_out, rc4_in;
static unsigned char mac_out[16];
static u32 seq_out;
static unsigned char raw[RECORD_MAX + 5];   /* bytes off the socket, not yet a whole record */
static int raw_len;
static unsigned char data[RECORD_MAX];      /* the current record's plaintext */
static int data_len, data_pos;
static unsigned char handshake[HANDSHAKE_MAX];  /* every handshake message so far, for Finished */
static int handshake_len;

static const unsigned char pad1[48] = {
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36};
static const unsigned char pad2[48] = {
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
    0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c};

/* Send one record of `type`, encrypted and MACed once the cipher is on. */
static int send_record(int type, const unsigned char *payload, int len)
{
    static unsigned char rec[5 + RECORD_MAX], inner[48 + 8 + 1 + 2 + RECORD_MAX], outer[16 + 48 + 16];
    int n = len;
    rec[0] = (unsigned char)type;
    rec[1] = 3;
    rec[2] = 0;
    memcpy(rec + 5, payload, len);
    if (encrypting) {
        unsigned char digest[16];
        int p = 0;
        memcpy(inner, mac_out, 16);
        p = 16;
        memcpy(inner + p, pad1, 48);
        p += 48;
        memset(inner + p, 0, 4);                /* sequence number, 64 bits big-endian */
        inner[p + 4] = (unsigned char)(seq_out >> 24);
        inner[p + 5] = (unsigned char)(seq_out >> 16);
        inner[p + 6] = (unsigned char)(seq_out >> 8);
        inner[p + 7] = (unsigned char)seq_out;
        p += 8;
        inner[p++] = (unsigned char)type;
        inner[p++] = (unsigned char)(len >> 8);
        inner[p++] = (unsigned char)len;
        memcpy(inner + p, payload, len);
        md5(inner, p + len, digest);
        memcpy(outer, mac_out, 16);
        memcpy(outer + 16, pad2, 48);
        memcpy(outer + 64, digest, 16);
        md5(outer, 80, rec + 5 + len);
        n = len + 16;
        rc4_crypt(&rc4_out, rec + 5, n);
        seq_out++;
    }
    rec[3] = (unsigned char)(n >> 8);
    rec[4] = (unsigned char)n;
    return net->send(sock, rec, 5 + n) == 5 + n;
}

/* The next record into data[] (decrypted, MAC dropped). Returns its type, 0 on timeout, -1 when closed. */
static int read_record(int timeout_ms)
{
    int need, r, n;
    for (;;) {
        if (raw_len >= 5) {
            n = raw[3] << 8 | raw[4];
            if (n > RECORD_MAX) {
                return -1;
            }
            if (raw_len >= 5 + n) {
                int type = raw[0];
                memcpy(data, raw + 5, n);
                memmove(raw, raw + 5 + n, raw_len - 5 - n);
                raw_len -= 5 + n;
                if (decrypting) {
                    rc4_crypt(&rc4_in, data, n);
                    n = n >= 16 ? n - 16 : 0;
                }
                data_len = n;
                data_pos = 0;
                return type;
            }
        }
        need = (int)sizeof(raw) - raw_len;
        r = net->recv(sock, raw + raw_len, need, timeout_ms);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        raw_len += r;
    }
}

/* Handshake messages arrive in handshake records, possibly several to a record or one across records. Returns
   the next message's type with its body in `out`, or -1. */
static int next_handshake(unsigned char *out, int max, int *out_len)
{
    static unsigned char hs[HANDSHAKE_MAX];
    static int hs_len;
    int type, n;
    for (;;) {
        if (hs_len >= 4) {
            n = hs[1] << 16 | hs[2] << 8 | hs[3];
            if (n > max || 4 + n > (int)sizeof(hs)) {
                return -1;
            }
            if (hs_len >= 4 + n) {
                type = hs[0];
                memcpy(out, hs + 4, n);
                *out_len = n;
                if (handshake_len + 4 + n <= HANDSHAKE_MAX) {
                    memcpy(handshake + handshake_len, hs, 4 + n);
                    handshake_len += 4 + n;
                }
                memmove(hs, hs + 4 + n, hs_len - 4 - n);
                hs_len -= 4 + n;
                return type;
            }
        }
        type = read_record(8000);
        if (type != 22 || hs_len + data_len > (int)sizeof(hs)) {
            hs_len = 0;
            return -1;
        }
        memcpy(hs + hs_len, data, data_len);
        hs_len += data_len;
    }
}

static int send_handshake(int type, const unsigned char *body, int len)
{
    static unsigned char msg[4 + 1024];
    msg[0] = (unsigned char)type;
    msg[1] = (unsigned char)(len >> 16);
    msg[2] = (unsigned char)(len >> 8);
    msg[3] = (unsigned char)len;
    memcpy(msg + 4, body, len);
    if (handshake_len + 4 + len <= HANDSHAKE_MAX) {
        memcpy(handshake + handshake_len, msg, 4 + len);
        handshake_len += 4 + len;
    }
    return send_record(22, msg, 4 + len);
}

/* A DER length at *p; advances *p. */
static int der_length(const unsigned char **p, const unsigned char *end)
{
    int len, k;
    if (*p >= end) {
        return -1;
    }
    len = *(*p)++;
    if (len & 0x80) {
        k = len & 0x7F;
        if (k > 3 || *p + k > end) {
            return -1;
        }
        for (len = 0; k > 0; k--) {
            len = len << 8 | *(*p)++;
        }
    }
    return *p + len <= end ? len : -1;
}

/* The RSA key of a DER certificate: after the rsaEncryption OID, a BIT STRING holding
   SEQUENCE { INTEGER n, INTEGER e }. */
static int cert_rsa_key(const unsigned char *cert, int len, const unsigned char **mod, int *mod_len,
                        const unsigned char **exp, int *exp_len)
{
    static const unsigned char oid[] = {0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
    const unsigned char *end = cert + len, *p;
    int n;
    for (p = cert; p + sizeof(oid) <= end && memcmp(p, oid, sizeof(oid)) != 0; p++) {
    }
    if (p + sizeof(oid) > end) {
        return 0;
    }
    p += sizeof(oid);
    if (p + 2 <= end && p[0] == 0x05 && p[1] == 0x00) {        /* NULL parameters */
        p += 2;
    }
    if (p >= end || *p++ != 0x03 || der_length(&p, end) < 0 || p >= end || *p++ != 0x00) {
        return 0;
    }
    if (p >= end || *p++ != 0x30 || der_length(&p, end) < 0) {
        return 0;
    }
    if (p >= end || *p++ != 0x02 || (n = der_length(&p, end)) <= 0) {
        return 0;
    }
    while (n > 1 && *p == 0) {                  /* the INTEGER's sign byte */
        p++;
        n--;
    }
    *mod = p;
    *mod_len = n;
    p += n;
    if (p >= end || *p++ != 0x02 || (n = der_length(&p, end)) <= 0) {
        return 0;
    }
    *exp = p;
    *exp_len = n;
    return 1;
}

static void fail_with(char *error, int size, const char *why)
{
    int n = (int)strlen(why) < size - 1 ? (int)strlen(why) : size - 1;
    memcpy(error, why, n);
    error[n] = '\0';
}

/* SSL 3.0 Finished hashes over the handshake so far for `sender` ("CLNT" or "SRVR"). */
static void finished_hash(const unsigned char *master, const char *sender, unsigned char out[36])
{
    static unsigned char buf[HANDSHAKE_MAX + 4 + 48 + 48];
    unsigned char inner[20], outer[48 + 48 + 20];
    int n = handshake_len;
    memcpy(buf, handshake, n);
    memcpy(buf + n, sender, 4);
    memcpy(buf + n + 4, master, 48);
    memcpy(buf + n + 52, pad1, 48);
    md5(buf, n + 100, inner);
    memcpy(outer, master, 48);
    memcpy(outer + 48, pad2, 48);
    memcpy(outer + 96, inner, 16);
    md5(outer, 112, out);
    memcpy(buf + n + 52, pad1, 40);
    sha1(buf, n + 92, inner);
    memcpy(outer + 48, pad2, 40);
    memcpy(outer + 88, inner, 20);
    sha1(outer, 108, out + 16);
}

/* SSL 3.0 key expansion: `count` MD5 blocks of MD5(secret + SHA1("A" | "BB" | ... + secret + r1 + r2)). */
static void expand(const unsigned char *secret, const unsigned char *r1, const unsigned char *r2, unsigned char *out,
                   int count)
{
    unsigned char buf[16 + 48 + 64], digest[20], outer[48 + 20];
    int i, k;
    for (i = 0; i < count; i++) {
        for (k = 0; k <= i; k++) {
            buf[k] = (unsigned char)('A' + i);
        }
        memcpy(buf + i + 1, secret, 48);
        memcpy(buf + i + 49, r1, 32);
        memcpy(buf + i + 81, r2, 32);
        sha1(buf, i + 113, digest);
        memcpy(outer, secret, 48);
        memcpy(outer + 48, digest, 20);
        md5(outer, 68, out + 16 * i);
    }
}

int ssl3_connect(const POL_NET *n, pol_u32 ip, unsigned short port, char *error, int error_size)
{
    static unsigned char msg[HANDSHAKE_MAX], hello[64], pre[48], block[512], keys[64], master[48];
    unsigned char client_random[32], server_random[32], verify[36], ccs = 1;
    const unsigned char *mod = NULL, *exp = NULL;
    int mod_len = 0, exp_len = 0, type, len, i, padding;
    net = n;
    encrypting = decrypting = closed = 0;
    seq_out = 0;
    raw_len = data_len = data_pos = handshake_len = 0;
    sock = net->connect(ip, port);
    if (sock <= 0) {
        fail_with(error, error_size, "cannot connect");
        return 0;
    }
    random_bytes(client_random, 32, 0);
    hello[0] = 3;                               /* ClientHello: version 3.0, random, no session, one suite */
    hello[1] = 0;
    memcpy(hello + 2, client_random, 32);
    hello[34] = 0;
    hello[35] = 0;
    hello[36] = 2;
    hello[37] = 0x00;
    hello[38] = 0x04;                           /* SSL_RSA_WITH_RC4_128_MD5 */
    hello[39] = 1;
    hello[40] = 0;                              /* null compression */
    if (!send_handshake(1, hello, 41)) {
        fail_with(error, error_size, "cannot send the hello");
        ssl3_close();
        return 0;
    }
    for (;;) {
        type = next_handshake(msg, sizeof(msg), &len);
        if (type == 2 && len >= 38) {           /* ServerHello */
            memcpy(server_random, msg + 2, 32);
            i = 34 + msg[34];
            if (msg[0] != 3 || msg[1] != 0 || i + 2 > len || msg[i + 1] != 0x00 || msg[i + 2] != 0x04) {
                fail_with(error, error_size, "the server did not choose SSL 3.0 with RC4-MD5");
                ssl3_close();
                return 0;
            }
        } else if (type == 11 && len > 6) {     /* Certificate: the chain's first certificate */
            int cert_len = msg[3] << 16 | msg[4] << 8 | msg[5];
            static unsigned char cert[HANDSHAKE_MAX];
            if (6 + cert_len > len) {
                fail_with(error, error_size, "bad certificate message");
                ssl3_close();
                return 0;
            }
            memcpy(cert, msg + 6, cert_len);
            if (!cert_rsa_key(cert, cert_len, &mod, &mod_len, &exp, &exp_len)) {
                fail_with(error, error_size, "no RSA key in the server's certificate");
                ssl3_close();
                return 0;
            }
        } else if (type == 14) {                /* ServerHelloDone */
            break;
        } else if (type == 13) {                /* CertificateRequest: answered with no certificate */
        } else {
            fail_with(error, error_size, "the server broke off the handshake");
            ssl3_close();
            return 0;
        }
    }
    if (mod == NULL || mod_len > (int)sizeof(block)) {
        fail_with(error, error_size, "the server sent no usable certificate");
        ssl3_close();
        return 0;
    }
    pre[0] = 3;                                 /* pre-master secret: version, 46 random bytes */
    pre[1] = 0;
    random_bytes(pre + 2, 46, 0);
    padding = mod_len - 3 - 48;                 /* PKCS #1 v1.5 type 2: 00 02 nonzero-random 00 secret */
    block[0] = 0;
    block[1] = 2;
    random_bytes(block + 2, padding, 1);
    block[2 + padding] = 0;
    memcpy(block + 3 + padding, pre, 48);
    if (!rsa_public(msg, block, mod, mod_len, exp, exp_len) || !send_handshake(16, msg, mod_len)) {
        fail_with(error, error_size, "cannot send the key exchange");
        ssl3_close();
        return 0;
    }
    expand(pre, client_random, server_random, master, 3);
    expand(master, server_random, client_random, keys, 4);  /* client MAC, server MAC, client key, server key */
    memcpy(mac_out, keys, 16);
    rc4_init(&rc4_out, keys + 32, 16);
    rc4_init(&rc4_in, keys + 48, 16);
    finished_hash(master, "CLNT", verify);
    if (!send_record(20, &ccs, 1)) {
        fail_with(error, error_size, "cannot change cipher");
        ssl3_close();
        return 0;
    }
    encrypting = 1;
    if (!send_handshake(20, verify, 36)) {
        fail_with(error, error_size, "cannot send Finished");
        ssl3_close();
        return 0;
    }
    /* The server's ChangeCipherSpec, then its (encrypted) Finished. */
    if (read_record(8000) != 20) {
        fail_with(error, error_size, "the server refused the key exchange");
        ssl3_close();
        return 0;
    }
    decrypting = 1;
    if (read_record(8000) != 22 || data_len < 4 || data[0] != 20) {
        fail_with(error, error_size, "no Finished from the server");
        ssl3_close();
        return 0;
    }
    data_len = data_pos = 0;
    return 1;
}

int ssl3_write(const void *buf, int len)
{
    const unsigned char *p = (const unsigned char *)buf;
    while (len > 0) {
        int n = len < 0x4000 ? len : 0x4000;
        if (!send_record(23, p, n)) {
            return 0;
        }
        p += n;
        len -= n;
    }
    return 1;
}

int ssl3_read(void *buf, int len, int timeout_ms)
{
    int type, n;
    while (data_pos >= data_len) {
        if (closed) {
            return -1;
        }
        type = read_record(timeout_ms);
        if (type == 0) {
            return 0;
        }
        if (type < 0 || type == 21) {           /* closed, or an alert (close_notify included) */
            closed = 1;
            return -1;
        }
        if (type != 23) {
            data_len = data_pos = 0;
        }
    }
    n = data_len - data_pos < len ? data_len - data_pos : len;
    memcpy(buf, data + data_pos, n);
    data_pos += n;
    return n;
}

void ssl3_close(void)
{
    if (sock > 0) {
        if (encrypting) {
            unsigned char alert[2] = {1, 0};    /* warning, close_notify */
            send_record(21, alert, 2);
        }
        net->close(sock);
    }
    sock = 0;
}
