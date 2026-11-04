#define _DEFAULT_SOURCE           /* fseeko/ftello under -std=c23 */
#define _FILE_OFFSET_BITS 64      /* packs are about 1 GiB; offsets stay 64-bit on 32-bit hosts too */
#include "simple.h"
#include "net.h"
#include "monocypher/monocypher.h"
#include "monocypher/monocypher-ed25519.h"
#include "md5.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <zlib.h>

/* Logging */
static simple_log_cb g_log = NULL;
static void *g_log_ud = NULL;

void simple_set_log(simple_log_cb log, void *ud)
{
    g_log = log;
    g_log_ud = ud;
}

static void log_msg(const char *fmt, ...)
{
    if (!g_log) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_log(g_log_ud, buf);
}

/* Error code to text */
const char *simple_code_text(int code)
{
    switch (code) {
        case SIMPLE_OK: return "OK";
        case SIMPLE_KEY_NOT_KEY: return "ZZ101";
        case SIMPLE_KEY_BAD_SIG: return "ZZ102";
        case SIMPLE_MANIFEST_DL: return "ZZ103";
        case SIMPLE_MANIFEST_HASH: return "ZZ104";
        case SIMPLE_MANIFEST_UNREADABLE: return "ZZ105";
        case SIMPLE_MANIFEST_FORMAT: return "ZZ106";
        case SIMPLE_NO_CONNECTION: return "ZZ201";
        case SIMPLE_DRIVE_REFUSED: return "ZZ202";
        case SIMPLE_PACK_HASH: return "ZZ203";
        case SIMPLE_PACK_NOT_FOUND: return "ZZ204";
        case SIMPLE_FILE_DIFFERS: return "ZZ501";
        case SIMPLE_FILE_DECOMP: return "ZZ502";
        default: return "ZZ999";
    }
}

/* Base64 decoding (standard and URL-safe) */
static unsigned char base64_decode_char(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return 255;
}

static bool base64_decode(const char *str, unsigned char *out, size_t out_size, size_t *out_len)
{
    size_t len = strlen(str);
    size_t pos = 0;
    unsigned int val = 0;
    int bits = 0;

    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (isspace((unsigned char)c)) continue;
        if (c == '=') break;

        unsigned char d = base64_decode_char(c);
        if (d == 255) return false;

        val = (val << 6) | d;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            if (pos >= out_size) return false;
            out[pos++] = (val >> bits) & 0xff;
        }
    }

    *out_len = pos;
    return true;
}

/* Hex to binary: the first out_size * 2 characters of str (a key value runs on into the next line) */
static bool hex_decode(const char *str, unsigned char *out, size_t out_size)
{
    if (strnlen(str, out_size * 2) != out_size * 2) return false;
    for (size_t i = 0; i < out_size; i++) {
        char h = str[i*2];
        char l = str[i*2+1];
        int hv = isdigit((unsigned char)h) ? h - '0' : tolower((unsigned char)h) - 'a' + 10;
        int lv = isdigit((unsigned char)l) ? l - '0' : tolower((unsigned char)l) - 'a' + 10;
        if (hv < 0 || hv > 15 || lv < 0 || lv > 15) return false;
        out[i] = (hv << 4) | lv;
    }
    return true;
}

/* SHA-512 (from Monocypher) */
static void sha512(const unsigned char *data, size_t size, unsigned char *digest)
{
    crypto_sha512_ctx ctx;
    crypto_sha512_init(&ctx);
    crypto_sha512_update(&ctx, data, size);
    crypto_sha512_final(&ctx, digest);
}

