// PlayOnline update server client (POLP, pt012.pol.com:54000) and the decoders its files need. The same protocol
// as tools/polfetch.py; see that file and docs/playonline-patch.md. Frames: u32 length, u32 checksum (first 4
// bytes of MD5 of the frame from byte 8, little-endian), "POLP", u32 command, body.
#define _DEFAULT_SOURCE   // getaddrinfo, strdup under -std=c23
#include "polp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md5.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define close_socket closesocket
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define close_socket close
#endif

#define HOST "pt012.pol.com"
#define PORT "54000"
#define PLATFORM "X2U"
#define PRODUCT "1000"
#define CHUNK 0x10000

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t get32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// One request on its own connection; the answer's body (malloc'd) in *body. Returns the answer's command or -1.
static int ask(const unsigned char *body_in, uint32_t len, uint32_t command, unsigned char **body, uint32_t *body_len)
{
    static int started;
    struct addrinfo hints = {0}, *ai = nullptr;
    unsigned char *pkt = malloc(16 + len), digest[16];
    unsigned char *buf = nullptr;
    uint32_t have = 0, cap = 0, total = 0;
    int result = -1;
    sock_t s = INVALID_SOCKET;
#ifdef _WIN32
    if (!started) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        started = 1;
    }
#else
    (void)started;
#endif
    put32(pkt, 16 + len);
    put32(pkt + 4, 0);
    memcpy(pkt + 8, "POLP", 4);
    put32(pkt + 12, command);
    memcpy(pkt + 16, body_in, len);
    md5(pkt + 8, 8 + len, digest);
    memcpy(pkt + 4, digest, 4);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(HOST, PORT, &hints, &ai) != 0 || !ai) {
        goto done;
    }
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == INVALID_SOCKET) {
        goto done;
    }
#ifdef _WIN32
    DWORD tv = 30000;
#else
    struct timeval tv = {30, 0};
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) != 0 || send(s, (const char *)pkt, (int)(16 + len), 0) < 0) {
        goto done;
    }
    for (;;) {
        if (have >= 16) {
            total = get32(buf);
            if (have >= total) {
                break;
            }
        }
        if (cap - have < CHUNK) {
            cap = cap ? cap * 2 : 4 * CHUNK;
            buf = realloc(buf, cap);
        }
        int got = recv(s, (char *)buf + have, (int)(cap - have), 0);
        if (got <= 0) {
            goto done;
        }
        have += (uint32_t)got;
    }
    if (memcmp(buf + 8, "POLP", 4) != 0 || total < 16) {
        goto done;
    }
    result = (int)get32(buf + 12);
    *body_len = total - 16;
    *body = malloc(*body_len + 1);
    memcpy(*body, buf + 16, *body_len);
done:
    if (s != INVALID_SOCKET) {
        close_socket(s);
    }
    if (ai) {
        freeaddrinfo(ai);
    }
    free(pkt);
    free(buf);
    return result;
}

// ─── Inflate (RFC 1951), enough for zlib streams: stored, fixed and dynamic blocks ─────────────────────────────

typedef struct {
    const unsigned char *in;
    size_t len, pos;
    uint32_t bits;
    int nbits;
    unsigned char *out;
    size_t olen, ocap;
    bool bad;
} inflater;

typedef struct {
    uint16_t count[16], symbol[288];
} huffman;

static int getbit(inflater *z, int n)
{
    while (z->nbits < n) {
        if (z->pos >= z->len) {
            z->bad = true;
            return 0;
        }
        z->bits |= (uint32_t)z->in[z->pos++] << z->nbits;
        z->nbits += 8;
    }
    int v = (int)(z->bits & ((1u << n) - 1));
    z->bits >>= n;
    z->nbits -= n;
    return v;
}

static void emit(inflater *z, unsigned char c)
{
    if (z->olen == z->ocap) {
        z->ocap = z->ocap ? z->ocap * 2 : 1 << 16;
        z->out = realloc(z->out, z->ocap);
    }
    z->out[z->olen++] = c;
}

static void build(huffman *h, const unsigned char *lengths, int n)
{
    uint16_t offs[16];
    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++) {
        offs[i + 1] = offs[i] + h->count[i];
    }
    for (int i = 0; i < n; i++) {
        if (lengths[i]) {
            h->symbol[offs[lengths[i]]++] = (uint16_t)i;
        }
    }
}

static int decode(inflater *z, const huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= getbit(z, 1);
        int count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
        if (z->bad) {
            return -1;
        }
    }
    z->bad = true;
    return -1;
}

static const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
                                   99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5,
                                   5, 0};
static const uint16_t DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DEXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                   12, 12, 13, 13};

