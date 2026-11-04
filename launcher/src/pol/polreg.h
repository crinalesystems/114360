/* New PlayOnline IDs through a server's sign-up pages (ucs.pol.com, port 51305), the way the Viewer's
   "PlayOnline registration" does it. Two page sets are known (polreg.c): OpenLobby's six-step wizard (plain
   HTTP) and Project Crystal's form with an e-mailed verification code (SSL 3.0 only, ssl3.c). Portable like
   polirc.c: sockets come through POL_NET. */
#ifndef POLREG_H
#define POLREG_H

#include "polirc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register on `server` (IPv4, host order): `name` is the handle (OpenLobby) or the user name (Project
   Crystal), `password` 8-15 letters and digits, `extra` the registration code for OpenLobby
   ("XXXX-XXXX-..."; "" for a server that asks for none) or the e-mail address for Project Crystal. Blocking.
   Returns 1 with the new PlayOnline ID in `pol_id` (at least 9 bytes; "" when the server's page did not name
   it, with a note in `error`); 2 when the server mailed a verification code (Project Crystal; `error` says
   where), to be given to pol_register_code; else 0 with the server's or the network's reason in `error`. */
int pol_register(const POL_NET *net, pol_u32 server, const char *name, const char *password, const char *extra,
                 char *pol_id, char *error, int error_size);

/* Finish a registration that returned 2 with the mailed `code`. Same results as pol_register (1 or 0). */
int pol_register_code(const POL_NET *net, const char *code, char *pol_id, char *error, int error_size);

#ifdef __cplusplus
}
#endif

#endif