/* Key parsing */
int simple_key_parse(const char *input, simple_key *k)
{
    if (!input || !k) return SIMPLE_KEY_NOT_KEY;

    memset(k, 0, sizeof(*k));

    /* Check if it looks like base64 */
    bool is_base64 = false;
    for (const char *p = input; *p; p++) {
        if (base64_decode_char(*p) != 255 || isspace((unsigned char)*p) || *p == '=') {
            is_base64 = true;
            break;
        }
    }

    /* Try base64 decode first */
    unsigned char decoded[2048] = {0};
    size_t decoded_len = 0;
    const char *to_parse = input;

    if (is_base64 && base64_decode(input, decoded, sizeof(decoded), &decoded_len)) {
        to_parse = (const char *)decoded;
    }

    /* Parse the key lines */
    const char *p = to_parse;
    bool has_folder = false, has_manifest = false, has_sha512 = false, has_sig = false;
    unsigned char sig[64];
    const char *end_of_signed = NULL;

    while (*p) {
        /* Skip whitespace */
        while (*p && (*p == '\r' || *p == '\n' || *p == '\t' || *p == ' ')) p++;
        if (!*p) break;

        const char *line_start = p;
        while (*p && *p != '\n') p++;
        const char *line_end = p;
        if (*p == '\n') p++;

        /* Remove trailing \r */
        if (line_end > line_start && line_end[-1] == '\r') line_end--;

        if (strncmp(line_start, "114360-key", 10) == 0) {
            continue;
        } else if (strncmp(line_start, "folder", 6) == 0) {
            const char *val = line_start + 6;
            while (*val && isspace((unsigned char)*val)) val++;
            if (val < line_end) {
                size_t len = line_end - val;
                if (len >= sizeof(k->folder_id)) return SIMPLE_KEY_NOT_KEY;
                memcpy(k->folder_id, val, len);
                k->folder_id[len] = 0;
                has_folder = true;
            }
        } else if (strncmp(line_start, "manifest", 8) == 0) {
            const char *val = line_start + 8;
            while (*val && isspace((unsigned char)*val)) val++;
            if (val < line_end) {
                size_t len = line_end - val;
                if (len >= sizeof(k->manifest_id)) return SIMPLE_KEY_NOT_KEY;
                memcpy(k->manifest_id, val, len);
                k->manifest_id[len] = 0;
                has_manifest = true;
            }
        } else if (strncmp(line_start, "sha512", 6) == 0) {
            const char *val = line_start + 6;
            while (*val && isspace((unsigned char)*val)) val++;
            if (val < line_end) {
                size_t len = line_end - val;
                if (len != 128) return SIMPLE_KEY_NOT_KEY;
                if (!hex_decode(val, k->sha512, 64)) return SIMPLE_KEY_NOT_KEY;
                has_sha512 = true;
            }
            end_of_signed = p;
        } else if (strncmp(line_start, "sig", 3) == 0) {
            const char *val = line_start + 3;
            while (*val && isspace((unsigned char)*val)) val++;
            if (val < line_end) {
                size_t len = line_end - val;
                if (len != 128) return SIMPLE_KEY_BAD_SIG;
                if (!hex_decode(val, sig, 64)) return SIMPLE_KEY_BAD_SIG;
                has_sig = true;
            }
        }
    }

    if (!has_folder && !has_manifest) return SIMPLE_KEY_NOT_KEY;
    if (!has_sha512) return SIMPLE_KEY_NOT_KEY;

    k->signed_present = false;

    if (has_sig) {
        /* Verify signature of everything up to (and including) the sha512 line */
        if (!end_of_signed) return SIMPLE_KEY_BAD_SIG;

        /* Get public key from build-time define */
        unsigned char pub_key[32];
        if (!hex_decode(SIMPLE_PUBLIC_KEY_HEX, pub_key, 32)) {
            return SIMPLE_KEY_BAD_SIG;
        }

        /* Prepare the signed data: lines up to and including sha512, each with \n */
        unsigned char signed_data[2048];
        size_t signed_len = 0;
        const char *q = to_parse;

        while (q < end_of_signed && *q) {
            if (signed_len >= sizeof(signed_data) - 1) return SIMPLE_KEY_BAD_SIG;
            
            /* Copy character and append \n at end of line */
            while (q < end_of_signed && *q != '\n' && *q != '\r') {
                if (signed_len >= sizeof(signed_data) - 1) return SIMPLE_KEY_BAD_SIG;
                signed_data[signed_len++] = *q++;
            }
            
            if (signed_len >= sizeof(signed_data) - 1) return SIMPLE_KEY_BAD_SIG;
            signed_data[signed_len++] = '\n';

            /* Skip to next line */
            while (q < end_of_signed && (*q == '\n' || *q == '\r')) q++;
        }

        if (crypto_ed25519_check(sig, pub_key, signed_data, signed_len) != 0) {   /* 0 = valid */
            return SIMPLE_KEY_BAD_SIG;
        }

        k->signed_present = true;
    }

    return SIMPLE_OK;
}

/* XML parser arena allocation */
typedef struct {
    simple_manifest *manifest;
    unsigned char *arena;
    size_t arena_size;
    size_t arena_pos;
} parse_ctx;

static void *arena_alloc(parse_ctx *ctx, size_t size)
{
    if (ctx->arena_pos + size > ctx->arena_size) return NULL;
    void *ptr = ctx->arena + ctx->arena_pos;
    ctx->arena_pos += size;
    return ptr;
}

static char *arena_strdup(parse_ctx *ctx, const char *str, size_t len)
{
    char *dup = (char *)arena_alloc(ctx, len + 1);
    if (!dup) return NULL;
    memcpy(dup, str, len);
    dup[len] = 0;
    return dup;
}

