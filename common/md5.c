/* MD5 (RFC 1321) of one buffer, for the lobby packets' 16-byte hash field. Byte-oriented, so it gives the same
   result on the big-endian PPC as on a PC. The XDK headers do not declare the kernel's XeCryptMd5*. */
#include "md5.h"

#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))
#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void md5_block(unsigned long h[4], const unsigned char *p)
{
    static const unsigned long k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const unsigned char s[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21,
        6, 10, 15, 21, 6, 10, 15, 21};
    unsigned long x[16], a = h[0], b = h[1], c = h[2], d = h[3], t;
    int i;
    for (i = 0; i < 16; i++) {
        x[i] = (unsigned long)p[i * 4] | ((unsigned long)p[i * 4 + 1] << 8) | ((unsigned long)p[i * 4 + 2] << 16)
               | ((unsigned long)p[i * 4 + 3] << 24);
    }
    for (i = 0; i < 64; i++) {
        unsigned long f;
        int g;
        if (i < 16) {
            f = F(b, c, d);
            g = i;
        } else if (i < 32) {
            f = G(b, c, d);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = H(b, c, d);
            g = (3 * i + 5) & 15;
        } else {
            f = I(b, c, d);
            g = (7 * i) & 15;
        }
        t = d;
        d = c;
        c = b;
        f = (a + f + k[i] + x[g]) & 0xFFFFFFFFUL;
        b = (b + ROTL(f, s[i])) & 0xFFFFFFFFUL;
        a = t;
    }
    h[0] = (h[0] + a) & 0xFFFFFFFFUL;
    h[1] = (h[1] + b) & 0xFFFFFFFFUL;
    h[2] = (h[2] + c) & 0xFFFFFFFFUL;
    h[3] = (h[3] + d) & 0xFFFFFFFFUL;
}

void md5(const void *data, unsigned int size, unsigned char digest[16])
{
    md5_iv(data, size, digest, 0);
}

/* md5 with initial state `iv` (four words) instead of the standard one; NULL = standard. PlayOnline's MD5
   leaves its state at zero after a finish (MD5Hash.Reset(doSqBug) in Project Crystal), so hashes made after the
   first one start from there. */
void md5_iv(const void *data, unsigned int size, unsigned char digest[16], const unsigned long *iv)
{
    unsigned long h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    const unsigned char *p = (const unsigned char *)data;
    unsigned char tail[128];
    unsigned int left = size, n, i;
    unsigned long bits = (unsigned long)size * 8;
    if (iv != 0) {
        for (i = 0; i < 4; i++) {
            h[i] = iv[i];
        }
    }
    while (left >= 64) {
        md5_block(h, p);
        p += 64;
        left -= 64;
    }
    for (i = 0; i < left; i++) {
        tail[i] = p[i];
    }
    tail[left] = 0x80;
    n = left < 56 ? 64 : 128;
    for (i = left + 1; i < n - 8; i++) {
        tail[i] = 0;
    }
    for (i = 0; i < 8; i++) {
        tail[n - 8 + i] = i < 4 ? (unsigned char)(bits >> (8 * i)) : 0;
    }
    md5_block(h, tail);
    if (n == 128) {
        md5_block(h, tail + 64);
    }
    for (i = 0; i < 16; i++) {
        digest[i] = (unsigned char)(h[i / 4] >> (8 * (i % 4)));
    }
}