static void codes(inflater *z, const huffman *lit, const huffman *dist)
{
    for (;;) {
        int sym = decode(z, lit);
        if (z->bad || sym == 256) {
            return;
        }
        if (sym < 256) {
            emit(z, (unsigned char)sym);
            continue;
        }
        sym -= 257;
        if (sym >= 29) {
            z->bad = true;
            return;
        }
        int len = LBASE[sym] + getbit(z, LEXTRA[sym]);
        int d = decode(z, dist);
        if (d < 0 || d >= 30) {
            z->bad = true;
            return;
        }
        size_t back = DBASE[d] + (size_t)getbit(z, DEXTRA[d]);
        if (back > z->olen) {
            z->bad = true;
            return;
        }
        while (len--) {
            emit(z, z->out[z->olen - back]);
        }
    }
}

// zlib stream (2-byte header, deflate, Adler-32 not checked: the caller checks the result's size or MD5).
static unsigned char *zlib_inflate(const unsigned char *in, size_t len, size_t *out_len)
{
    inflater z = {.in = in, .len = len, .pos = 2};
    int last;
    do {
        last = getbit(&z, 1);
        int type = getbit(&z, 2);
        if (type == 0) {
            z.bits = 0;
            z.nbits = 0;
            if (z.pos + 4 > z.len) {
                z.bad = true;
                break;
            }
            unsigned n = z.in[z.pos] | z.in[z.pos + 1] << 8;
            z.pos += 4;
            if (z.pos + n > z.len) {
                z.bad = true;
                break;
            }
            for (unsigned i = 0; i < n; i++) {
                emit(&z, z.in[z.pos++]);
            }
        } else if (type == 1) {
            static huffman lit, dist;
            static bool ready;
            if (!ready) {
                unsigned char l[288];
                for (int i = 0; i < 288; i++) {
                    l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
                }
                build(&lit, l, 288);
                memset(l, 5, 30);
                build(&dist, l, 30);
                ready = true;
            }
            codes(&z, &lit, &dist);
        } else if (type == 2) {
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            unsigned char lengths[320] = {0};
            huffman lit, dist, cl;
            int nlen = getbit(&z, 5) + 257, ndist = getbit(&z, 5) + 1, ncode = getbit(&z, 4) + 4;
            for (int i = 0; i < ncode; i++) {
                lengths[order[i]] = (unsigned char)getbit(&z, 3);
            }
            build(&cl, lengths, 19);
            memset(lengths, 0, sizeof(lengths));
            for (int i = 0; i < nlen + ndist && !z.bad;) {
                int sym = decode(&z, &cl);
                if (sym < 16) {
                    lengths[i++] = (unsigned char)sym;
                } else {
                    int rep = sym == 16 ? 3 + getbit(&z, 2) : sym == 17 ? 3 + getbit(&z, 3) : 11 + getbit(&z, 7);
                    unsigned char v = sym == 16 && i > 0 ? lengths[i - 1] : 0;
                    while (rep-- && i < nlen + ndist) {
                        lengths[i++] = v;
                    }
                }
            }
            build(&lit, lengths, nlen);
            build(&dist, lengths + nlen, ndist);
            codes(&z, &lit, &dist);
        } else {
            z.bad = true;
        }
    } while (!last && !z.bad);
    if (z.bad) {
        free(z.out);
        return nullptr;
    }
    *out_len = z.olen;
    return z.out;
}

// .slc container: 0x01 stored, 0x02 + u32 bit count + LSB-first LZSS (flag 0: 8-bit literal; 1: 16-bit distance,
// 8-bit length), 0x03 zlib.
static unsigned char *slc_decode(const unsigned char *blob, size_t len, size_t *out_len)
{
    if (len < 1) {
        return nullptr;
    }
    if (blob[0] == 1) {
        unsigned char *out = malloc(len);
        memcpy(out, blob + 1, len - 1);
        *out_len = len - 1;
        return out;
    }
    if (blob[0] == 3) {
        return zlib_inflate(blob + 1, len - 1, out_len);
    }
    if (blob[0] != 2 || len < 5) {
        return nullptr;
    }
    uint32_t end = get32(blob + 1);
    inflater z = {.in = blob + 5, .len = len - 5};
    uint32_t pos = 0;
    while (pos < end && !z.bad) {
        pos++;
        if (getbit(&z, 1) == 0) {
            emit(&z, (unsigned char)getbit(&z, 8));
            pos += 8;
        } else {
            size_t dist = (size_t)getbit(&z, 16);
            int n = getbit(&z, 8);
            pos += 24;
            if (dist < 1 || dist > z.olen) {
                break;
            }
            while (n--) {
                emit(&z, z.out[z.olen - dist]);
            }
        }
    }
    *out_len = z.olen;
    return z.out;
}

