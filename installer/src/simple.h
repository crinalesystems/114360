#ifndef SIMPLE_H
#define SIMPLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* The 114360 signing key's public half (tools/publish-install.py keygen; the private key never leaves the host) */
#define SIMPLE_PUBLIC_KEY_HEX "da23e4a8a74dfe4c1e14cf19b942214b1d04cd87eb1f36d65f6033319e7bcbbf"

/* Error codes (ZZnnn format) */
enum simple_code {
    SIMPLE_OK = 0,
    
    /* ZZ1xx: key and manifest */
    SIMPLE_KEY_NOT_KEY = 101,          /* not a key */
    SIMPLE_KEY_BAD_SIG = 102,          /* bad signature */
    SIMPLE_MANIFEST_DL = 103,          /* manifest download failed */
    SIMPLE_MANIFEST_HASH = 104,        /* manifest hash mismatch */
    SIMPLE_MANIFEST_UNREADABLE = 105,  /* manifest not readable (zlib) */
    SIMPLE_MANIFEST_FORMAT = 106,      /* manifest format too new */
    
    /* ZZ2xx: download */
    SIMPLE_NO_CONNECTION = 201,        /* no connection */
    SIMPLE_DRIVE_REFUSED = 202,        /* Drive refused (quota, not shared) */
    SIMPLE_PACK_HASH = 203,            /* pack hash mismatch */
    SIMPLE_PACK_NOT_FOUND = 204,       /* pack not found in folder */
    
    /* ZZ5xx: file check */
    SIMPLE_FILE_DIFFERS = 501,         /* file differs after writing */
    SIMPLE_FILE_DECOMP = 502,          /* decompression failed */
};

/* Convert error code to text (e.g., SIMPLE_KEY_NOT_KEY -> "ZZ101") */
const char *simple_code_text(int code);

/* Key structure (parsed from base64 secret key) */
typedef struct {
    char folder_id[256];        /* Google Drive folder ID */
    char manifest_id[256];      /* Google Drive file ID of manifest */
    unsigned char sha512[64];   /* SHA-512 of manifest.xml.zlib */
    bool signed_present;        /* true if sig was present and verified */
} simple_key;

/* Parse base64 secret key (standard or URL-safe, whitespace tolerant)
   Also accepts raw key text (not base64).
   Returns SIMPLE_OK, SIMPLE_KEY_NOT_KEY, or SIMPLE_KEY_BAD_SIG.
   Fills simple_key fields; signed_present true only if sig present AND verified.
*/
int simple_key_parse(const char *base64, simple_key *k);

/* Manifest set entry (from <set> elements) */
typedef struct {
    char id[32];                /* set ID */
    char revision[96];          /* revision identifier */
    char note[160];             /* optional note */
} simple_set;

/* Manifest file entry */
typedef struct {
    const char *path;           /* relative path (/ separated), points into arena */
    uint64_t size;              /* size of the file as installed */
    unsigned char md5[16];      /* MD5 of the file */
    int pack;                   /* pack index */
    uint64_t offset;            /* offset in pack stream */
    uint64_t stored;            /* bytes stored in pack (after zlib if codec=zlib) */
    enum { CODEC_RAW, CODEC_ZLIB } codec;
} simple_file;

/* Manifest pack entry */
typedef struct {
    int index;
    char name[256];             /* e.g., "pack-000.bin" */
    uint64_t size;              /* size of pack file */
    unsigned char sha512[64];   /* SHA-512 of pack */
    char drive_id[256];         /* Drive file ID (may be empty) */
} simple_pack;

/* Manifest structure (parsed XML) */
typedef struct {
    char name[256];             /* install name */
    char created[32];           /* creation date */
    char storage[32];           /* "pooled" or "direct" */
    char folder_id[256];        /* the key's Drive folder (finds packs without a drive id), may be empty */
    uint64_t size;              /* total install size */
    
    simple_set *sets;
    int sets_count;
    
    simple_pack *packs;
    int packs_count;
    
    simple_file *files;
    int files_count;
    
    /* Internal: arena for string data */
    unsigned char *arena;
    size_t arena_size;
} simple_manifest;

/* Progress callback: (done, total) bytes */
typedef void (*simple_progress_cb)(void *ud, uint64_t done, uint64_t total);

/* Cancel flag (set to true to abort) */
typedef bool *simple_cancel_flag;

/* Fetch and parse manifest
   - Resolves manifest by ID, or finds "manifest.xml.zlib" by name in folder
   - Downloads, checks sha512, inflates (zlib), parses XML
   - Returns SIMPLE_OK or error code
   - Fills simple_manifest; caller must free with simple_manifest_free()
   - On error, error buffer filled with human-readable message
*/
int simple_manifest_fetch(const simple_key *k, simple_manifest *m,
                          simple_progress_cb progress, void *ud,
                          simple_cancel_flag cancel,
                          char *error, size_t error_size);

void simple_manifest_free(simple_manifest *m);

/* Download pack file to <work_dir>/<name>
   - Resumes existing partial file (Range header)
   - Verifies sha512 (deletes and retries once if mismatch)
   - Resolves missing drive_id via folder listing
   - Returns SIMPLE_OK or error code
*/
int simple_pack_fetch(const simple_manifest *m, int pack,
                      const char *work_dir,
                      simple_progress_cb progress, void *ud,
                      simple_cancel_flag cancel,
                      char *error, size_t error_size);

/* Read file from downloaded pack
   - Reads the data stream from the pack
   - Inflates if codec=zlib
   - Checks size and MD5
   - Returns SIMPLE_OK or error code
   - Allocates *out; caller must free with free()
*/
int simple_file_read(const simple_manifest *m, const simple_file *f,
                     const char *work_dir,
                     unsigned char **out, size_t *out_len,
                     char *error, size_t error_size);

/* Verbose logging hook */
typedef void (*simple_log_cb)(void *ud, const char *line);
void simple_set_log(simple_log_cb log, void *ud);

#endif
