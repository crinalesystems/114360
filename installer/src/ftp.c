// FTP client for the installer; see ftp.h.
#define _DEFAULT_SOURCE // getaddrinfo under -std=c23
#include "ftp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCKET INVALID_SOCKET
#define close_socket closesocket
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCKET (-1)
#define close_socket close
#endif

#define BLOCK (256 * 1024)
#define TIMEOUT_S 60

struct ftp {
    sock_t control;
    char host[256], user[128], password[128];
    int port;
    char in[4096];
    size_t in_have;
    char reply[1024];   // the last reply's text, for error messages
};

static void net_start(void)
{
#ifdef _WIN32
    static bool started;
    if (!started) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        started = true;
    }
#endif
}

static sock_t dial(const char *host, int port, char *error, size_t error_size)
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *ai = nullptr;
    char service[16];
    snprintf(service, sizeof(service), "%d", port);
    if (getaddrinfo(host, service, &hints, &ai) != 0 || !ai) {
        snprintf(error, error_size, "FTP: cannot find %s", host);
        return BAD_SOCKET;
    }
    sock_t s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s != BAD_SOCKET) {
#ifdef _WIN32
        DWORD tv = TIMEOUT_S * 1000;
#else
        struct timeval tv = {TIMEOUT_S, 0};
#endif
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
        if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) != 0) {
            close_socket(s);
            s = BAD_SOCKET;
        }
    }
    freeaddrinfo(ai);
    if (s == BAD_SOCKET) {
        snprintf(error, error_size, "FTP: cannot connect to %s:%d", host, port);
    }
    return s;
}

static bool send_all(sock_t s, const void *data, size_t len)
{
    const char *p = data;
    while (len) {
        int n = send(s, p, len > BLOCK ? BLOCK : (int)len, 0);
        if (n <= 0) {
            return false;
        }
        p += n;
        len -= (size_t)n;
    }
    return true;
}

// One line of the control connection (CR LF removed), or false when it is gone.
static bool read_line(ftp *f, char *line, size_t size)
{
    for (;;) {
        char *nl = memchr(f->in, '\n', f->in_have);
        if (nl) {
            size_t n = (size_t)(nl - f->in);
            size_t copy = n < size - 1 ? n : size - 1;
            memcpy(line, f->in, copy);
            line[copy] = 0;
            if (copy && line[copy - 1] == '\r') {
                line[copy - 1] = 0;
            }
            memmove(f->in, nl + 1, f->in_have - n - 1);
            f->in_have -= n + 1;
            return true;
        }
        if (f->in_have == sizeof(f->in)) {
            f->in_have = 0;          // an overlong line: drop it
        }
        int got = recv(f->control, f->in + f->in_have, (int)(sizeof(f->in) - f->in_have), 0);
        if (got <= 0) {
            return false;
        }
        f->in_have += (size_t)got;
    }
}

// The next reply's code (multi-line replies "NNN-" ... "NNN " read whole), or -1 when the connection is gone.
static int reply(ftp *f)
{
    char line[1024];
    if (!read_line(f, line, sizeof(line)) || strlen(line) < 3) {
        return -1;
    }
    snprintf(f->reply, sizeof(f->reply), "%s", line);
    int code = atoi(line);
    if (line[3] == '-') {
        char end[5];
        snprintf(end, sizeof(end), "%.3s ", line);
        do {
            if (!read_line(f, line, sizeof(line))) {
                return -1;
            }
        } while (strncmp(line, end, 4) != 0);
    }
    return code;
}

// Send a command and read its reply: the code, or -1 when the connection is gone.
static int command(ftp *f, const char *fmt, ...)
{
    char line[1100];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof(line) - 2) {
        return -1;
    }
    memcpy(line + n, "\r\n", 3);
    if (f->control == BAD_SOCKET || !send_all(f->control, line, (size_t)n + 2)) {
        return -1;
    }
    return reply(f);
}

static bool failed(ftp *f, int code, const char *what, char *error, size_t error_size)
{
    if (code < 0) {
        snprintf(error, error_size, "FTP: connection to %s lost (%s)", f->host, what);
    } else {
        snprintf(error, error_size, "FTP: %s refused: %s", what, f->reply);
    }
    return false;
}

