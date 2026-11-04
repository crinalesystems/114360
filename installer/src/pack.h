// Pack a finished local install for hosting as a simple install (docs/installer-simple.md): pack-NNN.bin (each file
// one zlib stream, or raw bytes when zlib does not help) and manifest.xml.zlib, the same format
// tools/publish-install.py writes. Used by the window's Advanced > Generate secret key.
#ifndef PACK_H
#define PACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct pack_set {
    const char *id, *revision, *note;   // <set id revision note>
} pack_set;

// Pack every file under install_root into out_dir (created; its old pack-*.bin and manifest are replaced).
// progress(ud, done_bytes, total_bytes); *cancel stops it. manifest_sha512 receives the SHA-512 of the
// manifest.xml.zlib written (what an unsigned key carries). Returns 0, or a ZZ code with `error` filled.
int pack_build(const char *install_root, const char *out_dir, const char *name, const pack_set *sets, int set_count,
               void (*progress)(void *ud, uint64_t done, uint64_t total), void *ud, const bool *cancel,
               unsigned char manifest_sha512[64], char *error, size_t error_size);

// An unsigned secret key for a hosted folder: "114360-key 1\nfolder <id>\nsha512 <hex>\n", base64. Returns false
// when out is too small.
bool pack_key(const char *folder_id, const unsigned char manifest_sha512[64], char *out, size_t out_size);

// The Drive folder id in a share link (".../folders/<id>?usp=sharing", "...?id=<id>") or a bare id; false if none.
bool pack_folder_id(const char *link, char *id, size_t id_size);

#endif
