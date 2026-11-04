#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* HTTPS GET with resume support.
   - Follows redirects.
   - User agent: "114360-installer"
   - Timeouts: connect 20s, stall 60s
   - If range_start > 0, sends Range: bytes=N-
   - sink: called with data chunks; return false to abort
   - progress: called with (done, total) bytes; NULL ok
   - http_status: set to response code (200, 206, 404, etc.; NULL ok)
   - error: if non-NULL and sized, filled with error message on failure
   - Returns true on success (HTTP 200/206), false on error (and error message if provided).
*/
bool net_get(const char *url, uint64_t range_start,
             bool (*sink)(void *ud, const void *data, size_t n), void *ud,
             void (*progress)(void *ud, uint64_t done, uint64_t total),
             int *http_status, char *error, size_t error_size);

/* Convenience: fetch entire URL into memory (malloc'd buffer, cap 64 MiB).
   - Sets *buf_out and *len_out.
   - Returns true on success, false on error.
*/
bool net_get_memory(const char *url, unsigned char **buf_out, size_t *len_out,
                    void (*progress)(void *ud, uint64_t done, uint64_t total), void *ud,
                    int *http_status, char *error, size_t error_size);

#endif