static bool sign_in(ftp *f, char *error, size_t error_size)
{
    f->in_have = 0;
    f->control = dial(f->host, f->port, error, error_size);
    if (f->control == BAD_SOCKET) {
        return false;
    }
    int code = reply(f);
    if (code != 220) {
        return failed(f, code, "greeting", error, error_size);
    }
    code = command(f, "USER %s", f->user);
    if (code == 331) {
        code = command(f, "PASS %s", f->password);
    }
    if (code != 230) {
        return failed(f, code, "sign-in", error, error_size);
    }
    code = command(f, "TYPE I");
    if (code / 100 != 2) {
        return failed(f, code, "TYPE I", error, error_size);
    }
    return true;
}

ftp *ftp_open(const char *host, int port, const char *user, const char *password, char *error, size_t error_size)
{
    net_start();
    ftp *f = calloc(1, sizeof(*f));
    if (!f) {
        snprintf(error, error_size, "out of memory");
        return nullptr;
    }
    f->control = BAD_SOCKET;
    snprintf(f->host, sizeof(f->host), "%s", host);
    snprintf(f->user, sizeof(f->user), "%s", user);
    snprintf(f->password, sizeof(f->password), "%s", password);
    f->port = port;
    if (!sign_in(f, error, error_size)) {
        ftp_close(f);
        return nullptr;
    }
    return f;
}

void ftp_close(ftp *f)
{
    if (!f) {
        return;
    }
    if (f->control != BAD_SOCKET) {
        command(f, "QUIT");
        close_socket(f->control);
    }
    free(f);
}

bool ftp_reopen(ftp *f, char *error, size_t error_size)
{
    if (f->control != BAD_SOCKET) {
        close_socket(f->control);
        f->control = BAD_SOCKET;
    }
    return sign_in(f, error, error_size);
}

// Passive mode: a data connection to the address the server names. The port comes from the 227 reply; the
// address is the control connection's (as Python's ftplib does), since a console behind NAT may name its own.
static sock_t passive(ftp *f, char *error, size_t error_size)
{
    int code = command(f, "PASV");
    unsigned h[4], p[2];
    const char *open = strchr(f->reply, '(');
    if (code != 227 || !open || sscanf(open, "(%u,%u,%u,%u,%u,%u)", &h[0], &h[1], &h[2], &h[3], &p[0], &p[1]) != 6) {
        failed(f, code, "PASV", error, error_size);
        return BAD_SOCKET;
    }
    return dial(f->host, (int)(p[0] << 8 | p[1]), error, error_size);
}

// Read a data connection to its end into a malloc'd buffer (NUL-terminated).
static char *drain(sock_t data, size_t *len)
{
    size_t cap = 65536, n = 0;
    char *buf = malloc(cap + 1);
    for (int got; buf && (got = recv(data, buf + n, (int)(cap - n), 0)) > 0;) {
        n += (size_t)got;
        if (n == cap) {
            char *grown = realloc(buf, cap * 2 + 1);
            if (!grown) {
                free(buf);
                buf = nullptr;
                break;
            }
            buf = grown;
            cap *= 2;
        }
    }
    close_socket(data);
    if (buf) {
        buf[n] = 0;
    }
    *len = n;
    return buf;
}

