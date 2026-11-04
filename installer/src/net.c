#include "net.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <winhttp.h>

typedef struct {
    HINTERNET session;
    HINTERNET connect;
    HINTERNET request;
    unsigned char *buf;
    size_t buf_size;
    size_t buf_pos;
    uint64_t total_size;
    bool (*sink)(void *ud, const void *data, size_t n);
    void (*progress)(void *ud, uint64_t done, uint64_t total);
    void *ud;
    int http_status;
    char *error;
    size_t error_size;
} net_context_win;

static bool net_get_win(const char *url, uint64_t range_start,
                        bool (*sink)(void *ud, const void *data, size_t n), void *ud,
                        void (*progress)(void *ud, uint64_t done, uint64_t total),
                        int *http_status, char *error, size_t error_size)
{
    net_context_win ctx = {0};
    ctx.sink = sink;
    ctx.progress = progress;
    ctx.ud = ud;
    ctx.error = error;
    ctx.error_size = error_size;

    WCHAR url_wide[2048];
    WCHAR host_wide[256];
    WCHAR path_wide[2048];
    int url_len = MultiByteToWideChar(CP_UTF8, 0, url, -1, url_wide, sizeof(url_wide) / sizeof(url_wide[0]));
    if (url_len <= 0) {
        if (error && error_size > 0) snprintf(error, error_size, "URL too long");
        return false;
    }

    URL_COMPONENTS uc = {sizeof(uc)};
    uc.lpszHostName = host_wide;
    uc.dwHostNameLength = sizeof(host_wide) / sizeof(host_wide[0]);
    uc.lpszUrlPath = path_wide;
    uc.dwUrlPathLength = sizeof(path_wide) / sizeof(path_wide[0]);
    WCHAR extra_wide[2048] = L"";
    uc.lpszExtraInfo = extra_wide;     // the query string: "?id=...&export=download&confirm=t"
    uc.dwExtraInfoLength = sizeof(extra_wide) / sizeof(extra_wide[0]);

    if (!WinHttpCrackUrl(url_wide, wcslen(url_wide), 0, &uc)) {
        if (error && error_size > 0) snprintf(error, error_size, "Bad URL");
        return false;
    }
    if (wcslen(path_wide) + wcslen(extra_wide) + 1 < sizeof(path_wide) / sizeof(path_wide[0])) {
        wcscat(path_wide, extra_wide);
    }

    ctx.session = WinHttpOpen(L"114360-installer", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ctx.session) {
        if (error && error_size > 0) snprintf(error, error_size, "WinHttp init failed");
        return false;
    }

    DWORD timeout = 20000; // 20 seconds
    WinHttpSetTimeouts(ctx.session, timeout, timeout, timeout, 60000);

    DWORD secure = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    ctx.connect = WinHttpConnect(ctx.session, uc.lpszHostName, uc.nPort, 0);
    if (!ctx.connect) {
        if (error && error_size > 0) snprintf(error, error_size, "Connection failed");
        WinHttpCloseHandle(ctx.session);
        return false;
    }

    DWORD flags = WINHTTP_FLAG_REFRESH | secure;
    ctx.request = WinHttpOpenRequest(ctx.connect, L"GET", uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!ctx.request) {
        if (error && error_size > 0) snprintf(error, error_size, "Request creation failed");
        WinHttpCloseHandle(ctx.connect);
        WinHttpCloseHandle(ctx.session);
        return false;
    }

    if (range_start > 0) {
        WCHAR range[64];
        swprintf(range, sizeof(range) / sizeof(range[0]), L"Range: bytes=%llu-", (unsigned long long)range_start);
        WinHttpAddRequestHeaders(ctx.request, range, -1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (!WinHttpSendRequest(ctx.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)) {
        if (error && error_size > 0) snprintf(error, error_size, "Send request failed");
        WinHttpCloseHandle(ctx.request);
        WinHttpCloseHandle(ctx.connect);
        WinHttpCloseHandle(ctx.session);
        return false;
    }

    if (!WinHttpReceiveResponse(ctx.request, NULL)) {
        if (error && error_size > 0) snprintf(error, error_size, "Receive response failed");
        WinHttpCloseHandle(ctx.request);
        WinHttpCloseHandle(ctx.connect);
        WinHttpCloseHandle(ctx.session);
        return false;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(ctx.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             NULL, &status_code, &status_size, NULL)) {
        if (error && error_size > 0) snprintf(error, error_size, "Query status failed");
        WinHttpCloseHandle(ctx.request);
        WinHttpCloseHandle(ctx.connect);
        WinHttpCloseHandle(ctx.session);
        return false;
    }

    if (http_status) *http_status = (int)status_code;

    if (status_code != 200 && status_code != 206) {
        if (error && error_size > 0) snprintf(error, error_size, "HTTP %lu", status_code);
        WinHttpCloseHandle(ctx.request);
        WinHttpCloseHandle(ctx.connect);
        WinHttpCloseHandle(ctx.session);
        return false;
    }

    DWORD content_len = 0;
    DWORD content_len_size = sizeof(content_len);
    if (WinHttpQueryHeaders(ctx.request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            NULL, &content_len, &content_len_size, NULL)) {
        ctx.total_size = content_len;
    }

    DWORD bytes_read;
    unsigned char buffer[65536];
    uint64_t total_read = 0;
    bool ok = true;

    for (;;) {
        if (!WinHttpReadData(ctx.request, buffer, sizeof(buffer), &bytes_read)) {
            // The connection dropped or stalled past the receive timeout: not a complete download.
            if (error && error_size > 0) snprintf(error, error_size, "download interrupted (error %lu)", GetLastError());
            ok = false;
            break;
        }
        if (bytes_read == 0) break;

        if (!sink(ud, buffer, bytes_read)) {
            if (error && error_size > 0) snprintf(error, error_size, "Sink cancelled");
            ok = false;
            break;
        }

        total_read += bytes_read;
        if (progress) progress(ud, total_read, ctx.total_size > 0 ? ctx.total_size : total_read);
    }

    WinHttpCloseHandle(ctx.request);
    WinHttpCloseHandle(ctx.connect);
    WinHttpCloseHandle(ctx.session);
    return ok;
}

#else  /* Linux with libcurl */

#include <curl/curl.h>

typedef struct {
    bool (*sink)(void *ud, const void *data, size_t n);
    void (*progress)(void *ud, uint64_t done, uint64_t total);
    void *ud;
    uint64_t total_size;
    uint64_t downloaded;
    char *error;
    size_t error_size;
} net_context_curl;

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    net_context_curl *ctx = (net_context_curl *)userp;

    if (!ctx->sink(ctx->ud, (const unsigned char *)ptr, realsize)) {
        return 0; /* Returning 0 signals an error to libcurl */
    }

    ctx->downloaded += realsize;
    if (ctx->progress) {
        ctx->progress(ctx->ud, ctx->downloaded, ctx->total_size);
    }

    return realsize;
}

static int curl_progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow)
{
    net_context_curl *ctx = (net_context_curl *)clientp;
    ctx->total_size = (uint64_t)dltotal;
    (void)ultotal;
    (void)ulnow;
    return 0;
}

