/* SHA-1 (sha1.h). Words are `unsigned int`, 32 bits on the console and on the PC test builds. */
#include "sha1.h"

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_block(unsigned int h[5], const unsigned char *p)
{
    unsigned int w[80], a, b, c, d, e, f, k, t;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = (unsigned int)p[4 * i] << 24 | (unsigned int)p[4 * i + 1] << 16 | (unsigned int)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (i = 16; i < 80; i++) {
        t = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = ROL(t, 1);
    }
    a = h[0];
    b = h[1];
    c = h[2];
    d = h[3];
    e = h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        t = ROL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROL(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void sha1(const void *data, unsigned int size, unsigned char digest[20])
{
    unsigned int h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    const unsigned char *p = (const unsigned char *)data;
    unsigned char tail[128];
    unsigned int left = size, n, i;
    unsigned int bits_hi = size >> 29, bits_lo = size << 3;
    while (left >= 64) {
        sha1_block(h, p);
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
    for (i = 0; i < 4; i++) {
        tail[n - 8 + i] = (unsigned char)(bits_hi >> (24 - 8 * i));
        tail[n - 4 + i] = (unsigned char)(bits_lo >> (24 - 8 * i));
    }
    sha1_block(h, tail);
    if (n == 128) {
        sha1_block(h, tail + 64);
    }
    for (i = 0; i < 20; i++) {
        digest[i] = (unsigned char)(h[i / 4] >> (24 - 8 * (i % 4)));
    }
}