static int parse_manifest_xml(const unsigned char *data, size_t size, simple_manifest *m)
{
    parse_ctx ctx = {0};
    ctx.manifest = m;

    /* Size the arena from the manifest itself: count the elements, and the XML's size bounds the path bytes */
    size_t n_sets = 0, n_packs = 0, n_files = 0;
    for (size_t i = 0; i + 5 < size; i++) {
        if (data[i] != '<') continue;
        if (memcmp(data + i + 1, "set", 3) == 0) n_sets++;
        else if (memcmp(data + i + 1, "pack", 4) == 0) n_packs++;
        else if (memcmp(data + i + 1, "file", 4) == 0) n_files++;
    }
    ctx.arena_size = n_sets * sizeof(simple_set) + n_packs * sizeof(simple_pack) + n_files * sizeof(simple_file)
                     + size + 64;
    ctx.arena = (unsigned char *)calloc(1, ctx.arena_size);
    if (!ctx.arena) return SIMPLE_MANIFEST_UNREADABLE;

    m->arena = ctx.arena;
    m->arena_size = ctx.arena_size;

    /* The arrays with 64-bit fields first, so each starts aligned; then the sets (chars only); then the paths */
    m->packs = (simple_pack *)arena_alloc(&ctx, n_packs * sizeof(simple_pack));
    m->files = (simple_file *)arena_alloc(&ctx, n_files * sizeof(simple_file));
    m->sets = (simple_set *)arena_alloc(&ctx, n_sets * sizeof(simple_set));

    const char *p = (const char *)data;
    const char *end = p + size;

    /* Skip XML declaration */
    if (strncmp(p, "<?xml", 5) == 0) {
        while (p < end && *p != '>') p++;
        if (p < end) p++;
    }

    /* Parse root element */
    while (p < end && isspace((unsigned char)*p)) p++;

    if (strncmp(p, "<install114360", 14) != 0) {
        free(ctx.arena);
        return SIMPLE_MANIFEST_UNREADABLE;
    }

    p += 14;

    /* Parse root attributes */
    char format_str[32] = "0";
    while (p < end && *p != '>' && *p != '/') {
        while (p < end && isspace((unsigned char)*p)) p++;
        if (*p == '>' || *p == '/') break;

        /* Parse attribute name */
        const char *attr_start = p;
        while (p < end && *p != '=' && !isspace((unsigned char)*p)) p++;
        size_t attr_len = p - attr_start;

        while (p < end && (isspace((unsigned char)*p) || *p == '=')) p++;
        if (p >= end || (*p != '"' && *p != '\'')) break;

        char quote = *p++;
        const char *val_start = p;
        while (p < end && *p != quote) p++;
        size_t val_len = p - val_start;
        if (p < end) p++; /* skip closing quote */

        if (attr_len == 6 && strncmp(attr_start, "format", 6) == 0) {
            memcpy(format_str, val_start, val_len < 31 ? val_len : 31);
            format_str[val_len < 31 ? val_len : 31] = 0;
        } else if (attr_len == 4 && strncmp(attr_start, "name", 4) == 0) {
            memcpy(m->name, val_start, val_len < 255 ? val_len : 255);
            m->name[val_len < 255 ? val_len : 255] = 0;
        } else if (attr_len == 7 && strncmp(attr_start, "created", 7) == 0) {
            memcpy(m->created, val_start, val_len < 31 ? val_len : 31);
            m->created[val_len < 31 ? val_len : 31] = 0;
        } else if (attr_len == 7 && strncmp(attr_start, "storage", 7) == 0) {
            memcpy(m->storage, val_start, val_len < 31 ? val_len : 31);
            m->storage[val_len < 31 ? val_len : 31] = 0;
        } else if (attr_len == 4 && strncmp(attr_start, "size", 4) == 0) {
            char size_str[64];
            memcpy(size_str, val_start, val_len < 63 ? val_len : 63);
            size_str[val_len < 63 ? val_len : 63] = 0;
            m->size = strtoull(size_str, NULL, 10);
        }
    }

    int format = atoi(format_str);
    if (format > 1) {
        free(ctx.arena);
        return SIMPLE_MANIFEST_FORMAT;
    }

    /* Skip to > */
    while (p < end && *p != '>') p++;
    if (p < end) p++;

    /* Parse child elements */
    while (p < end) {
        while (p < end && isspace((unsigned char)*p)) p++;
        if (*p != '<') break;
        p++;

        /* Check for closing tag */
        if (strncmp(p, "/install114360", 14) == 0) break;

        /* Check element type */
        if (strncmp(p, "set", 3) == 0) {
            p += 3;
            if ((size_t)m->sets_count >= n_sets) break;

            simple_set *set = &m->sets[m->sets_count];
            memset(set, 0, sizeof(*set));

            /* Parse set attributes */
            while (p < end && *p != '>' && *p != '/') {
                while (p < end && isspace((unsigned char)*p)) p++;
                if (*p == '>' || *p == '/') break;

                const char *attr_start = p;
                while (p < end && *p != '=' && !isspace((unsigned char)*p)) p++;
                size_t attr_len = p - attr_start;

                while (p < end && (isspace((unsigned char)*p) || *p == '=')) p++;
                if (p >= end || (*p != '"' && *p != '\'')) break;

                char quote = *p++;
                const char *val_start = p;
                while (p < end && *p != quote) p++;
                size_t val_len = p - val_start;
                if (p < end) p++;

                if (attr_len == 2 && strncmp(attr_start, "id", 2) == 0) {
                    memcpy(set->id, val_start, val_len < 31 ? val_len : 31);
                    set->id[val_len < 31 ? val_len : 31] = 0;
                } else if (attr_len == 8 && strncmp(attr_start, "revision", 8) == 0) {
                    memcpy(set->revision, val_start, val_len < 95 ? val_len : 95);
                    set->revision[val_len < 95 ? val_len : 95] = 0;
                } else if (attr_len == 4 && strncmp(attr_start, "note", 4) == 0) {
                    memcpy(set->note, val_start, val_len < 159 ? val_len : 159);
                    set->note[val_len < 159 ? val_len : 159] = 0;
                }
            }

            m->sets_count++;
            while (p < end && *p != '>') p++;
            if (p < end) p++;

        } else if (strncmp(p, "pack", 4) == 0) {
            p += 4;
            if ((size_t)m->packs_count >= n_packs) break;

            simple_pack *pack = &m->packs[m->packs_count];
            memset(pack, 0, sizeof(*pack));

            /* Parse pack attributes */
            while (p < end && *p != '>' && *p != '/') {
                while (p < end && isspace((unsigned char)*p)) p++;
                if (*p == '>' || *p == '/') break;

                const char *attr_start = p;
                while (p < end && *p != '=' && !isspace((unsigned char)*p)) p++;
                size_t attr_len = p - attr_start;

                while (p < end && (isspace((unsigned char)*p) || *p == '=')) p++;
                if (p >= end || (*p != '"' && *p != '\'')) break;

                char quote = *p++;
                const char *val_start = p;
                while (p < end && *p != quote) p++;
                size_t val_len = p - val_start;
                if (p < end) p++;

                if (attr_len == 5 && strncmp(attr_start, "index", 5) == 0) {
                    char idx_str[32];
                    memcpy(idx_str, val_start, val_len < 31 ? val_len : 31);
                    idx_str[val_len < 31 ? val_len : 31] = 0;
                    pack->index = atoi(idx_str);
                } else if (attr_len == 4 && strncmp(attr_start, "name", 4) == 0) {
                    memcpy(pack->name, val_start, val_len < 255 ? val_len : 255);
                    pack->name[val_len < 255 ? val_len : 255] = 0;
                } else if (attr_len == 4 && strncmp(attr_start, "size", 4) == 0) {
                    char size_str[64];
                    memcpy(size_str, val_start, val_len < 63 ? val_len : 63);
                    size_str[val_len < 63 ? val_len : 63] = 0;
                    pack->size = strtoull(size_str, NULL, 10);
                } else if (attr_len == 6 && strncmp(attr_start, "sha512", 6) == 0) {
                    if (val_len == 128) {
                        hex_decode(val_start, pack->sha512, 64);
                    }
                } else if (attr_len == 5 && strncmp(attr_start, "drive", 5) == 0) {
                    memcpy(pack->drive_id, val_start, val_len < 255 ? val_len : 255);
                    pack->drive_id[val_len < 255 ? val_len : 255] = 0;
                }
            }

            m->packs_count++;
            while (p < end && *p != '>') p++;
            if (p < end) p++;

        } else if (strncmp(p, "file", 4) == 0) {
            p += 4;
            if ((size_t)m->files_count >= n_files) break;

            simple_file *file = &m->files[m->files_count];
            memset(file, 0, sizeof(*file));
            file->codec = CODEC_RAW;
            file->path = NULL;

            /* Parse file attributes */
            char path_buf[512];
            memset(path_buf, 0, sizeof(path_buf));
            
            while (p < end && *p != '>' && *p != '/') {
                while (p < end && isspace((unsigned char)*p)) p++;
                if (*p == '>' || *p == '/') break;

                const char *attr_start = p;
                while (p < end && *p != '=' && !isspace((unsigned char)*p)) p++;
                size_t attr_len = p - attr_start;

                while (p < end && (isspace((unsigned char)*p) || *p == '=')) p++;
                if (p >= end || (*p != '"' && *p != '\'')) break;

                char quote = *p++;
                const char *val_start = p;
                while (p < end && *p != quote) p++;
                size_t val_len = p - val_start;
                if (p < end) p++;

                if (attr_len == 4 && strncmp(attr_start, "path", 4) == 0) {
                    memcpy(path_buf, val_start, val_len < 511 ? val_len : 511);
                    path_buf[val_len < 511 ? val_len : 511] = 0;
                } else if (attr_len == 4 && strncmp(attr_start, "size", 4) == 0) {
                    char size_str[32];
                    memcpy(size_str, val_start, val_len < 31 ? val_len : 31);
                    size_str[val_len < 31 ? val_len : 31] = 0;
                    file->size = strtoull(size_str, NULL, 10);
                } else if (attr_len == 3 && strncmp(attr_start, "md5", 3) == 0) {
                    if (val_len == 32) {
                        hex_decode(val_start, file->md5, 16);
                    }
                } else if (attr_len == 4 && strncmp(attr_start, "pack", 4) == 0) {
                    char pack_str[32];
                    memcpy(pack_str, val_start, val_len < 31 ? val_len : 31);
                    pack_str[val_len < 31 ? val_len : 31] = 0;
                    file->pack = atoi(pack_str);
                } else if (attr_len == 6 && strncmp(attr_start, "offset", 6) == 0) {
                    char offset_str[32];
                    memcpy(offset_str, val_start, val_len < 31 ? val_len : 31);
                    offset_str[val_len < 31 ? val_len : 31] = 0;
                    file->offset = strtoull(offset_str, NULL, 10);
                } else if (attr_len == 6 && strncmp(attr_start, "stored", 6) == 0) {
                    char stored_str[32];
                    memcpy(stored_str, val_start, val_len < 31 ? val_len : 31);
                    stored_str[val_len < 31 ? val_len : 31] = 0;
                    file->stored = strtoull(stored_str, NULL, 10);
                } else if (attr_len == 5 && strncmp(attr_start, "codec", 5) == 0) {
                    if (val_len == 4 && strncmp(val_start, "zlib", 4) == 0) {
                        file->codec = CODEC_ZLIB;
                    }
                }
            }

            /* Copy path to arena */
            if (path_buf[0]) {
                file->path = arena_strdup(&ctx, path_buf, strlen(path_buf));
                if (!file->path) {
                    free(ctx.arena);
                    return SIMPLE_MANIFEST_UNREADABLE;
                }
            }

            m->files_count++;
            while (p < end && *p != '>') p++;
            if (p < end) p++;

        } else {
            /* Skip unknown element */
            while (p < end && *p != '>') p++;
            if (p < end) p++;
        }
    }

    log_msg("Manifest parsed: %d sets, %d packs, %d files", m->sets_count, m->packs_count, m->files_count);
    return SIMPLE_OK;
}