int ftp_list(ftp *f, const char *folder, ftp_entry **out, char *error, size_t error_size)
{
    *out = nullptr;
    sock_t data = passive(f, error, error_size);
    if (data == BAD_SOCKET) {
        return -1;
    }
    int code = command(f, "LIST %s", folder);
    if (code < 0) {
        close_socket(data);
        failed(f, code, "LIST", error, error_size);
        return -1;
    }
    if (code / 100 != 1) {
        close_socket(data);
        return 0;               // 450/550: no such folder, so nothing in it
    }
    size_t len;
    char *text = drain(data, &len);
    code = reply(f);
    if (code < 0 || !text) {
        free(text);
        failed(f, code, "LIST", error, error_size);
        return -1;
    }
    int count = 0, cap = 0;
    ftp_entry *entries = nullptr;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(nullptr, "\r\n")) {
        // "drwxr-xr-x 1 owner group 4096 Nov 03 17:40 name with spaces": nine fields, the name is the rest.
        char *field[9], *p = line;
        int n = 0;
        while (n < 9 && *p) {
            while (*p == ' ') {
                p++;
            }
            if (!*p) {
                break;
            }
            field[n++] = p;
            if (n == 9) {
                break;
            }
            while (*p && *p != ' ') {
                p++;
            }
            if (*p) {
                *p++ = 0;
            }
        }
        if (n < 9 || strcmp(field[8], ".") == 0 || strcmp(field[8], "..") == 0) {
            continue;
        }
        if (count == cap) {
            cap = cap ? cap * 2 : 64;
            ftp_entry *grown = realloc(entries, sizeof(*entries) * (size_t)cap);
            if (!grown) {
                break;
            }
            entries = grown;
        }
        entries[count++] = (ftp_entry){strdup(field[8]), strtoull(field[4], nullptr, 10), field[0][0] == 'd'};
    }
    free(text);
    *out = entries;
    return count;
}

void ftp_entries_free(ftp_entry *entries, int count)
{
    for (int i = 0; i < count; i++) {
        free(entries[i].name);
    }
    free(entries);
}

bool ftp_mkdir(ftp *f, const char *folder, char *error, size_t error_size)
{
    int code = command(f, "MKD %s", folder);
    return code >= 0 || failed(f, code, "MKD", error, error_size);   // 550 "exists" (or a drive root) is fine
}

bool ftp_store(ftp *f, const char *path, uint64_t size, ftp_reader read, void *ud, char *error, size_t error_size)
{
    static unsigned char buf[BLOCK];
    sock_t data = passive(f, error, error_size);
    if (data == BAD_SOCKET) {
        return false;
    }
    int code = command(f, "STOR %s", path);
    if (code / 100 != 1) {
        close_socket(data);
        return failed(f, code, "STOR", error, error_size);
    }
    bool ok = true;
    while (ok && size) {
        size_t n = size > sizeof(buf) ? sizeof(buf) : (size_t)size;
        if (!read(ud, buf, n)) {
            snprintf(error, error_size, "cannot read the source of %s", path);
            ok = false;
        } else if (!send_all(data, buf, n)) {
            snprintf(error, error_size, "FTP: upload of %s broke off", path);
            ok = false;
        }
        size -= n;
    }
#ifdef _WIN32
    shutdown(data, SD_SEND);
#else
    shutdown(data, SHUT_WR);
#endif
    close_socket(data);
    code = reply(f);
    if (ok && code / 100 != 2) {
        return failed(f, code, "STOR", error, error_size);
    }
    return ok;
}

unsigned char *ftp_fetch(ftp *f, const char *path, size_t *len, bool *missing, char *error, size_t error_size)
{
    *missing = false;
    sock_t data = passive(f, error, error_size);
    if (data == BAD_SOCKET) {
        return nullptr;
    }
    int code = command(f, "RETR %s", path);
    if (code / 100 != 1) {
        close_socket(data);
        *missing = code >= 400;
        failed(f, code, "RETR", error, error_size);
        return nullptr;
    }
    char *text = drain(data, len);
    code = reply(f);
    if (code / 100 != 2) {
        free(text);
        failed(f, code, "RETR", error, error_size);
        return nullptr;
    }
    return (unsigned char *)text;
}

bool ftp_rename(ftp *f, const char *from, const char *to, char *error, size_t error_size)
{
    int code = command(f, "RNFR %s", from);
    if (code != 350) {
        return failed(f, code, "RNFR", error, error_size);
    }
    code = command(f, "RNTO %s", to);
    return code / 100 == 2 || failed(f, code, "RNTO", error, error_size);
}

bool ftp_delete(ftp *f, const char *path, char *error, size_t error_size)
{
    int code = command(f, "DELE %s", path);
    return code / 100 == 2 || failed(f, code, "DELE", error, error_size);
}

bool ftp_rmdir(ftp *f, const char *path, char *error, size_t error_size)
{
    int code = command(f, "RMD %s", path);
    return code / 100 == 2 || failed(f, code, "RMD", error, error_size);
}
