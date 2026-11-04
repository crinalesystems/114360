// Pack a finished local install for hosting (pack.h, docs/installer-simple.md).
#include "pack.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "md5.h"
#include "monocypher/monocypher-ed25519.h"

#define PACK_LIMIT (1024ull * 1024 * 1024)   // start a new pack past this many bytes

typedef struct entry {
    char *path;               // relative, '/' separated
    uint64_t size;
} entry;

typedef struct walk {
    entry *items;
    size_t count, cap;
    char root[1024];
    bool failed;
} walk;

static SDL_EnumerationResult SDLCALL walk_cb(void *ud, const char *dir, const char *name)
{
    walk *w = ud;
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", dir, name);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) {
        w->failed = true;
        return SDL_ENUM_FAILURE;
    }
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        size_t n = strlen(path);
        if (n + 1 < sizeof(path)) {
            path[n] = '/';
            path[n + 1] = 0;
        }
        return SDL_EnumerateDirectory(path, walk_cb, w) ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
    }
    if (info.type != SDL_PATHTYPE_FILE) {
        return SDL_ENUM_CONTINUE;
    }
    if (w->count == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 4096;
        w->items = realloc(w->items, w->cap * sizeof(entry));
    }
    const char *rel = path + strlen(w->root);
    char *copy = SDL_strdup(rel);
    for (char *c = copy; *c; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
    w->items[w->count++] = (entry){copy, info.size};
    return SDL_ENUM_CONTINUE;
}

static int by_path(const void *a, const void *b) { return strcmp(((const entry *)a)->path, ((const entry *)b)->path); }

typedef struct buffer {
    char *data;
    size_t len, cap;
} buffer;

static void put(buffer *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void putf(buffer *b, const char *fmt, ...)
{
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    put(b, tmp, n < 0 ? 0 : (size_t)(n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1));
}

// An XML attribute value, escaped.
static void put_attr(buffer *b, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '&': put(b, "&amp;", 5); break;
        case '<': put(b, "&lt;", 4); break;
        case '>': put(b, "&gt;", 4); break;
        case '"': put(b, "&quot;", 6); break;
        case '\'': put(b, "&apos;", 6); break;
        default: put(b, s, 1); break;
        }
    }
}

static void hex(const unsigned char *bytes, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[2 * n] = 0;
}

typedef struct stored {
    unsigned char md5[16];
    uint64_t size, offset, length;
    int pack;
    bool zlib;
} stored;

// Contents already stored, by MD5 and size: an open-addressed table of `stored` (slots twice the file count).
typedef struct seen_table {
    stored *slots;
    bool *used;
    size_t mask;
} seen_table;

static size_t slot_of(const seen_table *t, const unsigned char md[16], uint64_t size)
{
    uint64_t h;
    memcpy(&h, md, sizeof(h));
    size_t i = (size_t)(h ^ size) & t->mask;
    while (t->used[i] && (memcmp(t->slots[i].md5, md, 16) != 0 || t->slots[i].size != size)) {
        i = (i + 1) & t->mask;
    }
    return i;
}

