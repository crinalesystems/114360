/* Development-only module for Xenia Canary's plugin loader (plugins\535107D6\).
   The retail 360 provides the "cdrom0:" drive name. Xenia only provides "game:" and "d:", so the
   PlayOnline boot (default.xex) fails to load cdrom0:\PlayOnline\PolCore.xex. This module adds the missing
   link before the title's main thread runs. Never loaded on hardware. */
#include <xtl.h>

typedef struct _ANSI_STRING_X {
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} ANSI_STRING_X;

NTSYSAPI LONG NTAPI ObCreateSymbolicLink(ANSI_STRING_X *link, ANSI_STRING_X *device);
ULONG __cdecl DbgPrint(const char *format, ...);

static void ansi(ANSI_STRING_X *s, const char *text)
{
    s->Buffer = (PCHAR)text;
    s->Length = (USHORT)strlen(text);
    s->MaximumLength = s->Length + 1;
}

BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        ANSI_STRING_X link, device;
        LONG status;
        ansi(&link, "\\??\\cdrom0:");
        ansi(&device, "\\Device\\Cdrom0");
        status = ObCreateSymbolicLink(&link, &device);
        DbgPrint("114360: xenia_compat attached; cdrom0: -> \\Device\\Cdrom0 status=%08X\n", status);
    }
    return TRUE;
}
