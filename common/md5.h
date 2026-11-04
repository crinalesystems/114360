#ifndef MD5_H
#define MD5_H

/* MD5 of `size` bytes at `data` into `digest` (RFC 1321). */
void md5(const void *data, unsigned int size, unsigned char digest[16]);

/* The same from initial state `iv` (four 32-bit words; NULL = the standard one). */
void md5_iv(const void *data, unsigned int size, unsigned char digest[16], const unsigned long *iv);

#endif