int pack_build(const char *install_root, const char *out_dir, const char *name, const pack_set *sets, int set_count,
               void (*progress)(void *ud, uint64_t done, uint64_t total), void *ud, const bool *cancel,
               unsigned char manifest_sha512[64], char *error, size_t error_size)
{
    walk w = {0};
    snprintf(w.root, sizeof(w.root), "%s%s", install_root,
             install_root[0] && strchr("/\\", install_root[strlen(install_root) - 1]) ? "" : "/");
    if (!SDL_EnumerateDirectory(w.root, walk_cb, &w) || w.failed || w.count == 0) {
        snprintf(error, error_size, "cannot read the install at %s", install_root);
        return 305;
    }
    qsort(w.items, w.count, sizeof(entry), by_path);
    uint64_t total = 0, done = 0;
    bool pooled = false;
    for (size_t i = 0; i < w.count; i++) {
        total += w.items[i].size;
        pooled |= strcmp(w.items[i].path, "data/pool.txt") == 0;
    }
    if (!SDL_CreateDirectory(out_dir)) {
        snprintf(error, error_size, "cannot make %s", out_dir);
        return 302;
    }

    // Each distinct content once: found by MD5 and size among those already stored.
    seen_table seen = {0};
    size_t slots = 1;
    while (slots < w.count * 2) {
        slots <<= 1;
    }
    seen.slots = calloc(slots, sizeof(stored));
    seen.used = calloc(slots, sizeof(bool));
    seen.mask = slots - 1;
    stored *placed = calloc(w.count, sizeof(stored));   // per entry, in path order
    buffer packs_xml = {0}, files_xml = {0};
    int pack = -1, code = 0;
    FILE *out = nullptr;
    uint64_t pack_len = 0;
    crypto_sha512_ctx sha;
    char path[2048], pack_name[32], digest[129];

    for (size_t i = 0; i <= w.count && !code; i++) {
        bool last = i == w.count;
        if (!last && cancel && *cancel) {
            snprintf(error, error_size, "Cancelled.");
            code = 902;
            break;
        }
        unsigned char *data = nullptr, *z = nullptr;
        unsigned char md[16] = {0};
        size_t zlen = 0;
        bool use_zlib = false;
        stored *same = nullptr;
        if (!last) {
            entry *e = &w.items[i];
            snprintf(path, sizeof(path), "%s%s", w.root, e->path);
            size_t len = 0;
            data = e->size ? SDL_LoadFile(path, &len) : SDL_malloc(1);
            if (!data || len != e->size) {
                snprintf(error, error_size, "cannot read %s", path);
                SDL_free(data);
                code = 305;
                break;
            }
            md5(data, (unsigned)e->size, md);
            size_t at = slot_of(&seen, md, e->size);
            same = seen.used[at] ? &seen.slots[at] : nullptr;
            if (!same && e->size) {
                uLongf bound = compressBound((uLong)e->size);
                z = malloc(bound);
                if (z && compress2(z, &bound, data, (uLong)e->size, 9) == Z_OK && bound * 50 < e->size * 49) {
                    zlen = bound;
                    use_zlib = true;
                }
            }
        }
        uint64_t length = same ? 0 : (use_zlib ? zlen : (last ? 0 : w.items[i].size));
        // Close the pack when it is full (or at the end).
        if (out && (last || (pack_len > 0 && pack_len + length > PACK_LIMIT))) {
            unsigned char h[64];
            crypto_sha512_final(&sha, h);
            hex(h, 64, digest);
            bool closed = fclose(out) == 0;
            out = nullptr;
            if (!closed) {
                snprintf(error, error_size, "cannot write %s", pack_name);
                code = 302;
            }
            putf(&packs_xml, "  <pack index=\"%d\" name=\"%s\" size=\"%llu\" sha512=\"%s\"/>\n", pack, pack_name,
                 (unsigned long long)pack_len, digest);
        }
        if (last || code) {
            SDL_free(data);
            free(z);
            break;
        }
        entry *e = &w.items[i];
        if (same) {
            placed[i] = *same;
        } else {
            if (!out) {
                pack++;
                snprintf(pack_name, sizeof(pack_name), "pack-%03d.bin", pack);
                snprintf(path, sizeof(path), "%s/%s", out_dir, pack_name);
                out = fopen(path, "wb");
                if (!out) {
                    snprintf(error, error_size, "cannot write %s", path);
                    SDL_free(data);
                    free(z);
                    code = 302;
                    break;
                }
                pack_len = 0;
                crypto_sha512_init(&sha);
            }
            const unsigned char *bytes = use_zlib ? z : data;
            if (length && fwrite(bytes, 1, length, out) != length) {
                snprintf(error, error_size, "cannot write %s", pack_name);
                code = 302;
            }
            crypto_sha512_update(&sha, bytes, length);
            stored s = {.size = e->size, .offset = pack_len, .length = length, .pack = pack, .zlib = use_zlib};
            memcpy(s.md5, md, 16);
            placed[i] = s;
            pack_len += length;
            size_t at = slot_of(&seen, md, e->size);
            seen.slots[at] = s;
            seen.used[at] = true;
        }
        char md_hex[33];
        hex(placed[i].md5, 16, md_hex);
        put(&files_xml, "  <file path=\"", 14);
        put_attr(&files_xml, e->path);
        putf(&files_xml, "\" size=\"%llu\" md5=\"%s\" pack=\"%d\" offset=\"%llu\" stored=\"%llu\" codec=\"%s\"/>\n",
             (unsigned long long)e->size, md_hex, placed[i].pack, (unsigned long long)placed[i].offset,
             (unsigned long long)placed[i].length, placed[i].zlib ? "zlib" : "raw");
        done += e->size;
        if (progress) {
            progress(ud, done, total);
        }
        SDL_free(data);
        free(z);
    }

    if (!code) {
        SDL_Time now = 0;
        SDL_DateTime dt = {0};
        SDL_GetCurrentTime(&now);
        SDL_TimeToDateTime(now, &dt, true);
        buffer xml = {0};
        put(&xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<install114360 format=\"1\" name=\"", 72);
        put_attr(&xml, name);
        putf(&xml, "\" created=\"%04d-%02d-%02d\" storage=\"%s\" size=\"%llu\">\n", dt.year, dt.month, dt.day,
             pooled ? "pooled" : "direct", (unsigned long long)total);
        for (int s = 0; s < set_count; s++) {
            put(&xml, "  <set id=\"", 11);
            put_attr(&xml, sets[s].id);
            put(&xml, "\" revision=\"", 12);
            put_attr(&xml, sets[s].revision);
            put(&xml, "\" note=\"", 8);
            put_attr(&xml, sets[s].note ? sets[s].note : "");
            put(&xml, "\"/>\n", 4);
        }
        put(&xml, packs_xml.data ? packs_xml.data : "", packs_xml.len);
        put(&xml, files_xml.data ? files_xml.data : "", files_xml.len);
        put(&xml, "</install114360>\n", 17);
        uLongf bound = compressBound((uLong)xml.len);
        unsigned char *z = malloc(bound);
        if (!z || compress2(z, &bound, (const unsigned char *)xml.data, (uLong)xml.len, 9) != Z_OK) {
            snprintf(error, error_size, "out of memory compressing the manifest");
            code = 901;
        } else {
            snprintf(path, sizeof(path), "%s/manifest.xml.zlib", out_dir);
            FILE *m = fopen(path, "wb");
            if (!m || fwrite(z, 1, bound, m) != bound || fclose(m) != 0) {
                snprintf(error, error_size, "cannot write %s", path);
                code = 302;
            } else {
                crypto_sha512(manifest_sha512, z, bound);
            }
        }
        free(z);
        free(xml.data);
    }
    if (out) {
        fclose(out);
    }
    for (size_t i = 0; i < w.count; i++) {
        SDL_free(w.items[i].path);
    }
    free(w.items);
    free(seen.slots);
    free(seen.used);
    free(placed);
    free(packs_xml.data);
    free(files_xml.data);
    return code;
}