// ─── The file list and the files ──────────────────────────────────────────────────────────────────────────────

void polp_files_free(polp_file *files, int count)
{
    for (int i = 0; i < count; i++) {
        free(files[i].path);
        free(files[i].blob);
    }
    free(files);
}

int polp_catalogue(const char *cap_version, polp_file **out, char *error, size_t error_size)
{
    unsigned char *body = nullptr, *text;
    uint32_t body_len = 0;
    size_t text_len = 0;
    int command = ask((const unsigned char *)PLATFORM "\0" PRODUCT, 8, 1, &body, &body_len);
    if (command != 2) {
        snprintf(error, error_size, "PlayOnline's update server refused the file list (answer %d)", command);
        free(body);
        return -1;
    }
    text = slc_decode(body, body_len, &text_len);
    free(body);
    if (!text) {
        snprintf(error, error_size, "PlayOnline's file list could not be read");
        return -1;
    }
    // file <path> {\n<version> <size> <checksum> <token> <blob> <blob size> [...]\n}
    polp_file *files = nullptr;
    int count = 0, cap = 0;
    char *path = nullptr, best_version[16] = "";
    long best_size = 0, best_blob_size = 0;
    char best_blob[512] = "";
    text = realloc(text, text_len + 1);
    text[text_len] = 0;
    for (char *line = strtok((char *)text, "\n"); line; line = strtok(nullptr, "\n")) {
        if (strncmp(line, "file ", 5) == 0 && strchr(line, '{')) {
            free(path);
            path = strdup(line + 5);
            char *brace = strrchr(path, '{');
            *brace = 0;
            for (size_t n = strlen(path); n > 0 && path[n - 1] == ' '; n--) {
                path[n - 1] = 0;
            }
            best_version[0] = 0;
        } else if (line[0] == '}' && path) {
            if (best_version[0]) {
                if (count == cap) {
                    cap = cap ? cap * 2 : 256;
                    files = realloc(files, sizeof(*files) * cap);
                }
                files[count++] = (polp_file){strdup(path), strdup(best_blob), best_size, best_blob_size};
                SDL_strlcpy(files[count - 1].version, best_version, sizeof(files[count - 1].version));
            }
            free(path);
            path = nullptr;
        } else if (path) {
            char version[16], blob[512];
            long size, blob_size;
            if (sscanf(line, "%15s %ld %*s %*s %511s %ld", version, &size, blob, &blob_size) == 4
                && strcmp(version, cap_version) <= 0 && strcmp(version, best_version) > 0) {
                SDL_strlcpy(best_version, version, sizeof(best_version));
                SDL_strlcpy(best_blob, blob, sizeof(best_blob));
                best_size = size;
                best_blob_size = blob_size;
            }
        }
    }
    free(path);
    free(text);
    *out = files;
    return count;
}

unsigned char *polp_download(const polp_file *f, size_t *len, char *error, size_t error_size)
{
    size_t plen = strlen(f->blob) + 1, have = 0;
    unsigned char *req = malloc(24 + plen), *data = malloc((size_t)f->blob_size + 1);
    while (have < (size_t)f->blob_size) {
        unsigned char *body = nullptr;
        uint32_t body_len = 0, want = (uint32_t)((size_t)f->blob_size - have > CHUNK ? CHUNK : f->blob_size - have);
        put32(req, (uint32_t)have);
        put32(req + 4, want);
        memcpy(req + 8, PLATFORM "\0" PRODUCT, 8);
        put32(req + 16, (uint32_t)plen);
        memcpy(req + 20, f->blob, plen);
        int command = ask(req, (uint32_t)(20 + plen), 3, &body, &body_len);
        if (command != 4 || body_len < 12 || body_len < 12 + get32(body + 8)) {
            snprintf(error, error_size, "PlayOnline's update server refused %s (answer %d)", f->blob, command);
            free(body);
            free(req);
            free(data);
            return nullptr;
        }
        uint32_t skip = 12 + get32(body + 8), n = body_len - skip;
        if (n == 0 || have + n > (size_t)f->blob_size) {
            n = n == 0 ? 0 : (uint32_t)((size_t)f->blob_size - have);
        }
        if (n == 0) {
            snprintf(error, error_size, "no data for %s", f->blob);
            free(body);
            free(req);
            free(data);
            return nullptr;
        }
        memcpy(data + have, body + skip, n);
        have += n;
        free(body);
    }
    free(req);
    unsigned char *out = slc_decode(data, have, len);
    free(data);
    if (!out || *len != (size_t)f->size) {
        snprintf(error, error_size, "%s could not be decoded", f->path);
        free(out);
        return nullptr;
    }
    return out;
}