void simple_manifest_free(simple_manifest *m)
{
    if (m && m->arena) {
        free(m->arena);
        memset(m, 0, sizeof(*m));
    }
}

/* ── Google Drive ──────────────────────────────────────────────────────────────────────────────────────────────
 * Files: https://drive.usercontent.google.com/download?id=<id>&export=download&confirm=t. Files Drive cannot scan
 * for viruses (large ones) may still answer with an HTML page holding a form ("Download anyway"): its action and
 * hidden inputs (id, export, confirm, uuid) are requested next. Folders: embeddedfolderview lists names and ids. */

#define CODE_CANCELLED 902

static void drive_url(const char *id, char *url, size_t n)
{
    snprintf(url, n, "https://drive.usercontent.google.com/download?id=%s&export=download&confirm=t", id);
}

static bool prefix_ci(const unsigned char *p, size_t n, const char *word)
{
    size_t w = strlen(word);
    if (n < w) {
        return false;
    }
    for (size_t i = 0; i < w; i++) {
        if (tolower(p[i]) != word[i]) {
            return false;
        }
    }
    return true;
}

static bool starts_html(const unsigned char *p, size_t n)
{
    while (n && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t')) {
        p++;
        n--;
    }
    return prefix_ci(p, n, "<!doctype") || prefix_ci(p, n, "<html");
}

