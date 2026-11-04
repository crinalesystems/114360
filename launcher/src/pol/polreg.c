/* PlayOnline sign-up client (polreg.h). Two sign-up page sets are known, both on ucs.pol.com:51305:

   OpenLobby (services/ucscgi.py) answers plain HTTP as well. Its six-step wizard is walked with GET requests as
   its pages link them: step 1 (the links carry the sign-up token t=), 4 (leaving the code page: rc0..rc4 hold
   the registration code), 5 (leaving the info page: pw1, pw2, handle), 6 (the account is made; the page names
   the new PlayOnline ID in its "PlayOnline ID" row). A refused step answers with the previous page, the reason
   in its last "note" line.

   Project Crystal (Apache, /pml-cgi-bin/UMENZ001.cgi, kinou_id=20) speaks only SSL 3.0 (ssl3.c). Every screen
   is a POST to that CGI with a `step` and the account's fields: reg (username, email, password,
   passwordConf), mail (the server mails a 6-digit code), code (the code checked), conf (the account is made;
   the page lists its POLID). A refusal comes back as a screen with the reason in
   `<define name="$srvErr" value="...">`. Letters and digits only in the password: "superpass!3" was refused. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "polreg.h"
#include "ssl3.h"

#define UCS_PORT 51305
#define UCS_TIMEOUT_MS 8000
#define PLAIN_PROBE_MS 3000
#define PAGE_MAX 0x10000

static char page[PAGE_MAX];

/* Append `value` to `out`, %-encoded where a query string or form body needs it. */
static void url_add(char *out, int size, const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    int n = (int)strlen(out);
    for (; *value && n < size - 4; value++) {
        unsigned char c = (unsigned char)*value;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'
            || c == '.') {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = '\0';
}

/* One HTTP/1.0 request to the sign-up pages, plain or over SSL 3.0; the answer's body lands in `page`.
   `body` NULL = GET. Returns the body's length, or -1 (no HTTP answer). */
static int request(const POL_NET *net, pol_u32 server, int ssl, const char *path, const char *body, int timeout_ms)
{
    static char req[2048];
    char error[96], *start;
    long sock = 0;
    int len = 0, r, header, content = -1;
    if (body == NULL) {
        _snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: ucs.pol.com\r\nConnection: close\r\n\r\n", path);
    } else {
        _snprintf(req, sizeof(req), "POST %s HTTP/1.0\r\nHost: ucs.pol.com\r\nConnection: close\r\n"
                  "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n\r\n%s",
                  path, (int)strlen(body), body);
    }
    req[sizeof(req) - 1] = '\0';
    if (ssl) {
        if (!ssl3_connect(net, server, UCS_PORT, error, sizeof(error)) || !ssl3_write(req, (int)strlen(req))) {
            ssl3_close();
            return -1;
        }
    } else {
        sock = net->connect(server, UCS_PORT);
        if (sock <= 0) {
            return -1;
        }
        if (net->send(sock, req, (int)strlen(req)) <= 0) {
            net->close(sock);
            return -1;
        }
    }
    for (;;) {
        if (len >= PAGE_MAX - 1) {
            break;
        }
        r = ssl ? ssl3_read(page + len, PAGE_MAX - 1 - len, timeout_ms)
                : net->recv(sock, page + len, PAGE_MAX - 1 - len, timeout_ms);
        if (r <= 0) {
            break;
        }
        len += r;
        page[len] = '\0';
        /* Stop at the end of a body with a Content-Length rather than wait for the close. */
        if (content < 0 && (start = strstr(page, "\r\n\r\n")) != NULL) {
            const char *cl = strstr(page, "Content-Length:");
            header = (int)(start + 4 - page);
            content = cl != NULL && cl < start ? header + atoi(cl + 15) : 0x7FFFFFFF;
        }
        if (content >= 0 && len >= content) {
            break;
        }
    }
    if (ssl) {
        ssl3_close();
    } else {
        net->close(sock);
    }
    page[len] = '\0';
    if (strncmp(page, "HTTP/", 5) != 0 || (start = strstr(page, "\r\n\r\n")) == NULL) {
        return -1;
    }
    start += 4;
    len -= (int)(start - page);
    memmove(page, start, len + 1);
    return len;
}

/* The text of the first <text ...style="STYLE"...>VALUE</text> after `from` (entities left as they are). */
static int text_after(const char *from, const char *style, char *out, int size)
{
    char want[32];
    const char *p, *end;
    int n;
    _snprintf(want, sizeof(want), "style=\"%s\"", style);
    want[sizeof(want) - 1] = '\0';
    if ((p = strstr(from, want)) == NULL || (p = strchr(p, '>')) == NULL || (end = strchr(++p, '<')) == NULL) {
        return 0;
    }
    n = (int)(end - p) < size - 1 ? (int)(end - p) : size - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 1;
}

static void set_error(char *error, int size, const char *text)
{
    _snprintf(error, size, "%s", text);
    error[size - 1] = '\0';
}

/* OpenLobby: the reason for a refused step, its page's last "note" line other than the "* Required" footnote. */
static void note_refusal(char *error, int size, const char *fallback)
{
    const char *p = page;
    char note[160], last[160];
    last[0] = '\0';
    while ((p = strstr(p, "style=\"note\"")) != NULL) {
        if (text_after(p, "note", note, sizeof(note)) && strcmp(note, "* Required") != 0) {
            strcpy(last, note);
        }
        p += 12;
    }
    set_error(error, size, last[0] ? last : fallback);
}

static int register_openlobby(const POL_NET *net, pol_u32 server, const char *handle, const char *password,
                              const char *code, char *pol_id, char *error, int error_size)
{
    char token[40], query[512], label[32], value[32];
    const char *p;
    int i, n, part;
    /* The links carry the sign-up's token ("...&step=2&t=a2531efc..."). */
    if ((p = strstr(page, "&t=")) == NULL) {
        set_error(error, error_size, "the sign-up page gave no token");
        return 0;
    }
    for (p += 3, n = 0; n < (int)sizeof(token) - 1 && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')); p++) {
        token[n++] = *p;
    }
    token[n] = '\0';

    /* Leaving the code page: the code's groups as rc0..rc4. */
    _snprintf(query, sizeof(query), "/pml-cgi-bin/?kinou_id=20&step=4&t=%s", token);
    for (part = 0, p = code; *p && part < 5; part++) {
        char group[16];
        for (i = 0; p[i] && p[i] != '-' && i < (int)sizeof(group) - 1; i++) {
            group[i] = p[i];
        }
        group[i] = '\0';
        _snprintf(query + strlen(query), sizeof(query) - strlen(query), "&rc%d=", part);
        url_add(query, sizeof(query), group);
        p += i;
        if (*p == '-') {
            p++;
        }
    }
    if (request(net, server, 0, query, NULL, UCS_TIMEOUT_MS) < 0 || strstr(page, "name=\"pw1\"") == NULL) {
        note_refusal(error, error_size, "the registration code was refused");
        return 0;
    }

    /* Leaving the info page: password twice and the handle. */
    _snprintf(query, sizeof(query), "/pml-cgi-bin/?kinou_id=20&step=5&t=%s&pw1=", token);
    url_add(query, sizeof(query), password);
    strcat(query, "&pw2=");
    url_add(query, sizeof(query), password);
    strcat(query, "&handle=");
    url_add(query, sizeof(query), handle);
    if (request(net, server, 0, query, NULL, UCS_TIMEOUT_MS) < 0 || strstr(page, "name=\"pw1\"") != NULL) {
        note_refusal(error, error_size, "the password or handle was refused");
        return 0;
    }

    /* Confirm: the account is made; the page names it in its "PlayOnline ID" row. */
    _snprintf(query, sizeof(query), "/pml-cgi-bin/?kinou_id=20&step=6&t=%s", token);
    if (request(net, server, 0, query, NULL, UCS_TIMEOUT_MS) < 0) {
        set_error(error, error_size, "no answer to the confirmation");
        return 0;
    }
    for (p = page; (p = strstr(p, "style=\"label\"")) != NULL; p += 13) {
        if (text_after(p, "label", label, sizeof(label)) && strcmp(label, "PlayOnline ID") == 0
            && text_after(p + 13, "ok", value, sizeof(value)) && value[0] != '(') {
            _snprintf(pol_id, 9, "%s", value);
            pol_id[8] = '\0';
            return 1;
        }
    }
    note_refusal(error, error_size, "the server did not issue a PlayOnline ID");
    return 0;
}

/* The value of `<define name="NAME" value="...">` in the page; "" when absent. Project Crystal writes its line
   breaks as a yen sign + n (Shift-JIS 0x5C shown as a yen): they become spaces. */
static void define_value(const char *name, char *out, int size)
{
    char want[48];
    const char *p, *end;
    int n = 0;
    out[0] = '\0';
    _snprintf(want, sizeof(want), "name=\"%s\"", name);
    want[sizeof(want) - 1] = '\0';
    if ((p = strstr(page, want)) == NULL || (p = strstr(p, "value=\"")) == NULL || (end = strchr(p += 7, '"')) == NULL) {
        return;
    }
    while (p < end && n < size - 1) {
        if (p[0] == '\\' && p[1] == 'n') {
            p += 2;
            if (n > 0 && out[n - 1] != ' ') {
                out[n++] = ' ';
            }
            continue;
        }
        out[n++] = *p++;
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
}

/* Project Crystal's sign-up keeps the account's details in hidden fields from screen to screen; they are kept
   here between pol_register (form, then "send the code") and pol_register_code (the mailed code). */
static char crystal_fields[384];
static pol_u32 crystal_server;

/* The page's PlayOnline ID, if it names one: an 8-character upper-case word after "PlayOnline ID". */
static int page_pol_id(char *pol_id)
{
    const char *p, *q;
    int i, k;
    for (p = page; (p = strstr(p, "PlayOnline ID")) != NULL; p += 13) {
        q = p + 13;
        for (k = 0; k < 200 && q[k]; k++) {
            for (i = 0; i < 8 && ((q[k + i] >= 'A' && q[k + i] <= 'Z') || (q[k + i] >= '0' && q[k + i] <= '9')); i++) {
            }
            if (i == 8 && !(q[k + 8] >= 'A' && q[k + 8] <= 'Z') && !(q[k + 8] >= '0' && q[k + 8] <= '9')
                && (k == 0 || !(q[k - 1] >= 'A' && q[k - 1] <= 'Z'))) {
                memcpy(pol_id, q + k, 8);
                pol_id[8] = '\0';
                return 1;
            }
        }
    }
    return 0;
}

/* Post `step` with the kept fields (and `more`); 1 with the next page in `page`, 0 with the reason. */
static int crystal_post(const POL_NET *net, const char *step, const char *more, char *error, int error_size)
{
    static char body[640];
    char reason[160];
    _snprintf(body, sizeof(body), "step=%s&kinou_id=20&lang=en&%s%s", step, crystal_fields, more);
    body[sizeof(body) - 1] = '\0';
    if (request(net, crystal_server, 1, "/pml-cgi-bin/UMENZ001.cgi", body, UCS_TIMEOUT_MS) < 0) {
        set_error(error, error_size, "no answer from the registration pages");
        return 0;
    }
    define_value("$srvErr", reason, sizeof(reason));
    if (reason[0]) {
        set_error(error, error_size, reason);
        return 0;
    }
    return 1;
}

/* Project Crystal: the form (step reg: username, e-mail, password twice), then step mail, which mails a
   verification code; returns 2 for the caller to ask for it. A refusal comes back as the form with $srvErr. */
static int register_crystal(const POL_NET *net, pol_u32 server, const char *username, const char *password,
                            const char *email, char *error, int error_size)
{
    char more[64];
    crystal_server = server;
    strcpy(crystal_fields, "username=");
    url_add(crystal_fields, sizeof(crystal_fields), username);
    strcat(crystal_fields, "&email=");
    url_add(crystal_fields, sizeof(crystal_fields), email);
    strcat(crystal_fields, "&password=");
    url_add(crystal_fields, sizeof(crystal_fields), password);
    strcpy(more, "&passwordConf=");
    url_add(more, sizeof(more), password);
    if (!crystal_post(net, "reg", more, error, error_size)) {
        return 0;
    }
    if (strstr(page, "\"step\"     value=\"mail\"") == NULL && strstr(page, "\"step\" value=\"mail\"") == NULL) {
        set_error(error, error_size, "the form was taken, but the e-mail step did not follow");
        return 0;
    }
    if (!crystal_post(net, "mail", "", error, error_size)) {
        return 0;
    }
    _snprintf(error, error_size, "a verification code was mailed to %s", email);
    error[error_size - 1] = '\0';
    return 2;
}

/* Project Crystal after the mailed code: step code (the code checked, a confirmation page), then step conf with
   the same fields; the completion page lists the account in an array: `<array>"POLID","ABCD1234"</array>`. */
int pol_register_code(const POL_NET *net, const char *code, char *pol_id, char *error, int error_size)
{
    char more[64];
    const char *p;
    pol_id[0] = '\0';
    if (crystal_fields[0] == '\0') {
        set_error(error, error_size, "no registration is waiting for a code (/register first)");
        return 0;
    }
    strcpy(more, "&code=");
    url_add(more, sizeof(more), code);
    if (!crystal_post(net, "code", more, error, error_size)) {
        return 0;
    }
    if (strstr(page, "\"step\"     value=\"conf\"") == NULL && strstr(page, "\"step\" value=\"conf\"") == NULL) {
        set_error(error, error_size, "the code was not accepted");
        return 0;
    }
    if (!crystal_post(net, "conf", more, error, error_size)) {
        return 0;
    }
    crystal_fields[0] = '\0';
    if ((p = strstr(page, "\"POLID\",\"")) != NULL && strlen(p + 9) >= 9 && p[17] == '"') {
        memcpy(pol_id, p + 9, 8);
        pol_id[8] = '\0';
    } else if (!page_pol_id(pol_id)) {
        set_error(error, error_size, "registered, but the page named no PlayOnline ID (see the server's mail)");
    }
    return 1;
}

int pol_register(const POL_NET *net, pol_u32 server, const char *handle, const char *password, const char *extra,
                 char *pol_id, char *error, int error_size)
{
    pol_id[0] = '\0';
    error[0] = '\0';
    ssl3_seed(password, (int)strlen(password));
    ssl3_seed(handle, (int)strlen(handle));
    ssl3_seed(&server, sizeof(server));
    ssl3_seed(&error, sizeof(error));
    if (request(net, server, 0, "/pml-cgi-bin/?kinou_id=20&step=1", NULL, PLAIN_PROBE_MS) >= 0
        && strstr(page, "&t=") != NULL) {
        return register_openlobby(net, server, handle, password, extra, pol_id, error, error_size);
    }
    if (request(net, server, 1, "/pml-cgi-bin/UMENZ001.cgi?kinou_id=20", NULL, UCS_TIMEOUT_MS) >= 0
        && strstr(page, "UMENZ001.cgi") != NULL) {
        if (!strchr(extra, '@')) {
            set_error(error, error_size, "this server asks for an e-mail address: /register name password e-mail");
            return 0;
        }
        return register_crystal(net, server, handle, password, extra, error, error_size);
    }
    _snprintf(error, error_size, "no sign-up pages on this server (port %d)", UCS_PORT);
    error[error_size - 1] = '\0';
    return 0;
}
