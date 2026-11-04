/* POL_NET (polirc.h) over the console's sockets: plain TCP to a PlayOnline server, which is not an Xbox Live
   peer, so the connections are insecure ones (XNET_STARTUP_BYPASS_SECURITY, SO_MARKINSECURE). Sockets are
   non-blocking and waited on with select: Xenia's select can call a socket readable with nothing there, and a
   blocking recv would then hang the menu. */
#include <xtl.h>
#include <winsockx.h>
#include "polnet.h"

/* Not in the public XDK headers; the value the system's own insecure-socket users pass (xkelib, Freestyle). */
#ifndef SO_MARKINSECURE
#define SO_MARKINSECURE 0x5801
#endif
/* select's first argument: the console ignores it, but Xenia hands it to the host's select, where it bounds
   the host descriptor numbers looked at (and 0 looks at none, so every wait timed out). */
#define SELECT_NFDS 1024

static int started;

/* The last call would block (or a connect is under way). The console reports WSAEWOULDBLOCK; Xenia passes the
   host's errno through unchanged, so Linux EAGAIN (11) and EINPROGRESS (115) count too. */
static int would_block(void)
{
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == 11 || e == 115;
}

static int net_start(void)
{
    XNetStartupParams xnsp;
    WSADATA wsa;
    if (started) {
        return 1;
    }
    memset(&xnsp, 0, sizeof(xnsp));
    xnsp.cfgSizeOfStruct = sizeof(xnsp);
    xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
    XNetStartup(&xnsp);               /* fails harmlessly when the system already started it */
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 0;
    }
    started = 1;
    return 1;
}

static long net_connect(pol_u32 ip, unsigned short port)
{
    SOCKET s;
    struct sockaddr_in a;
    BOOL yes = TRUE;
    u_long nonblocking = 1;
    fd_set wr;
    struct timeval tv;
    if (!net_start()) {
        return -1;
    }
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return -1;
    }
    setsockopt(s, SOL_SOCKET, SO_MARKINSECURE, (const char *)&yes, sizeof(yes));
    ioctlsocket(s, FIONBIO, &nonblocking);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(ip);
    if (connect(s, (struct sockaddr *)&a, sizeof(a)) != 0 && !would_block()) {
        closesocket(s);
        return -1;
    }
    FD_ZERO(&wr);
    FD_SET(s, &wr);
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (select(SELECT_NFDS, NULL, &wr, NULL, &tv) <= 0) {
        closesocket(s);
        return -1;
    }
    return (long)s;
}

static int net_send(long sock, const void *buf, int len)
{
    const char *p = (const char *)buf;
    int done = 0, tries = 0;
    while (done < len && tries < 200) {
        int r = send((SOCKET)sock, p + done, len - done, 0);
        if (r > 0) {
            done += r;
        } else if (r < 0 && would_block()) {
            Sleep(10);
            tries++;
        } else {
            return -1;
        }
    }
    return done;
}

static int net_recv(long sock, void *buf, int len, int timeout_ms)
{
    fd_set rd;
    struct timeval tv;
    int r;
    FD_ZERO(&rd);
    FD_SET((SOCKET)sock, &rd);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (select(SELECT_NFDS, &rd, NULL, NULL, &tv) <= 0) {
        return 0;
    }
    r = recv((SOCKET)sock, (char *)buf, len, 0);
    if (r > 0) {
        return r;
    }
    if (r < 0 && would_block()) {
        return 0;               /* "readable" with nothing there (Xenia) */
    }
    return -1;
}

static void net_close(long sock)
{
    closesocket((SOCKET)sock);
}

const POL_NET pol_console_net = {net_connect, net_send, net_recv, net_close};