/* The "Download anyway" form of Drive's interstitial page as a URL, or false when there is none. */
static bool drive_form_url(const char *html, char *url, size_t n)
{
    const char *form = strstr(html, "<form");
    if (!form) {
        return false;
    }
    const char *act = strstr(form, "action=\"");
    const char *end_form = strstr(form, "</form>");
    if (!act || (end_form && act > end_form)) {
        return false;
    }
    act += 8;
    const char *q = strchr(act, '"');
    if (!q || (size_t)(q - act) >= n) {
        return false;
    }
    size_t len = (size_t)(q - act);
    memcpy(url, act, len);
    url[len] = 0;
    char sep = strchr(url, '?') ? '&' : '?';
    for (const char *in = form; (in = strstr(in, "<input")) && (!end_form || in < end_form); in++) {
        const char *nm = strstr(in, "name=\""), *vl = strstr(in, "value=\""), *close = strchr(in, '>');
        if (!nm || !vl || !close || nm > close || vl > close) {
            continue;
        }
        nm += 6;
        vl += 7;
        const char *nm_end = strchr(nm, '"'), *vl_end = strchr(vl, '"');
        if (!nm_end || !vl_end) {
            continue;
        }
        int w = snprintf(url + len, n - len, "%c%.*s=%.*s", sep, (int)(nm_end - nm), nm, (int)(vl_end - vl), vl);
        if (w < 0 || (size_t)w >= n - len) {
            return false;
        }
        len += (size_t)w;
        sep = '&';
    }
    log_msg("Drive interstitial: continuing with %s", url);
    return true;
}

typedef struct {
    FILE *file;                 /* the download goes here (NULL: memory) */
    unsigned char *mem;         /* ...or here */
    size_t mem_len, mem_cap, mem_size;   /* bytes held, the most allowed, bytes allocated */
    unsigned char head[512];    /* the first bytes, to tell an HTML page from the data */
    size_t head_len;
    bool html;
    uint64_t got, base, total;
    simple_progress_cb progress;
    void *pud;
    const bool *cancel;
    bool write_failed;
} dl;

static bool dl_sink(void *ud, const void *data, size_t n)
{
    dl *d = ud;
    if (d->cancel && *d->cancel) {
        return false;
    }
    if (d->got == 0 && d->head_len == 0) {
        d->head_len = n < sizeof(d->head) ? n : sizeof(d->head);
        memcpy(d->head, data, d->head_len);
        d->html = d->base == 0 && starts_html(d->head, d->head_len);
    }
    if (d->file && !d->html) {
        if (fwrite(data, 1, n, d->file) != n) {
            d->write_failed = true;
            return false;
        }
    } else {
        size_t cap = d->html ? 4u << 20 : d->mem_cap;
        if (d->mem_len + n > cap) {
            return false;
        }
        if (d->mem_len + n + 1 > d->mem_size) {
            size_t want = (d->mem_len + n + 1) * 2;
            unsigned char *grown = realloc(d->mem, want);
            if (!grown) {
                return false;
            }
            d->mem = grown;
            d->mem_size = want;
        }
        memcpy(d->mem + d->mem_len, data, n);
        d->mem_len += n;
        d->mem[d->mem_len] = 0;
    }
    d->got += n;
    if (d->progress) {
        d->progress(d->pud, d->base + d->got, d->total);
    }
    return true;
}

