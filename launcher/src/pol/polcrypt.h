#ifndef POLCRYPT_H
#define POLCRYPT_H

/* PlayOnline's sign-in crypto, ported from Project Crystal's POLCommon (SqCrypto.cs, Blowfish.cs; AGPL-3.0,
   work/ref/crystal). Byte-oriented C89 so it runs the same on the 360 (big-endian) and on a PC (the test
   harness, tools/polirc-test.c). */

#ifdef _MSC_VER
typedef unsigned __int64 pol_u64;
#else
typedef unsigned long long pol_u64;
#endif
typedef unsigned int pol_u32;

/* The IRC session cipher: PlayOnline's Blowfish (P and S tables grown from the 8-byte key by MD5) run as a
   keystream with IV (rkey1, rkey2). pol_crypt64 with `restart` 1 starts the stream over from the IV (every IRC
   line, every profile header), 0 continues it. CR and LF are never changed. */
typedef struct {
    pol_u32 p[18];
    pol_u32 s[4][256];
    pol_u32 rkey1, rkey2;
    pol_u32 xl, xr, pos;
    unsigned char key[8];
} POL_CIPHER;

void pol_cipher_init(POL_CIPHER *c, const unsigned char key[8], pol_u32 rkey1, pol_u32 rkey2);
void pol_crypt64(POL_CIPHER *c, unsigned char *buf, int size, int restart);

/* PlayOnline's base32 and base64 alphabets. Encoders return the text length; decoders the byte count. */
int pol_base32_encode(const unsigned char *in, int size, char *out);
int pol_base32_decode(const char *in, int len, unsigned char *out);
int pol_base64_encode(const unsigned char *in, int size, int padding, char *out);
int pol_base64_decode(const char *in, int len, unsigned char *out);

/* The 4-character checksum every IRC line ends with. */
void pol_checksum_str(const unsigned char *data, int size, char out[5]);

/* "UELRRODE9" -> "TEST0001" (the sender of a chat line). */
void pol_unscramble_id(const char *nick, char out[9]);

/* "TEST0001" -> "UELRRODE9", the nick the auth server knows a member by. */
void pol_scramble_id(const char *pol_id, char out[10]);

/* MD5(InitData base32 + password) as 32 lowercase hex digits: the NICK password field. `salt_chars` is how much
   of the base32 the salt takes (0 = all), which depends on the PlayOnline version (polirc.h, POL_VERSION). */
void pol_password_md5(const char *init_base32, int salt_chars, const char *password, char out[33]);

/* Content authentication: the 0x40-byte password the game's lobby login carries (0x26 +0x34), from the
   profile server's random value, the login time and the session's Blowfish key. */
void pol_contents_auth(const unsigned char random[16], pol_u32 login_time, const unsigned char blowkey[8],
                       pol_u32 client_ip_be, unsigned short client_port, unsigned char out[0x40]);

/* The handshake key pair: a fixed 256-bit RSA key (e = 0xFFFF). pol_rsa_modulus_le gives the modulus
   little-endian, as USER sends it; pol_rsa_session_key raises the server's 300 token (32-byte big-endian block)
   to the private exponent and keeps the low 8 bytes, as PolCore does (the session key before byte reversal). */
void pol_rsa_modulus_le(unsigned char out[32]);
void pol_rsa_session_key(const unsigned char in[32], unsigned char out[8]);

#endif
