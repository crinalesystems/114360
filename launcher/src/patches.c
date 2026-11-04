/* Client patches (patches.h). C89. */
#include <string.h>
#include <ctype.h>

#include "patches.h"

/* The registry. Each patch names the build it was made for by its file's size; launcher.c applies the edits of
   every enabled patch as FFXiMain reads that file. */
const CLIENT_PATCH client_patches[] = {
    {NULL, NULL, NULL, 0, 0, NULL, 0, {NULL}},
};
const int client_patch_count = 0;

/* One entry of a patches key: [-]name, separated by commas or spaces. *s moves past it. Returns 0 at the end. */
static int next_entry(const char **s, const char **start, size_t *len, int *off)
{
    while (**s == ',' || **s == ' ') {
        (*s)++;
    }
    if (**s == '\0') {
        return 0;
    }
    *start = *s;
    while (**s && **s != ',' && **s != ' ') {
        (*s)++;
    }
    *off = **start == '-';
    if (*off) {
        (*start)++;
    }
    *len = (size_t)(*s - *start);
    return 1;
}

int patch_enabled(const CLIENT_PATCH *p, const char *list)
{
    const char *s = list != NULL ? list : "", *start;
    size_t len, id_len = strlen(p->id);
    int off;
    while (next_entry(&s, &start, &len, &off)) {
        if (len == id_len && _strnicmp(start, p->id, len) == 0) {
            return !off;
        }
    }
    return p->default_on;
}

void patch_list_set(char *list, int size, const CLIENT_PATCH *p, int on)
{
    char out[256];
    const char *s = list, *start;
    size_t len, n = 0, id_len = strlen(p->id);
    int off;
    out[0] = '\0';
    while (next_entry(&s, &start, &len, &off)) {
        if (len == id_len && _strnicmp(start, p->id, len) == 0) {
            continue;   /* this patch's old entry */
        }
        if (n + len + 3 < sizeof(out)) {
            if (n) {
                out[n++] = ',';
            }
            if (off) {
                out[n++] = '-';
            }
            memcpy(out + n, start, len);
            n += len;
            out[n] = '\0';
        }
    }
    if (on != p->default_on && n + id_len + 3 < sizeof(out)) {
        if (n) {
            out[n++] = ',';
        }
        if (!on) {
            out[n++] = '-';
        }
        memcpy(out + n, p->id, id_len);
        n += id_len;
        out[n] = '\0';
    }
    strncpy(list, out, (size_t)size - 1);
    list[size - 1] = '\0';
}

const CLIENT_PATCH *patch_for_file(const char *name, int length)
{
    int i;
    for (i = 0; i < client_patch_count; i++) {
        int n = (int)strlen(client_patches[i].file);
        if (length >= n && _strnicmp(name + length - n, client_patches[i].file, n) == 0 &&
            (length == n || name[length - n - 1] == '\\' || name[length - n - 1] == '/' ||
             name[length - n - 1] == ':')) {
            return &client_patches[i];
        }
    }
    return NULL;
}

int patch_apply(const CLIENT_PATCH *p, unsigned long position, unsigned char *data, unsigned long length,
                int *mismatched)
{
    int i, applied = 0;
    for (i = 0; i < p->edit_count; i++) {
        const PATCH_EDIT *e = &p->edits[i];
        if (e->offset < position || e->offset + e->size > position + length) {
            continue;   /* edits are small and never split across reads of this file; a split one is skipped */
        }
        if (memcmp(data + (e->offset - position), e->expect, e->size) != 0) {
            (*mismatched)++;
            continue;
        }
        memcpy(data + (e->offset - position), e->bytes, e->size);
        applied++;
    }
    return applied;
}
