/* Console test 5: why does XexLoadImage refuse PolCore.xex (C0000022)?

   Test 4 on Peer Pressure: our unsigned package R114360 reads as corrupt (570), and even the console's own
   PlayOnline package R100000 (mounted with XContentCreate) refuses its PolCore.xex with C0000022. So the media
   type is not the cause. The launcher loaded PolCore with flags 0x8 only; PolCore itself loads FFXi.dll with 0x9.
   This tries 0x8, 0x9 and 0x1 on PolCore from the 114360 folder and from R100000, then FFXi.dll with 0x9.
   Everything goes to game:\pkgtest.log; the program then returns to the dashboard on its own. */
#include <xtl.h>
#include <stdio.h>
#include <string.h>

NTSYSAPI LONG NTAPI XexLoadImage(const char *name, DWORD flags, DWORD min_version, HANDLE *module);
NTSYSAPI VOID NTAPI XexUnloadImage(HANDLE module);

static FILE *out;

static void say(const char *fmt, ...)
{
    va_list ap;
    if (out == NULL) {
        return;
    }
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fflush(out);
}

static void try_load(const char *path, DWORD flags)
{
    HANDLE h = NULL;
    LONG status = XexLoadImage(path, flags, 0, &h);
    say("XexLoadImage(%s, flags %X) = %08X%s\r\n", path, flags, status, status >= 0 ? "  <- LOADS" : "");
    if (status >= 0 && h != NULL) {
        XexUnloadImage(h);
    }
}

void __cdecl main(void)
{
    static const char *folder = "\\Device\\Harddisk0\\Partition1\\114360\\rev\\40160203_0";
    static const DWORD flags[] = {8, 9, 1};
    static XCONTENT_DATA items[32];
    char path[300];
    HANDLE en = INVALID_HANDLE_VALUE;
    DWORD r, size = 0, got = 0, disposition, license;
    int i, k;

    out = fopen("game:\\pkgtest.log", "w");
    say("pkgtest 5\r\n");
    for (k = 0; k < 3; k++) {
        _snprintf(path, sizeof(path), "%s\\PolCore.xex", folder);
        try_load(path, flags[k]);
    }
    r = XContentCreateEnumerator(XUSER_INDEX_NONE, XCONTENTDEVICE_ANY, 3, 0, 32, &size, &en);
    if (r == ERROR_SUCCESS) {
        XEnumerate(en, items, sizeof(items), &got, NULL);
        CloseHandle(en);
    }
    for (i = 0; i < (int)got; i++) {
        if (strcmp(items[i].szFileName, "R100000") != 0) {
            continue;
        }
        r = XContentCreate(XUSER_INDEX_NONE, "pkc0", &items[i], XCONTENTFLAG_OPENEXISTING, &disposition, &license,
                           NULL);
        say("XContentCreate(R100000) = %08X\r\n", r);
        if (r == ERROR_SUCCESS) {
            for (k = 0; k < 3; k++) {
                try_load("pkc0:\\PolCore.xex", flags[k]);
            }
            XContentClose("pkc0", NULL);
        }
    }
    _snprintf(path, sizeof(path), "%s\\0001\\FFXi.dll", folder);
    try_load(path, 9);
    say("done\r\n");
    if (out != NULL) {
        fclose(out);
    }
}
