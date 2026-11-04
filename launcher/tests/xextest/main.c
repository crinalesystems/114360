/* Console load test: the smallest program built and converted exactly like the launcher (same XDK, same
   xex.xml: FFXI title id, 32 MB workspace, insecure sockets). If the console starts it, it writes
   game:\xextest-ok.txt (the folder it was started from) and shows a message box, then returns to the dashboard.
   That tells apart "the console refuses our XEX files" from "the launcher's own code stops". */
#include <xtl.h>
#include <stdio.h>

void __cdecl main(void)
{
    static WCHAR text[] = L"xextest started: this XEX format loads on the console.";
    static LPCWSTR buttons[] = {L"OK"};
    MESSAGEBOX_RESULT result;
    XOVERLAPPED overlapped;
    FILE *f = fopen("game:\\xextest-ok.txt", "w");
    if (f != NULL) {
        fputs("xextest started\r\n", f);
        fclose(f);
    }
    memset(&overlapped, 0, sizeof(overlapped));
    if (XShowMessageBoxUI(0, L"114360 xextest", text, 1, buttons, 0, XMB_ALERTICON, &result, &overlapped)
        == ERROR_IO_PENDING) {
        while (!XHasOverlappedIoCompleted(&overlapped)) {
            Sleep(100);
        }
    } else {
        Sleep(3000);
    }
}
