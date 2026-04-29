#ifndef CRYPTO_H
#define CRYPTO_H
/* crypto.h — primitif kriptografi untuk TLS 1.3
 * SHA-256, HMAC-SHA256, HKDF, AES-128, AES-128-GCM, X25519
 * Semua implementasi pure integer (tanpa SSE/float). */
#include <stdint.h>

/* ── SHA-256 ─────────────────────────────────────────────── */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
} Sha256;

void sha256_init   (Sha256 *ctx);
void sha256_update (Sha256 *ctx, const uint8_t *data, uint32_t len);
void sha256_final  (Sha256 *ctx, uint8_t digest[32]);
void sha256        (const uint8_t *data, uint32_t len, uint8_t digest[32]);

/* ── HMAC-SHA256 ──────────────────────────────────────────── */
void hmac_sha256(const uint8_t *key, uint32_t klen,
                 const uint8_t *msg, uint32_t mlen,
                 uint8_t out[32]);

/* ── HKDF-SHA256 ──────────────────────────────────────────── */
/* Extract: prk = HMAC-SHA256(salt, ikm) */
void hkdf_extract(const uint8_t *salt, uint32_t slen,
                  const uint8_t *ikm,  uint32_t ilen,
                  uint8_t prk[32]);

/* Expand: olen bytes dari PRK + info */
void hkdf_expand(const uint8_t *prk,
                 const uint8_t *info, uint32_t ilen,
                 uint8_t *out, uint32_t olen);

/* TLS 1.3 HKDF-Expand-Label(secret, label, context, len)
 * label harus berupa string C (tanpa "tls13 " prefix — ditambahkan otomatis) */
void hkdf_expand_label(const uint8_t secret[32],
                       const char    *label,
                       const uint8_t *ctx,  uint32_t ctx_len,
                       uint8_t       *out,  uint32_t out_len);

/* TLS 1.3 Derive-Secret(secret, label, transcript_hash) → 32 bytes */
void tls13_derive_secret(const uint8_t secret[32],
                         const char    *label,
                         const uint8_t transcript_hash[32],
                         uint8_t       out[32]);

/* ── AES-128 ──────────────────────────────────────────────── */
typedef struct { uint32_t rk[44]; } Aes128;

void aes128_setkey         (Aes128 *ctx, const uint8_t key[16]);
void aes128_encrypt_block  (const Aes128 *ctx,
                            const uint8_t in[16], uint8_t out[16]);

/* ── AES-128-GCM (AEAD) ───────────────────────────────────── */
/* Encrypt in-place, append 16-byte auth tag at out+len */
void aes128gcm_encrypt(const Aes128  *aes,
                       const uint8_t  iv[12],
                       const uint8_t *aad,  uint32_t aad_len,
                       uint8_t       *buf,  uint32_t len,
                       uint8_t        tag[16]);

/* Decrypt in-place; returns 0 OK, -1 tag mismatch */
int  aes128gcm_decrypt(const Aes128  *aes,
                       const uint8_t  iv[12],
                       const uint8_t *aad,  uint32_t aad_len,
                       uint8_t       *buf,  uint32_t len,
                       const uint8_t  tag[16]);

/* ── X25519 Diffie-Hellman ────────────────────────────────── */
/* Hasilkan private key (32 byte acak + clamping) */
void x25519_generate_private(uint8_t priv[32]);

/* Hitung public key dari private key */
void x25519_public(const uint8_t priv[32], uint8_t pub[32]);

/* Hitung shared secret = X25519(priv, peer_pub) */
void x25519_shared(const uint8_t priv[32],
                   const uint8_t peer_pub[32],
                   uint8_t       shared[32]);

#endif /* CRYPTO_H */