bool pack_key(const char *folder_id, const unsigned char manifest_sha512[64], char *out, size_t out_size)
{
    char digest[129], text[512];
    hex(manifest_sha512, 64, digest);
    int n = snprintf(text, sizeof(text), "114360-key 1\nfolder %s\nsha512 %s\n", folder_id, digest);
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = ((size_t)n + 2) / 3 * 4 + 1, o = 0;
    if (n <= 0 || need > out_size) {
        return false;
    }
    for (int i = 0; i < n; i += 3) {
        unsigned v = (unsigned char)text[i] << 16 | (i + 1 < n ? (unsigned char)text[i + 1] << 8 : 0)
                     | (i + 2 < n ? (unsigned char)text[i + 2] : 0);
        out[o++] = b64[v >> 18 & 63];
        out[o++] = b64[v >> 12 & 63];
        out[o++] = i + 1 < n ? b64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? b64[v & 63] : '=';
    }
    out[o] = 0;
    return true;
}

bool pack_folder_id(const char *link, char *id, size_t id_size)
{
    const char *start = strstr(link, "/folders/");
    if (start) {
        start += 9;
    } else if ((start = strstr(link, "id="))) {
        start += 3;
    } else {
        start = link;
    }
    size_t n = 0;
    while (start[n] && (SDL_isalnum((unsigned char)start[n]) || start[n] == '-' || start[n] == '_')) {
        n++;
    }
    if (n < 10 || n >= id_size) {
        return false;
    }
    memcpy(id, start, n);
    id[n] = 0;
    return true;
}