static bool net_get_curl(const char *url, uint64_t range_start,
                         bool (*sink)(void *ud, const void *data, size_t n), void *ud,
                         void (*progress)(void *ud, uint64_t done, uint64_t total),
                         int *http_status, char *error, size_t error_size)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        if (error && error_size > 0) snprintf(error, error_size, "libcurl init failed");
        return false;
    }

    net_context_curl ctx = {0};
    ctx.sink = sink;
    ctx.progress = progress;
    ctx.ud = ud;
    ctx.error = error;
    ctx.error_size = error_size;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "114360-installer");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    /* No total timeout (a 1 GB pack takes a while); a transfer under 1 byte/s for 60 s counts as stalled. */
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (range_start > 0) {
        char range_str[64];
        snprintf(range_str, sizeof(range_str), "%llu-", (unsigned long long)range_start);
        curl_easy_setopt(curl, CURLOPT_RANGE, range_str);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)&ctx);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "curl error: %s", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
        return false;
    }

    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (http_status) *http_status = (int)response_code;

    if (response_code != 200 && response_code != 206) {
        if (error && error_size > 0) snprintf(error, error_size, "HTTP %ld", response_code);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_cleanup(curl);
    return true;
}

#endif

/* Main dispatch and convenience function */

bool net_get(const char *url, uint64_t range_start,
             bool (*sink)(void *ud, const void *data, size_t n), void *ud,
             void (*progress)(void *ud, uint64_t done, uint64_t total),
             int *http_status, char *error, size_t error_size)
{
#ifdef _WIN32
    return net_get_win(url, range_start, sink, ud, progress, http_status, error, error_size);
#else
    return net_get_curl(url, range_start, sink, ud, progress, http_status, error, error_size);
#endif
}

/* Memory sink for convenience function */
typedef struct {
    unsigned char *buf;
    size_t pos;
    size_t cap;
} mem_sink_ctx;

static bool mem_sink(void *ud, const void *data, size_t n)
{
    mem_sink_ctx *ctx = (mem_sink_ctx *)ud;
    if (ctx->pos + n > ctx->cap) {
        return false; /* Cap exceeded */
    }
    memcpy(ctx->buf + ctx->pos, data, n);
    ctx->pos += n;
    return true;
}

bool net_get_memory(const char *url, unsigned char **buf_out, size_t *len_out,
                    void (*progress)(void *ud, uint64_t done, uint64_t total), void *ud,
                    int *http_status, char *error, size_t error_size)
{
    if (!buf_out || !len_out) {
        if (error && error_size > 0) snprintf(error, error_size, "Invalid arguments");
        return false;
    }

    const size_t MAX_SIZE = 64 * 1024 * 1024; /* 64 MiB cap */
    unsigned char *buf = malloc(MAX_SIZE);
    if (!buf) {
        if (error && error_size > 0) snprintf(error, error_size, "Out of memory");
        return false;
    }

    mem_sink_ctx ctx = {
        .buf = buf,
        .pos = 0,
        .cap = MAX_SIZE,
    };

    bool success = net_get(url, 0, mem_sink, &ctx, progress, http_status, error, error_size);
    if (!success) {
        free(buf);
        return false;
    }

    *buf_out = buf;
    *len_out = ctx.pos;
    return true;
}
