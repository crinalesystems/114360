/* Client patches: named fixes to the game's program files (FixedFuncShader.fxb, ...; not the DAT packs), each for
   the client build it was made for, applied in memory as the client reads the file. The files on disk stay as
   installed. The chat's /patches lists them; a preset's "patches" key turns them on and off. */
#ifndef PATCHES_H
#define PATCHES_H

#ifdef __cplusplus
extern "C" {
#endif

/* One byte edit: `size` bytes at file offset `offset` become `bytes`, where the file still holds `expect`. */
typedef struct {
    unsigned long offset;
    unsigned long size;
    const unsigned char *expect;
    const unsigned char *bytes;
} PATCH_EDIT;

typedef struct {
    const char *id;            /* name in /patches and the preset's patches key */
    const char *title;         /* one line */
    const char *file;          /* program file, relative to the revision's 0001 folder */
    unsigned long file_size;   /* the build's size of that file; another size means another build: not applied */
    int default_on;            /* on when the preset does not name it */
    const PATCH_EDIT *edits;
    int edit_count;
    const char *detail[4];     /* /patches <id>: what it changes and why */
} CLIENT_PATCH;

extern const CLIENT_PATCH client_patches[];
extern const int client_patch_count;

/* Whether patch `p` is on under a preset's patches key ("water", "-water", "water,-other"; "" = defaults). */
int patch_enabled(const CLIENT_PATCH *p, const char *list);

/* Set patch `p` on or off in a preset's patches key `list` (size bytes): its entry is removed, and written back
   ("id" or "-id") only when the state differs from the patch's default. */
void patch_list_set(char *list, int size, const CLIENT_PATCH *p, int on);

/* The patch whose file is `name` (a path ending in the file name, any case), or NULL. */
const CLIENT_PATCH *patch_for_file(const char *name, int length);

/* Apply `p`'s edits that fall inside `data`, which holds the file's bytes from `position` on. Returns the number
   of edits applied; an edit whose bytes differ from `expect` is left alone and counted in *mismatched. */
int patch_apply(const CLIENT_PATCH *p, unsigned long position, unsigned char *data, unsigned long length,
                int *mismatched);

#ifdef __cplusplus
}
#endif

#endif