/* Map a failed request to a ZZ code. */
static int net_code(int status, const char *what, char *error, size_t n)
{
    if (status == 403 || status == 429) {
        snprintf(error, n, "Google Drive refused %s (HTTP %d: daily download quota, or not shared)", what, status);
        return SIMPLE_DRIVE_REFUSED;
    }
    if (status == 404) {
        snprintf(error, n, "Google Drive has no %s (HTTP 404)", what);
        return SIMPLE_PACK_NOT_FOUND;
    }
    if (status >= 400) {
        snprintf(error, n, "Google Drive answered HTTP %d for %s", status, what);
        return SIMPLE_DRIVE_REFUSED;
    }
    return SIMPLE_NO_CONNECTION;
}

/* Download Drive file `id` into memory (cap bytes at most) or onto the end of `file` from offset `base`, past the
 * interstitial page if Drive shows one. */
static int drive_fetch(const char *id, FILE *file, uint64_t base, uint64_t total, unsigned char **mem,
                       size_t *mem_len, size_t cap, simple_progress_cb progress, void *pud, const bool *cancel,
                       const char *what, char *error, size_t error_size)
{
    char url[2048];
    drive_url(id, url, sizeof(url));
    for (int hop = 0; hop < 3; hop++) {
        dl d = {.file = file, .mem_cap = cap, .base = base, .total = total, .progress = progress, .pud = pud,
                .cancel = cancel};
        int status = 0;
        char err[512] = "";
        log_msg("GET %s (from byte %llu)", url, (unsigned long long)base);
        bool ok = net_get(url, base, dl_sink, &d, NULL, &status, err, sizeof(err));
        log_msg("  HTTP %d, %llu bytes%s%s%s", status, (unsigned long long)d.got, d.html ? ", an HTML page" : "",
                err[0] ? ": " : "", err);
        if (cancel && *cancel) {
            free(d.mem);
            snprintf(error, error_size, "Cancelled.");
            return CODE_CANCELLED;
        }
        if (d.write_failed) {
            free(d.mem);
            snprintf(error, error_size, "cannot write the download of %s to disk", what);
            return SIMPLE_FILE_DIFFERS;
        }
        if (!ok) {
            free(d.mem);
            int code = net_code(status, what, error, error_size);
            if (code == SIMPLE_NO_CONNECTION) {
                snprintf(error, error_size, "no connection to Google Drive for %s: %s", what, err);
            }
            return code;
        }
        if (d.html) {
            char next[2048];
            bool form = d.mem && drive_form_url((const char *)d.mem, next, sizeof(next));
            free(d.mem);
            if (!form) {
                snprintf(error, error_size, "Google Drive sent a web page instead of %s (quota exceeded or not "
                         "shared \"Anyone with the link\")", what);
                return SIMPLE_DRIVE_REFUSED;
            }
            snprintf(url, sizeof(url), "%s", next);
            continue;
        }
        if (mem) {
            *mem = d.mem;
            *mem_len = d.mem_len;
        } else {
            free(d.mem);
        }
        return SIMPLE_OK;
    }
    snprintf(error, error_size, "Google Drive kept sending web pages instead of %s", what);
    return SIMPLE_DRIVE_REFUSED;
}

/* The id of file `name` in public Drive folder `folder`, from its embedded folder view. */
static int drive_find(const char *folder, const char *name, char *id, size_t id_size, char *error, size_t error_size)
{
    char url[512];
    snprintf(url, sizeof(url), "https://drive.google.com/embeddedfolderview?id=%s", folder);
    dl d = {.mem_cap = 8u << 20};
    int status = 0;
    char err[512] = "";
    log_msg("GET %s (find %s)", url, name);
    if (!net_get(url, 0, dl_sink, &d, NULL, &status, err, sizeof(err)) || !d.mem) {
        free(d.mem);
        int code = net_code(status, "the folder listing", error, error_size);
        if (code == SIMPLE_NO_CONNECTION) {
            snprintf(error, error_size, "no connection to Google Drive: %s", err);
        }
        return code;
    }
    /* Entries: <a href="https://drive.google.com/file/d/<id>/view..."> ... <div class="flip-entry-title">name</div> */
    const char *p = (const char *)d.mem;
    while ((p = strstr(p, "/file/d/"))) {
        p += 8;
        size_t n = strcspn(p, "/\"?");
        const char *title = strstr(p, "flip-entry-title\">");
        const char *next = strstr(p, "/file/d/");
        if (title && (!next || title < next)) {
            title += 18;
            size_t tn = strcspn(title, "<");
            if (tn == strlen(name) && strncmp(title, name, tn) == 0 && n < id_size) {
                memcpy(id, p, n);
                id[n] = 0;
                log_msg("  %s is %s", name, id);
                free(d.mem);
                return SIMPLE_OK;
            }
        }
    }
    free(d.mem);
    snprintf(error, error_size, "%s is not in the install's Google Drive folder", name);
    return SIMPLE_PACK_NOT_FOUND;
}

