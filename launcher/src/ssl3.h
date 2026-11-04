/* A minimal SSL 3.0 client: RSA key exchange with SSL_RSA_WITH_RC4_128_MD5, the one suite the PlayOnline
   sign-up servers speak (ucs.pol.com:51305; Project Crystal answers nothing else, OpenLobby takes it through its
   SSL 3.0 terminator). The server's certificate is not verified: the PlayOnline certificates are long expired
   and self-made, and the Viewer only checks the chain against its own store. Portable like polirc.c: sockets
   come through POL_NET. One connection at a time (static buffers). */
#ifndef SSL3_H
#define SSL3_H

#include "polirc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Connect to `ip` (host order) and `port` and do the handshake. Returns 1, or 0 with the reason in `error`. */
int ssl3_connect(const POL_NET *net, pol_u32 ip, unsigned short port, char *error, int error_size);

/* Send application data; returns 1 when all of it went out. */
int ssl3_write(const void *buf, int len);

/* Up to `len` bytes of application data, waiting at most `timeout_ms` for a record: > 0 bytes read, 0 when
   nothing came in time, -1 when the connection is closed (close_notify, an alert, or the socket). */
int ssl3_read(void *buf, int len, int timeout_ms);

void ssl3_close(void);

/* Mix caller entropy (time, addresses) into the generator for client randoms and the pre-master secret. */
void ssl3_seed(const void *data, int len);

#ifdef __cplusplus
}
#endif

#endif
