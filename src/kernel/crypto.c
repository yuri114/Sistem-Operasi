/* crypto.c — SHA-256, HMAC, HKDF, AES-128, AES-128-GCM, X25519
 * Pure integer, no SSE/float/stdlib. */
#include "crypto.h"

/* ═══════════════════════════════════════════════════════════
 * BAGIAN 1 — SHA-256
 * ═══════════════════════════════════════════════════════════ */
static const uint32_t SHA_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define RR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(e,f,g)  (((e)&(f))^((~(e))&(g)))
#define MAJ(a,b,c) (((a)&(b))^((a)&(c))^((b)&(c)))
#define S0(a) (RR32(a,2)^RR32(a,13)^RR32(a,22))
#define S1(e) (RR32(e,6)^RR32(e,11)^RR32(e,25))
#define s0(w) (RR32(w,7)^RR32(w,18)^((w)>>3))
#define s1(w) (RR32(w,17)^RR32(w,19)^((w)>>10))

static void sha256_compress(uint32_t st[8], const uint8_t blk[64])
{
    uint32_t W[64];
    int i;
    for (i = 0; i < 16; i++)
        W[i] = ((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|
               ((uint32_t)blk[i*4+2]<<8)|blk[i*4+3];
    for (i = 16; i < 64; i++)
        W[i] = s1(W[i-2]) + W[i-7] + s0(W[i-15]) + W[i-16];

    uint32_t a=st[0],b=st[1],c=st[2],d=st[3],
             e=st[4],f=st[5],g=st[6],h=st[7];
    for (i = 0; i < 64; i++) {
        uint32_t T1 = h + S1(e) + CH(e,f,g) + SHA_K[i] + W[i];
        uint32_t T2 = S0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+T1;
        d=c; c=b; b=a; a=T1+T2;
    }
    st[0]+=a; st[1]+=b; st[2]+=c; st[3]+=d;
    st[4]+=e; st[5]+=f; st[6]+=g; st[7]+=h;
}

void sha256_init(Sha256 *ctx)
{
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
    ctx->count = 0;
}

void sha256_update(Sha256 *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t rem = (uint32_t)(ctx->count & 63);
    ctx->count += len;
    if (rem) {
        uint32_t fill = 64 - rem;
        if (len < fill) { fill = len; }
        for (uint32_t i = 0; i < fill; i++) ctx->buf[rem+i] = data[i];
        data += fill; len -= fill; rem += fill;
        if (rem == 64) { sha256_compress(ctx->state, ctx->buf); rem = 0; }
    }
    while (len >= 64) {
        sha256_compress(ctx->state, data);
        data += 64; len -= 64;
    }
    for (uint32_t i = 0; i < len; i++) ctx->buf[i] = data[i];
}

void sha256_final(Sha256 *ctx, uint8_t digest[32])
{
    uint64_t bit_count = ctx->count * 8;
    uint8_t  pad[1] = {0x80};
    sha256_update(ctx, pad, 1);
    uint8_t zero[64] = {0};
    while ((ctx->count & 63) != 56)
        sha256_update(ctx, zero, 1);
    uint8_t len_enc[8];
    for (int i = 7; i >= 0; i--) { len_enc[i] = (uint8_t)bit_count; bit_count >>= 8; }
    sha256_update(ctx, len_enc, 8);
    for (int i = 0; i < 8; i++) {
        digest[4*i]   = (uint8_t)(ctx->state[i]>>24);
        digest[4*i+1] = (uint8_t)(ctx->state[i]>>16);
        digest[4*i+2] = (uint8_t)(ctx->state[i]>>8);
        digest[4*i+3] = (uint8_t)(ctx->state[i]);
    }
}

void sha256(const uint8_t *data, uint32_t len, uint8_t digest[32])
{
    Sha256 ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* ═══════════════════════════════════════════════════════════
 * BAGIAN 2 — HMAC-SHA256
 * ═══════════════════════════════════════════════════════════ */
void hmac_sha256(const uint8_t *key, uint32_t klen,
                 const uint8_t *msg, uint32_t mlen,
                 uint8_t out[32])
{
    uint8_t k0[64], ipad[64], opad[64], inner[32];
    int i;
    /* Jika key > 64 byte: hash dulu */
    if (klen > 64) { sha256(key, klen, k0); klen = 32; key = k0; }
    for (i = 0; i < 64; i++) k0[i] = (i < (int)klen) ? key[i] : 0;
    for (i = 0; i < 64; i++) { ipad[i] = k0[i] ^ 0x36; opad[i] = k0[i] ^ 0x5c; }
    /* inner = SHA256(ipad || msg) */
    Sha256 ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, msg, mlen);
    sha256_final(&ctx, inner);
    /* outer = SHA256(opad || inner) */
    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

/* ═══════════════════════════════════════════════════════════
 * BAGIAN 3 — HKDF-SHA256 + TLS 1.3 helpers
 * ═══════════════════════════════════════════════════════════ */
void hkdf_extract(const uint8_t *salt, uint32_t slen,
                  const uint8_t *ikm,  uint32_t ilen,
                  uint8_t prk[32])
{
    if (!salt || slen == 0) {
        static const uint8_t zero32[32] = {0};
        hmac_sha256(zero32, 32, ikm, ilen, prk);
    } else {
        hmac_sha256(salt, slen, ikm, ilen, prk);
    }
}

void hkdf_expand(const uint8_t *prk,
                 const uint8_t *info, uint32_t ilen,
                 uint8_t *out, uint32_t olen)
{
    uint8_t T[32], buf[32 + 256 + 1];
    uint32_t done = 0;
    uint8_t  ctr  = 0;
    while (done < olen) {
        ctr++;
        uint32_t blen = 0;
        if (ctr > 1) {
            for (uint32_t i = 0; i < 32; i++) buf[blen++] = T[i];
        }
        for (uint32_t i = 0; i < ilen && blen < sizeof(buf)-1; i++) buf[blen++] = info[i];
        buf[blen++] = ctr;
        hmac_sha256(prk, 32, buf, blen, T);
        uint32_t take = olen - done;
        if (take > 32) take = 32;
        for (uint32_t i = 0; i < take; i++) out[done+i] = T[i];
        done += take;
    }
}

/* TLS 1.3 HkdfLabel = uint16(len) || uint8(len_label) || "tls13 " || label
 *                   || uint8(len_ctx) || ctx */
void hkdf_expand_label(const uint8_t secret[32],
                       const char    *label,
                       const uint8_t *ctx,  uint32_t ctx_len,
                       uint8_t       *out,  uint32_t out_len)
{
    /* Bangun HkdfLabel */
    uint8_t info[2 + 1 + 6 + 64 + 1 + 64];
    uint32_t pos = 0;
    /* Length */
    info[pos++] = (uint8_t)(out_len >> 8);
    info[pos++] = (uint8_t)(out_len);
    /* Label: "tls13 " + label */
    const char *prefix = "tls13 ";
    uint32_t plen = 6, llen = 0;
    while (label[llen]) llen++;
    info[pos++] = (uint8_t)(plen + llen);
    for (uint32_t i = 0; i < plen; i++) info[pos++] = (uint8_t)prefix[i];
    for (uint32_t i = 0; i < llen; i++) info[pos++] = (uint8_t)label[i];
    /* Context */
    info[pos++] = (uint8_t)(ctx_len);
    for (uint32_t i = 0; i < ctx_len; i++) info[pos++] = ctx[i];
    hkdf_expand(secret, info, pos, out, out_len);
}

void tls13_derive_secret(const uint8_t secret[32],
                         const char    *label,
                         const uint8_t transcript_hash[32],
                         uint8_t       out[32])
{
    hkdf_expand_label(secret, label, transcript_hash, 32, out, 32);
}

/* ═══════════════════════════════════════════════════════════
 * BAGIAN 4 — AES-128
 * ═══════════════════════════════════════════════════════════ */
static const uint8_t SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};
static const uint8_t RCON[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

void aes128_setkey(Aes128 *ctx, const uint8_t key[16])
{
    uint32_t *w = ctx->rk;
    int i;
    for (i = 0; i < 4; i++)
        w[i] = ((uint32_t)key[4*i]<<24)|((uint32_t)key[4*i+1]<<16)|
               ((uint32_t)key[4*i+2]<<8)|key[4*i+3];
    for (i = 4; i < 44; i++) {
        uint32_t t = w[i-1];
        if (i % 4 == 0) {
            t = ((uint32_t)SBOX[(t>>16)&0xff]<<24)|((uint32_t)SBOX[(t>>8)&0xff]<<16)|
                ((uint32_t)SBOX[t&0xff]<<8)|SBOX[(t>>24)&0xff];
            t ^= ((uint32_t)RCON[i/4-1]<<24);
        }
        w[i] = w[i-4] ^ t;
    }
}

static uint8_t aes_xtime(uint8_t b) { return (b<<1)^(b>>7 ? 0x1b : 0); }

void aes128_encrypt_block(const Aes128 *ctx, const uint8_t in[16], uint8_t out[16])
{
    /* state[r][c]: AES 4×4 matrix, stored s[row][col] */
    uint8_t s[4][4];
    int r, c, i;
    const uint32_t *rk = ctx->rk;

    /* Load (column-major: byte 0 = row0,col0; byte 1 = row1,col0 ...) */
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++)
            s[r][c] = in[c*4+r];

    /* AddRoundKey round 0 */
    for (c = 0; c < 4; c++) {
        s[0][c] ^= (rk[c]>>24)&0xff;
        s[1][c] ^= (rk[c]>>16)&0xff;
        s[2][c] ^= (rk[c]>>8)&0xff;
        s[3][c] ^= rk[c]&0xff;
    }

    for (i = 1; i <= 10; i++) {
        /* SubBytes */
        for (r = 0; r < 4; r++)
            for (c = 0; c < 4; c++)
                s[r][c] = SBOX[s[r][c]];
        /* ShiftRows: row r shifted left by r */
        { uint8_t t;
          t=s[1][0]; s[1][0]=s[1][1]; s[1][1]=s[1][2]; s[1][2]=s[1][3]; s[1][3]=t;
          t=s[2][0]; s[2][0]=s[2][2]; s[2][2]=t;
          t=s[2][1]; s[2][1]=s[2][3]; s[2][3]=t;
          t=s[3][3]; s[3][3]=s[3][2]; s[3][2]=s[3][1]; s[3][1]=s[3][0]; s[3][0]=t;
        }
        /* MixColumns (skip last round) */
        if (i < 10) {
            for (c = 0; c < 4; c++) {
                uint8_t a0=s[0][c],a1=s[1][c],a2=s[2][c],a3=s[3][c];
                /* new = [2 3 1 1; 1 2 3 1; 1 1 2 3; 3 1 1 2] × col */
                s[0][c] = aes_xtime(a0)^a1^aes_xtime(a1)^a2^a3;
                s[1][c] = a0^aes_xtime(a1)^a2^aes_xtime(a2)^a3;
                s[2][c] = a0^a1^aes_xtime(a2)^a3^aes_xtime(a3);
                s[3][c] = a0^aes_xtime(a0)^a1^a2^aes_xtime(a3);
            }
        }
        /* AddRoundKey */
        const uint32_t *rki = rk + i*4;
        for (c = 0; c < 4; c++) {
            s[0][c] ^= (rki[c]>>24)&0xff;
            s[1][c] ^= (rki[c]>>16)&0xff;
            s[2][c] ^= (rki[c]>>8)&0xff;
            s[3][c] ^= rki[c]&0xff;
        }
    }
    /* Store (column-major) */
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++)
            out[c*4+r] = s[r][c];
}

/* ═══════════════════════════════════════════════════════════
 * BAGIAN 5 — AES-128-GCM
 * ═══════════════════════════════════════════════════════════ */
/* GHASH menggunakan software carry-less multiplication bit-by-bit (kecil & sederhana) */

/* GCM multiplying H × X in GF(2^128), polynomial x^128+x^7+x^2+x+1
 * Block besar-endian 16 bytes. */
static void gcm_mul(uint8_t Z[16], const uint8_t X[16], const uint8_t Y[16])
{
    uint8_t V[16];
    int i, j;
    for (i = 0; i < 16; i++) Z[i] = 0;
    for (i = 0; i < 16; i++) V[i] = Y[i];

    for (i = 0; i < 16; i++) {
        uint8_t xi = X[i];
        for (j = 7; j >= 0; j--) {
            if ((xi >> j) & 1) {
                for (int k = 0; k < 16; k++) Z[k] ^= V[k];
            }
            /* V = V * x (right-shift in GF(2^128)) */
            uint8_t lsb = V[15] & 1;
            /* shift right 1 bit (big-endian bit order means shift toward LSB) */
            for (int k = 15; k > 0; k--) V[k] = (V[k]>>1) | (V[k-1]<<7);
            V[0] >>= 1;
            /* if lsb was 1: XOR with reduction polynomial 0xE1000...0 */
            if (lsb) V[0] ^= 0xe1;
        }
    }
}

/* GHASH(H, A, C): H = AES(key, 0^128) */
static void ghash(const uint8_t H[16],
                  const uint8_t *aad, uint32_t aad_len,
                  const uint8_t *cipher, uint32_t clen,
                  uint8_t       out[16])
{
    uint8_t X[16] = {0};
    uint32_t i, j;

    /* Process AAD */
    uint32_t aad_blocks = (aad_len + 15) / 16;
    for (i = 0; i < aad_blocks; i++) {
        uint8_t block[16] = {0};
        uint32_t take = (aad_len - i*16 < 16) ? (aad_len - i*16) : 16;
        for (j = 0; j < take; j++) block[j] = aad[i*16+j];
        for (j = 0; j < 16; j++) X[j] ^= block[j];
        gcm_mul(X, X, H);
    }
    /* Process ciphertext */
    uint32_t c_blocks = (clen + 15) / 16;
    for (i = 0; i < c_blocks; i++) {
        uint8_t block[16] = {0};
        uint32_t take = (clen - i*16 < 16) ? (clen - i*16) : 16;
        for (j = 0; j < take; j++) block[j] = cipher[i*16+j];
        for (j = 0; j < 16; j++) X[j] ^= block[j];
        gcm_mul(X, X, H);
    }
    /* Length block: (aad_len * 8) || (clen * 8), both big-endian 64-bit */
    uint8_t lblock[16] = {0};
    uint64_t a_bits = (uint64_t)aad_len * 8;
    uint64_t c_bits = (uint64_t)clen * 8;
    for (j = 0; j < 8; j++) lblock[7-j]   = (uint8_t)(a_bits >> (j*8));
    for (j = 0; j < 8; j++) lblock[15-j]  = (uint8_t)(c_bits >> (j*8));
    for (j = 0; j < 16; j++) X[j] ^= lblock[j];
    gcm_mul(X, X, H);
    for (j = 0; j < 16; j++) out[j] = X[j];
}

/* GCM-CTR: nonce(12) → J0 = nonce||0001; increment counter (last 4 bytes, big-endian) */
static void gcm_ctr(const Aes128 *aes, const uint8_t iv[12],
                    uint8_t *buf, uint32_t len, uint32_t ctr_start)
{
    uint8_t ctr_block[16], keystream[16];
    for (uint32_t i = 0; i < 12; i++) ctr_block[i] = iv[i];
    uint32_t ctr = ctr_start;
    for (uint32_t done = 0; done < len; done += 16) {
        ctr++;
        ctr_block[12] = (uint8_t)(ctr>>24);
        ctr_block[13] = (uint8_t)(ctr>>16);
        ctr_block[14] = (uint8_t)(ctr>>8);
        ctr_block[15] = (uint8_t)(ctr);
        aes128_encrypt_block(aes, ctr_block, keystream);
        uint32_t take = len - done;
        if (take > 16) take = 16;
        for (uint32_t j = 0; j < take; j++) buf[done+j] ^= keystream[j];
    }
}

void aes128gcm_encrypt(const Aes128  *aes,
                       const uint8_t  iv[12],
                       const uint8_t *aad,  uint32_t aad_len,
                       uint8_t       *buf,  uint32_t len,
                       uint8_t        tag[16])
{
    /* Compute H = AES(key, 0^128) */
    uint8_t H[16] = {0}, J0[16] = {0}, S[16];
    aes128_encrypt_block(aes, H, H);
    /* Encrypt plaintext (CTR starting at 2: J0 is ctr=1, data starts at 2) */
    gcm_ctr(aes, iv, buf, len, 1);
    /* GHASH(H, AAD, ciphertext) */
    ghash(H, aad, aad_len, buf, len, S);
    /* Tag = GCTR(J0) XOR S: J0 = iv||0x00000001 */
    for (int i = 0; i < 12; i++) J0[i] = iv[i];
    J0[12]=0; J0[13]=0; J0[14]=0; J0[15]=1;
    uint8_t E_J0[16];
    aes128_encrypt_block(aes, J0, E_J0);
    for (int i = 0; i < 16; i++) tag[i] = S[i] ^ E_J0[i];
}

int aes128gcm_decrypt(const Aes128  *aes,
                      const uint8_t  iv[12],
                      const uint8_t *aad,  uint32_t aad_len,
                      uint8_t       *buf,  uint32_t len,
                      const uint8_t  expected_tag[16])
{
    uint8_t H[16] = {0}, J0[16] = {0}, S[16], tag[16];
    aes128_encrypt_block(aes, H, H);
    /* GHASH before decryption (on ciphertext) */
    ghash(H, aad, aad_len, buf, len, S);
    for (int i = 0; i < 12; i++) J0[i] = iv[i];
    J0[12]=0; J0[13]=0; J0[14]=0; J0[15]=1;
    uint8_t E_J0[16];
    aes128_encrypt_block(aes, J0, E_J0);
    for (int i = 0; i < 16; i++) tag[i] = S[i] ^ E_J0[i];
    /* Verify tag (constant-time compare) */
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= tag[i] ^ expected_tag[i];
    if (diff) return -1;
    /* Decrypt (same as encrypt in CTR mode) */
    gcm_ctr(aes, iv, buf, len, 1);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * BAGIAN 6 — X25519 (Curve25519 DH)
 * Field: GF(2^255-19), representasi 5 × uint64_t, 51 bit/limb
 * ═══════════════════════════════════════════════════════════ */
typedef uint64_t fe[5];
#define FE_MASK ((1ULL<<51)-1)

static void fe_copy(fe h, const fe f)  { h[0]=f[0];h[1]=f[1];h[2]=f[2];h[3]=f[3];h[4]=f[4]; }
static void fe_zero(fe h)              { h[0]=h[1]=h[2]=h[3]=h[4]=0; }
static void fe_one (fe h)              { h[0]=1;h[1]=h[2]=h[3]=h[4]=0; }

static void fe_add(fe h, const fe f, const fe g)
{
    h[0]=f[0]+g[0]; h[1]=f[1]+g[1]; h[2]=f[2]+g[2]; h[3]=f[3]+g[3]; h[4]=f[4]+g[4];
}

static void fe_sub(fe h, const fe f, const fe g)
{
    /* Tambahkan 4*p per-limb agar tidak negatif */
    h[0]=f[0]-g[0]+(4*(1ULL<<51)-76);
    h[1]=f[1]-g[1]+(4*(1ULL<<51)-4);
    h[2]=f[2]-g[2]+(4*(1ULL<<51)-4);
    h[3]=f[3]-g[3]+(4*(1ULL<<51)-4);
    h[4]=f[4]-g[4]+(4*(1ULL<<51)-4);
}

static void fe_mul(fe h, const fe f, const fe g)
{
    typedef __uint128_t u128;
    uint64_t f19[5];
    int i;
    for (i=0;i<5;i++) f19[i] = f[i]*19;
    u128 t0 = (u128)f[0]*g[0] + (u128)f19[1]*g[4] + (u128)f19[2]*g[3] + (u128)f19[3]*g[2] + (u128)f19[4]*g[1];
    u128 t1 = (u128)f[0]*g[1] + (u128)f[1]*g[0]   + (u128)f19[2]*g[4] + (u128)f19[3]*g[3] + (u128)f19[4]*g[2];
    u128 t2 = (u128)f[0]*g[2] + (u128)f[1]*g[1]   + (u128)f[2]*g[0]   + (u128)f19[3]*g[4] + (u128)f19[4]*g[3];
    u128 t3 = (u128)f[0]*g[3] + (u128)f[1]*g[2]   + (u128)f[2]*g[1]   + (u128)f[3]*g[0]   + (u128)f19[4]*g[4];
    u128 t4 = (u128)f[0]*g[4] + (u128)f[1]*g[3]   + (u128)f[2]*g[2]   + (u128)f[3]*g[1]   + (u128)f[4]*g[0];
    /* Propagasi carry */
    uint64_t c;
    c=(uint64_t)(t0>>51); t0&=FE_MASK; t1+=c;
    c=(uint64_t)(t1>>51); t1&=FE_MASK; t2+=c;
    c=(uint64_t)(t2>>51); t2&=FE_MASK; t3+=c;
    c=(uint64_t)(t3>>51); t3&=FE_MASK; t4+=c;
    c=(uint64_t)(t4>>51); t4&=FE_MASK; t0+=(u128)19*c;
    c=(uint64_t)(t0>>51); t0&=FE_MASK; t1+=c;
    c=(uint64_t)(t1>>51); t1&=FE_MASK; t2+=c;
    c=(uint64_t)(t2>>51); t2&=FE_MASK; t3+=c;
    c=(uint64_t)(t3>>51); t3&=FE_MASK; t4+=c;
    h[0]=(uint64_t)t0; h[1]=(uint64_t)t1; h[2]=(uint64_t)t2;
    h[3]=(uint64_t)t3; h[4]=(uint64_t)t4;
}

static void fe_sq(fe h, const fe f) { fe_mul(h,f,f); }

static void fe_mul_scalar(fe h, const fe f, uint64_t n)
{
    typedef __uint128_t u128;
    u128 t0=(u128)f[0]*n, t1=(u128)f[1]*n, t2=(u128)f[2]*n,
         t3=(u128)f[3]*n, t4=(u128)f[4]*n;
    uint64_t c;
    c=(uint64_t)(t0>>51); t0&=FE_MASK; t1+=c;
    c=(uint64_t)(t1>>51); t1&=FE_MASK; t2+=c;
    c=(uint64_t)(t2>>51); t2&=FE_MASK; t3+=c;
    c=(uint64_t)(t3>>51); t3&=FE_MASK; t4+=c;
    c=(uint64_t)(t4>>51); t4&=FE_MASK; t0+=19*c;
    c=(uint64_t)(t0>>51); t0&=FE_MASK; t1+=c;
    h[0]=(uint64_t)t0; h[1]=(uint64_t)t1; h[2]=(uint64_t)t2;
    h[3]=(uint64_t)t3; h[4]=(uint64_t)t4;
}

/* Invers menggunakan Fermat: a^(p-2) = a^(2^255-21) */
static void fe_invert(fe out, const fe z)
{
    fe t0,t1,t2,t3;
    int i;
    fe_sq(t0,z);       /* z^2 */
    fe_sq(t1,t0); fe_sq(t1,t1);  /* z^8 */
    fe_mul(t1,z,t1);  /* z^9 */
    fe_mul(t0,t0,t1); /* z^11 */
    fe_sq(t2,t0);     /* z^22 */
    fe_mul(t1,t1,t2); /* z^31 = z^(2^5-1) */
    fe_sq(t2,t1); for(i=1;i<5;i++) fe_sq(t2,t2); fe_mul(t1,t2,t1);  /* z^(2^10-1) */
    fe_sq(t2,t1); for(i=1;i<10;i++) fe_sq(t2,t2); fe_mul(t2,t2,t1); /* z^(2^20-1) */
    fe_sq(t3,t2); for(i=1;i<20;i++) fe_sq(t3,t3); fe_mul(t2,t3,t2); /* z^(2^40-1) */
    fe_sq(t2,t2); for(i=1;i<10;i++) fe_sq(t2,t2); fe_mul(t1,t2,t1); /* z^(2^50-1) */
    fe_sq(t2,t1); for(i=1;i<50;i++) fe_sq(t2,t2); fe_mul(t2,t2,t1); /* z^(2^100-1) */
    fe_sq(t3,t2); for(i=1;i<100;i++) fe_sq(t3,t3); fe_mul(t2,t3,t2);/* z^(2^200-1) */
    fe_sq(t2,t2); for(i=1;i<50;i++) fe_sq(t2,t2); fe_mul(t1,t2,t1); /* z^(2^250-1) */
    fe_sq(t1,t1); for(i=1;i<5;i++) fe_sq(t1,t1); fe_mul(out,t1,t0); /* z^(2^255-21) */
}

/* Conditional swap (constant-time): swap jika bit==1 */
static void fe_cswap(fe f, fe g, uint64_t bit)
{
    uint64_t mask = 0ULL - bit;  /* 0 or 0xFFFFFFFFFFFFFFFF */
    int i;
    for (i = 0; i < 5; i++) {
        uint64_t x = (f[i] ^ g[i]) & mask;
        f[i] ^= x; g[i] ^= x;
    }
}

/* Load little-endian 32 bytes → field element */
static void fe_from_bytes(fe h, const uint8_t s[32])
{
    uint64_t w0,w1,w2,w3;
    /* Load sebagai 4 × 64-bit little-endian */
    w0=((uint64_t)s[ 0])|(((uint64_t)s[ 1])<<8)|(((uint64_t)s[ 2])<<16)|(((uint64_t)s[ 3])<<24)|
       (((uint64_t)s[ 4])<<32)|(((uint64_t)s[ 5])<<40)|(((uint64_t)s[ 6])<<48)|(((uint64_t)s[ 7])<<56);
    w1=((uint64_t)s[ 8])|(((uint64_t)s[ 9])<<8)|(((uint64_t)s[10])<<16)|(((uint64_t)s[11])<<24)|
       (((uint64_t)s[12])<<32)|(((uint64_t)s[13])<<40)|(((uint64_t)s[14])<<48)|(((uint64_t)s[15])<<56);
    w2=((uint64_t)s[16])|(((uint64_t)s[17])<<8)|(((uint64_t)s[18])<<16)|(((uint64_t)s[19])<<24)|
       (((uint64_t)s[20])<<32)|(((uint64_t)s[21])<<40)|(((uint64_t)s[22])<<48)|(((uint64_t)s[23])<<56);
    w3=((uint64_t)s[24])|(((uint64_t)s[25])<<8)|(((uint64_t)s[26])<<16)|(((uint64_t)s[27])<<24)|
       (((uint64_t)s[28])<<32)|(((uint64_t)s[29])<<40)|(((uint64_t)s[30])<<48)|(((uint64_t)s[31])<<56);
    h[0] = w0 & FE_MASK;
    h[1] = ((w0>>51)|(w1<<13)) & FE_MASK;
    h[2] = ((w1>>38)|(w2<<26)) & FE_MASK;
    h[3] = ((w2>>25)|(w3<<39)) & FE_MASK;
    h[4] = (w3>>12) & FE_MASK; /* ignore top bit (bit 255 = set to 0 for curve25519) */
}

/* Store field element → little-endian 32 bytes (setelah full reduce) */
static void fe_to_bytes(uint8_t s[32], const fe h)
{
    /* Full reduce mod p = 2^255-19 */
    fe t;
    fe_copy(t, h);
    /* Propagate carries */
    for (int iter = 0; iter < 2; iter++) {
        uint64_t c;
        c=t[4]>>51; t[4]&=FE_MASK; t[0]+=19*c;
        c=t[0]>>51; t[0]&=FE_MASK; t[1]+=c;
        c=t[1]>>51; t[1]&=FE_MASK; t[2]+=c;
        c=t[2]>>51; t[2]&=FE_MASK; t[3]+=c;
        c=t[3]>>51; t[3]&=FE_MASK; t[4]+=c;
    }
    /* Conditional subtract p if t >= p */
    /* p = 2^255-19; nilai ini dalam 5 limbs adalah:
       [2^51-19, 2^51-1, 2^51-1, 2^51-1, 2^51-1] */
    /* Cek apakah t >= p (simplified: periksa apakah ada carry pada t0+19 setelah pengurangan p) */
    uint64_t m = (t[4]>>51 != 0) ? 1ULL : 0ULL;
    /* jika t[4] full (==2^51-1) dan semua di atas threshold */
    if (t[4] == FE_MASK && t[3] == FE_MASK && t[2] == FE_MASK && t[1] == FE_MASK && t[0] >= (FE_MASK-18))
        m = 1;
    t[0] -= m * (FE_MASK - 18); /* = m * (2^51-19) */
    t[1] -= m * FE_MASK;
    t[2] -= m * FE_MASK;
    t[3] -= m * FE_MASK;
    t[4] -= m * FE_MASK;
    /* Pack ke 64-bit words */
    uint64_t w0 = t[0] | (t[1]<<51);
    uint64_t w1 = (t[1]>>13) | (t[2]<<38);
    uint64_t w2 = (t[2]>>26) | (t[3]<<25);
    uint64_t w3 = (t[3]>>39) | (t[4]<<12);
    for (int i = 0; i < 8; i++) { s[i]    = (uint8_t)(w0>>(i*8)); }
    for (int i = 0; i < 8; i++) { s[8+i]  = (uint8_t)(w1>>(i*8)); }
    for (int i = 0; i < 8; i++) { s[16+i] = (uint8_t)(w2>>(i*8)); }
    for (int i = 0; i < 8; i++) { s[24+i] = (uint8_t)(w3>>(i*8)); }
}

/* Montgomery ladder untuk Curve25519:
 * curve: y^2 = x^3 + 486662*x^2 + x
 * r = scalar * basepoint (projective coordinates X/Z) */
static void curve25519_ladder(const uint8_t scalar[32], const uint8_t u_in[32], uint8_t u_out[32])
{
    fe x_1, x_2, z_2, x_3, z_3, A, AA, B, BB, C, D, DA, CB, E;
    int i;
    fe_from_bytes(x_1, u_in);
    fe_one(x_2); fe_zero(z_2);
    fe_copy(x_3, x_1); fe_one(z_3);

    uint8_t k[32];
    for (i = 0; i < 32; i++) k[i] = scalar[i];
    /* Clamping (sudah dilakukan di caller, tapi tambahkan lagi untuk safety) */
    k[0] &= 248; k[31] &= 127; k[31] |= 64;

    uint64_t swap = 0;
    for (i = 254; i >= 0; i--) {
        uint64_t bit = (k[i/8] >> (i&7)) & 1;
        uint64_t s2 = swap ^ bit;
        fe_cswap(x_2, x_3, s2);
        fe_cswap(z_2, z_3, s2);
        swap = bit;

        /* Differential addition and doubling */
        fe_add(A, x_2, z_2);   fe_sq(AA, A);
        fe_sub(B, x_2, z_2);   fe_sq(BB, B);
        fe_sub(E, AA, BB);
        fe_add(C, x_3, z_3);
        fe_sub(D, x_3, z_3);
        fe_mul(DA, D, A);
        fe_mul(CB, C, B);
        fe_add(x_3, DA, CB);   fe_sq(x_3, x_3);
        fe_sub(z_3, DA, CB);   fe_sq(z_3, z_3);
        fe_mul(z_3, z_3, x_1);
        fe_mul(x_2, AA, BB);
        /* z_2 = E * (AA + 121666*E) */
        fe_mul_scalar(A, E, 121666);
        fe_add(A, AA, A);
        fe_mul(z_2, E, A);
    }
    fe_cswap(x_2, x_3, swap);
    fe_cswap(z_2, z_3, swap);

    /* Convert projective to affine: u_out = x_2 * z_2^(p-2) */
    fe_invert(z_2, z_2);
    fe_mul(x_2, x_2, z_2);
    fe_to_bytes(u_out, x_2);
}

/* Kernel PRNG sederhana: seeded dari timer tick + alamat stack */
extern uint64_t get_ticks(void);

static uint64_t prng_state = 0;
static uint64_t prng_next(void)
{
    if (!prng_state) {
        prng_state = get_ticks();
        prng_state ^= (uint64_t)(uintptr_t)&prng_state;
        prng_state ^= 0xdeadbeefcafeULL;
    }
    /* xorshift64 */
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 7;
    prng_state ^= prng_state << 17;
    return prng_state;
}

void x25519_generate_private(uint8_t priv[32])
{
    for (int i = 0; i < 32; i += 8) {
        uint64_t r = prng_next();
        for (int j = 0; j < 8; j++) priv[i+j] = (uint8_t)(r >> (j*8));
    }
    /* Clamping sesuai RFC 7748 */
    priv[0]  &= 248;
    priv[31] &= 127;
    priv[31] |= 64;
}

/* Base point u = 9 (little-endian 32 bytes) */
static const uint8_t BASE_POINT[32] = {9,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

void x25519_public(const uint8_t priv[32], uint8_t pub[32])
{
    curve25519_ladder(priv, BASE_POINT, pub);
}

void x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t shared[32])
{
    curve25519_ladder(priv, peer_pub, shared);
}
