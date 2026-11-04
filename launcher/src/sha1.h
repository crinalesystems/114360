#ifndef SHA1_H
#define SHA1_H

/* SHA-1 of `size` bytes at `data` into `digest` (FIPS 180-1), for SSL 3.0 (ssl3.c). */
void sha1(const void *data, unsigned int size, unsigned char digest[20]);

#endif
