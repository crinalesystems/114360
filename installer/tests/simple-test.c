// The Simple install against a real hosted install, with the installer's own code: the key (signature), the
// manifest (SHA-512), then one pack (or all, and optionally every file written into a folder), each pack's SHA-512
// and every file inflated and MD5-checked.
//   simple-test <key> <work_dir> [pack | all [target_dir]]        (installer/build.sh simple-test)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/stat.h>
#include "simple.h"

static void on_log(void *ud, const char *line)
{
    (void)ud;
    printf("  log: %s\n", line);
    fflush(stdout);
}

static int last_pct = -1;
static void on_progress(void *ud, uint64_t done, uint64_t total)
{
    (void)ud;
    int pct = total ? (int)(done * 100 / total) : 0;
    if (pct / 10 != last_pct / 10) {
        last_pct = pct;
        printf("  %3d%%  %llu / %llu bytes\n", pct, (unsigned long long)done, (unsigned long long)total);
        fflush(stdout);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: simple-test <key> <work_dir> [pack | all [target_dir]]\n");
        return 2;
    }
    bool all = argc > 3 && strcmp(argv[3], "all") == 0;
    int pack = argc > 3 && !all ? atoi(argv[3]) : 0;
    const char *target = all && argc > 4 ? argv[4] : nullptr;
    bool cancel = false;
    char error[512] = "";
    simple_set_log(on_log, nullptr);
    simple_key k;
    int rc = simple_key_parse(argv[1], &k);
    printf("key: %s, signed: %s\n", rc ? simple_code_text(rc) : "ok", k.signed_present ? "YES (verified)" : "no");
    if (rc) {
        return 1;
    }
    simple_manifest m = {0};
    time_t t0 = time(nullptr);
    rc = simple_manifest_fetch(&k, &m, on_progress, nullptr, &cancel, error, sizeof(error));
    printf("manifest: %s %s (%ld s)\n", rc ? simple_code_text(rc) : "ok", error, (long)(time(nullptr) - t0));
    if (rc) {
        return 1;
    }
    printf("  %s | %s | %d sets, %d packs, %d files, %llu bytes\n", m.name, m.storage, m.sets_count, m.packs_count,
           m.files_count, (unsigned long long)m.size);
    for (int i = 0; i < m.sets_count; i++) {
        printf("  set %s = %s (%s)\n", m.sets[i].id, m.sets[i].revision, m.sets[i].note);
    }
    int first = all ? 0 : pack, last = all ? m.packs_count - 1 : pack;
    time_t t_all = time(nullptr);
    for (int pk = first; pk <= last; pk++) {
        printf("pack %d: %s, %llu bytes\n", pk, m.packs[pk].name, (unsigned long long)m.packs[pk].size);
        last_pct = -1;
        t0 = time(nullptr);
        rc = simple_pack_fetch(&m, pk, argv[2], on_progress, nullptr, &cancel, error, sizeof(error));
        long secs = (long)(time(nullptr) - t0);
        printf("pack fetch: %s %s (%ld s, %.1f MB/s)\n", rc ? simple_code_text(rc) : "ok (SHA-512 verified)", error,
               secs, secs ? m.packs[pk].size / 1048576.0 / secs : 0.0);
        fflush(stdout);
        if (rc) {
            return 1;
        }
    }
    printf("all packs fetched in %ld s\n", (long)(time(nullptr) - t_all));
    if (target) {
        mkdir(target, 0755);
    }
    int ok = 0, bad = 0;
    uint64_t bytes = 0;
    for (int i = 0; i < m.files_count; i++) {
        if (!all && m.files[i].pack != pack) {
            continue;
        }
        unsigned char *data = nullptr;
        size_t len = 0;
        rc = simple_file_read(&m, &m.files[i], argv[2], &data, &len, error, sizeof(error));
        if (!rc && target) {      // write it where the install goes, making its folders
            char out[1024];
            snprintf(out, sizeof(out), "%s/%s", target, m.files[i].path);
            for (char *s = out + strlen(target) + 1; (s = strchr(s, '/')); s++) {
                *s = 0;
                mkdir(out, 0755);
                *s = '/';
            }
            FILE *f = fopen(out, "wb");
            if (!f || fwrite(data, 1, len, f) != len) {
                rc = 1;
                snprintf(error, sizeof(error), "write %s", out);
            }
            if (f) {
                fclose(f);
            }
        }
        if (rc) {
            if (bad++ < 5) {
                printf("  FILE %s: %s %s\n", m.files[i].path, simple_code_text(rc), error);
            }
        } else {
            ok++;
            bytes += len;
        }
        free(data);
    }
    printf("files: %d read and MD5-verified%s (%.1f MB), %d failed\n", ok, target ? " and written" : "",
           bytes / 1048576.0, bad);
    simple_manifest_free(&m);
    return bad ? 1 : 0;
}