int simple_manifest_fetch(const simple_key *k, simple_manifest *m,
                          simple_progress_cb progress, void *ud,
                          simple_cancel_flag cancel,
                          char *error, size_t error_size)
{
    char scratch[512];
    if (!error || !error_size) {
        error = scratch;
        error_size = sizeof(scratch);
    }
    if (!k || !m || (!k->folder_id[0] && !k->manifest_id[0])) {
        snprintf(error, error_size, "the key names no folder and no manifest");
        return SIMPLE_KEY_NOT_KEY;
    }
    char id[256];
    snprintf(id, sizeof(id), "%s", k->manifest_id);
    if (!id[0]) {
        int code = drive_find(k->folder_id, "manifest.xml.zlib", id, sizeof(id), error, error_size);
        if (code) {
            return code == SIMPLE_PACK_NOT_FOUND ? SIMPLE_MANIFEST_DL : code;
        }
    }
    unsigned char *data = NULL;
    size_t len = 0;
    int code = drive_fetch(id, NULL, 0, 0, &data, &len, 64u << 20, progress, ud, cancel, "the manifest", error,
                           error_size);
    if (code) {
        return code == SIMPLE_NO_CONNECTION || code == CODE_CANCELLED || code == SIMPLE_DRIVE_REFUSED
                   ? code : SIMPLE_MANIFEST_DL;
    }
    log_msg("Downloaded manifest: %zu bytes", len);
    unsigned char digest[64];
    sha512(data, len, digest);
    if (memcmp(digest, k->sha512, 64) != 0) {
        free(data);
        snprintf(error, error_size, "the manifest is not the one the key names (SHA-512 differs)");
        return SIMPLE_MANIFEST_HASH;
    }
    log_msg("Manifest SHA-512 verified");

    /* Inflate (grow the buffer as needed). */
    z_stream zs = {0};
    size_t cap = len * 8 + (1u << 20), out_len = 0;
    unsigned char *xml = malloc(cap);
    int zret = xml ? inflateInit(&zs) : Z_MEM_ERROR;
    if (zret == Z_OK) {
        zs.next_in = data;
        zs.avail_in = (uInt)len;
        do {
            if (out_len == cap) {
                unsigned char *grown = realloc(xml, cap * 2);
                if (!grown) {
                    zret = Z_MEM_ERROR;
                    break;
                }
                xml = grown;
                cap *= 2;
            }
            zs.next_out = xml + out_len;
            zs.avail_out = (uInt)(cap - out_len);
            zret = inflate(&zs, Z_NO_FLUSH);
            out_len = cap - zs.avail_out;
        } while (zret == Z_OK);
        inflateEnd(&zs);
    }
    free(data);
    if (zret != Z_STREAM_END) {
        free(xml);
        snprintf(error, error_size, "the manifest is damaged (zlib)");
        return SIMPLE_MANIFEST_UNREADABLE;
    }
    log_msg("Manifest decompressed: %zu bytes", out_len);
    int result = parse_manifest_xml(xml, out_len, m);
    free(xml);
    if (result == SIMPLE_OK) {
        snprintf(m->folder_id, sizeof(m->folder_id), "%s", k->folder_id);
    } else {
        snprintf(error, error_size, result == SIMPLE_MANIFEST_FORMAT ? "the manifest needs a newer installer"
                                                                     : "the manifest is not readable");
    }
    return result;
}

#ifdef _WIN32
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko
#define ftell64 ftello
#endif

static uint64_t file_size_of(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    fseek64(f, 0, SEEK_END);
    long long n = (long long)ftell64(f);
    fclose(f);
    return n > 0 ? (uint64_t)n : 0;
}

static bool file_sha512(const char *path, unsigned char digest[64], const bool *cancel)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    crypto_sha512_ctx ctx;
    crypto_sha512_init(&ctx);
    unsigned char *buf = malloc(1u << 20);
    size_t n;
    bool ok = buf != NULL;
    while (ok && (n = fread(buf, 1, 1u << 20, f)) > 0) {
        if (cancel && *cancel) {
            ok = false;
            break;
        }
        crypto_sha512_update(&ctx, buf, n);
    }
    ok = ok && !ferror(f);
    fclose(f);
    free(buf);
    crypto_sha512_final(&ctx, digest);
    return ok;
}

int simple_pack_fetch(const simple_manifest *m, int pack_idx,
                      const char *work_dir,
                      simple_progress_cb progress, void *ud,
                      simple_cancel_flag cancel,
                      char *error, size_t error_size)
{
    char scratch[512];
    if (!error || !error_size) {
        error = scratch;
        error_size = sizeof(scratch);
    }
    if (!m || pack_idx < 0 || pack_idx >= m->packs_count) {
        snprintf(error, error_size, "no pack %d in the manifest", pack_idx);
        return SIMPLE_PACK_NOT_FOUND;
    }
    const simple_pack *pack = &m->packs[pack_idx];
    char path[1024], id[256], what[300];
    snprintf(path, sizeof(path), "%s/%s", work_dir, pack->name);
    snprintf(what, sizeof(what), "%s", pack->name);
    snprintf(id, sizeof(id), "%s", pack->drive_id);

    for (int attempt = 0; attempt < 2; attempt++) {
        uint64_t have = file_size_of(path);
        if (have > pack->size) {
            log_msg("%s: %llu bytes, more than the pack; starting over", pack->name, (unsigned long long)have);
            remove(path);
            have = 0;
        }
        if (have < pack->size) {
            if (!id[0]) {
                if (!m->folder_id[0]) {
                    snprintf(error, error_size, "%s has no Drive id and the key names no folder", pack->name);
                    return SIMPLE_PACK_NOT_FOUND;
                }
                int code = drive_find(m->folder_id, pack->name, id, sizeof(id), error, error_size);
                if (code) {
                    return code;
                }
            }
            FILE *f = fopen(path, have ? "ab" : "wb");
            if (!f) {
                snprintf(error, error_size, "cannot write %s", path);
                return SIMPLE_FILE_DIFFERS;
            }
            log_msg("%s: downloading from byte %llu of %llu", pack->name, (unsigned long long)have,
                    (unsigned long long)pack->size);
            int code = drive_fetch(id, f, have, pack->size, NULL, NULL, 0, progress, ud, cancel, what, error,
                                   error_size);
            if (fclose(f) != 0 && !code) {
                snprintf(error, error_size, "cannot write %s", path);
                code = SIMPLE_FILE_DIFFERS;
            }
            if (code) {
                return code;   /* a partial pack stays: the next run continues it */
            }
        }
        unsigned char digest[64];
        uint64_t got = file_size_of(path);
        bool same = got == pack->size && file_sha512(path, digest, cancel) && memcmp(digest, pack->sha512, 64) == 0;
        if (cancel && *cancel) {
            snprintf(error, error_size, "Cancelled.");
            return CODE_CANCELLED;
        }
        log_msg("%s: %llu bytes, SHA-512 %s", pack->name, (unsigned long long)got, same ? "verified" : "WRONG");
        if (same) {
            return SIMPLE_OK;
        }
        remove(path);    /* damaged or not what the manifest names: download it again once */
    }
    snprintf(error, error_size, "%s downloaded twice and is still not the manifest's pack (SHA-512 differs)",
             pack->name);
    return SIMPLE_PACK_HASH;
}

int simple_file_read(const simple_manifest *m, const simple_file *f,
                     const char *work_dir,
                     unsigned char **out, size_t *out_len,
                     char *error, size_t error_size)
{
    char scratch[512];
    if (!error || !error_size) {
        error = scratch;
        error_size = sizeof(scratch);
    }
    if (!m || !f || !out || !out_len || f->pack < 0 || f->pack >= m->packs_count) {
        snprintf(error, error_size, "bad file entry");
        return SIMPLE_FILE_DIFFERS;
    }
    *out = NULL;
    *out_len = 0;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", work_dir, m->packs[f->pack].name);
    unsigned char *stored = malloc(f->stored ? f->stored : 1);
    FILE *in = fopen(path, "rb");
    bool read_ok = stored && in && fseek64(in, (long long)f->offset, SEEK_SET) == 0
                   && (f->stored == 0 || fread(stored, 1, f->stored, in) == f->stored);
    if (in) {
        fclose(in);
    }
    if (!read_ok) {
        free(stored);
        snprintf(error, error_size, "cannot read %s from %s", f->path, m->packs[f->pack].name);
        return SIMPLE_FILE_DECOMP;
    }
    unsigned char *data;
    if (f->codec == CODEC_ZLIB) {
        data = malloc(f->size ? f->size : 1);
        uLongf dest_len = (uLongf)f->size;
        int z = data ? uncompress(data, &dest_len, stored, (uLong)f->stored) : Z_MEM_ERROR;
        free(stored);
        if (z != Z_OK || dest_len != f->size) {
            free(data);
            snprintf(error, error_size, "%s does not decompress (zlib %d)", f->path, z);
            return SIMPLE_FILE_DECOMP;
        }
    } else {
        if (f->stored != f->size) {
            free(stored);
            snprintf(error, error_size, "%s: stored %llu bytes, expected %llu", f->path,
                     (unsigned long long)f->stored, (unsigned long long)f->size);
            return SIMPLE_FILE_DIFFERS;
        }
        data = stored;
    }
    unsigned char digest[16];
    md5(data, (unsigned)f->size, digest);
    if (memcmp(digest, f->md5, 16) != 0) {
        free(data);
        snprintf(error, error_size, "%s is not the file the manifest names (MD5 differs)", f->path);
        return SIMPLE_FILE_DIFFERS;
    }
    *out = data;
    *out_len = (size_t)f->size;
    return SIMPLE_OK;
}
