/* tls13.c — TLS 1.3 minimal client
 * Alur: ClientHello -> ServerHello -> [EncExt+Cert+CertVerify+Finished] -> Finished
 * Fondasi AQ: sertifikat di-parse (x509) dan hostname di-verifikasi.
 * Cipher suite: TLS_AES_128_GCM_SHA256 (0x1301) + X25519 */
#include "tls13.h"
#include "crypto.h"
#include "x509.h"

/* Forward declarations dari net.h (hindari circular include) */
extern int  net_tcp_send(int id, const void *data, uint16_t len);
extern int  net_tcp_recv(int id, void *buf, uint16_t maxlen);
extern uint64_t get_ticks(void);
extern void print(const char *s);
extern void set_color(uint32_t fg, uint32_t bg);

/* Konstanta */
#define TLS_MAX_CONN          4
#define TLS_REC_BUF        8192
#define TLS_APP_BUF        8192
#define TLS_HANDSHAKE      0x16
#define TLS_APP_DATA       0x17
#define TLS_ALERT          0x15
#define TLS_CHANGE_CIPHER  0x14
#define HS_CLIENT_HELLO    1
#define HS_SERVER_HELLO    2
#define HS_CERTIFICATE     11
#define HS_FINISHED        20

/* State TLS */
typedef struct {
    int     used;
    int     tcp_id;
    uint8_t c_priv[32], c_pub[32], s_pub[32], ecdhe[32];
    uint8_t hs_s_key[16], hs_s_iv[12];
    uint8_t hs_c_key[16], hs_c_iv[12];
    uint8_t c_hs_traffic[32];  /* simpan untuk client Finished key */
    uint8_t ap_s_key[16], ap_s_iv[12];
    uint8_t ap_c_key[16], ap_c_iv[12];
    uint64_t hs_s_seq, hs_c_seq;
    uint64_t ap_s_seq, ap_c_seq;
    Sha256   transcript;
    int      state;
    uint8_t  ap_buf[TLS_APP_BUF];
    uint32_t ap_head, ap_tail;
} TlsConn;

static TlsConn  tls_conns[TLS_MAX_CONN];
/* Fondasi AQ: sertifikat server terakhir yang di-parse */
static X509Cert tls_last_cert;
static int      tls_last_cert_valid = 0;

/* Utilitas */
static void tmemset(void *p, uint8_t v, uint32_t n)
{ uint8_t *b=(uint8_t*)p; while(n--)*b++=v; }

static void tmemcpy(void *d, const void *s, uint32_t n)
{ uint8_t *dd=(uint8_t*)d; const uint8_t *ss=(const uint8_t*)s; while(n--)*dd++=*ss++; }

/* PRNG sederhana */
static uint64_t prng_st = 0;
static uint8_t prng_byte(void)
{
    if (!prng_st) {
        prng_st = get_ticks();
        prng_st ^= (uint64_t)(uintptr_t)&prng_st;
        prng_st ^= 0xdeadcafe1234ULL;
    }
    prng_st ^= prng_st << 13;
    prng_st ^= prng_st >> 7;
    prng_st ^= prng_st << 17;
    return (uint8_t)prng_st;
}

/* Nonce GCM: iv XOR seq (big-endian 8 byte, left-pad to 12) */
static void make_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12])
{
    tmemcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++)
        nonce[4+i] ^= (uint8_t)(seq >> (56 - 8*i));
}

/* Kirim data TCP dengan split jika perlu */
static void tcp_send_all(int tcp_id, const uint8_t *data, uint32_t len)
{
    while (len > 0) {
        uint16_t chunk = (len > 1400) ? 1400 : (uint16_t)len;
        net_tcp_send(tcp_id, data, chunk);
        data += chunk;
        len  -= chunk;
    }
}

/* Baca N byte dari TCP (blocking, 5s timeout) */
static int tcp_read_exact(int tcp_id, uint8_t *buf, uint32_t n)
{
    uint32_t done = 0;
    uint32_t start_tick = (uint32_t)get_ticks();
    while (done < n) {
        if ((uint32_t)get_ticks() - start_tick > 5000) return -1;
        int r = net_tcp_recv(tcp_id, buf+done, (uint16_t)(n-done < 1460 ? n-done : 1460));
        if (r > 0) { done += r; start_tick = (uint32_t)get_ticks(); }
    }
    return 0;
}

/* Buffer record masuk */
static uint8_t rec_buf[TLS_REC_BUF];

/* Baca satu TLS record */
static int read_record(TlsConn *c, uint8_t *type, uint8_t **data, uint32_t *len)
{
    uint8_t hdr[5];
    if (tcp_read_exact(c->tcp_id, hdr, 5) != 0) return -1;
    uint32_t rlen = ((uint32_t)hdr[3]<<8)|hdr[4];
    if (rlen > TLS_REC_BUF) return -1;
    if (tcp_read_exact(c->tcp_id, rec_buf, rlen) != 0) return -1;
    *type = hdr[0];
    *data = rec_buf;
    *len  = rlen;
    return 0;
}

/* Buffer enkripsi */
static uint8_t enc_buf[TLS_REC_BUF + 32];

/* Enkripsi & kirim TLS record */
static int send_encrypted(TlsConn *c, uint8_t ct,
                           const uint8_t *data, uint32_t len,
                           const uint8_t key[16], const uint8_t iv[12], uint64_t *seq)
{
    if (len + 1 > TLS_REC_BUF) return -1;
    tmemcpy(enc_buf, data, len);
    enc_buf[len] = ct;
    uint32_t ptlen = len + 1;
    uint32_t clen  = ptlen + 16;

    uint8_t aad[5] = {0x17, 0x03, 0x03, (uint8_t)(clen>>8), (uint8_t)clen};
    uint8_t nonce[12]; make_nonce(iv, *seq, nonce); (*seq)++;

    Aes128 aes; aes128_setkey(&aes, key);
    uint8_t tag[16];
    aes128gcm_encrypt(&aes, nonce, aad, 5, enc_buf, ptlen, tag);

    uint8_t hdr[5] = {0x17, 0x03, 0x03, (uint8_t)(clen>>8), (uint8_t)clen};
    tcp_send_all(c->tcp_id, hdr, 5);
    tcp_send_all(c->tcp_id, enc_buf, ptlen);
    tcp_send_all(c->tcp_id, tag, 16);
    return 0;
}

/* Dekripsi record; return inner content type, -1 = error */
static int decrypt_record(uint8_t *buf, uint32_t *len,
                           const uint8_t key[16], const uint8_t iv[12], uint64_t *seq,
                           const uint8_t aad[5])
{
    if (*len < 16) return -1;
    uint32_t ptlen = *len - 16;
    uint8_t tag[16]; tmemcpy(tag, buf+ptlen, 16);
    uint8_t nonce[12]; make_nonce(iv, *seq, nonce); (*seq)++;
    Aes128 aes; aes128_setkey(&aes, key);
    if (aes128gcm_decrypt(&aes, nonce, aad, 5, buf, ptlen, tag) != 0) return -1;
    int ict = -1;
    for (int i = (int)ptlen-1; i >= 0; i--) {
        if (buf[i]) { ict = buf[i]; *len = (uint32_t)i; break; }
    }
    return ict;
}

/* Buffer ClientHello */
static uint8_t ch_buf[640];

/* Kirim ClientHello dengan SNI */
static int send_client_hello(TlsConn *c, const char *host)
{
    uint32_t p = 0;
    ch_buf[p++]=1; uint32_t hs_len_pos=p; p+=3;
    uint32_t body_start = p;

    ch_buf[p++]=0x03; ch_buf[p++]=0x03; /* legacy_version */
    for (int i=0;i<32;i++) ch_buf[p++]=prng_byte(); /* client_random */
    ch_buf[p++]=0; /* session_id_len=0 */
    ch_buf[p++]=0; ch_buf[p++]=2; ch_buf[p++]=0x13; ch_buf[p++]=0x01; /* cipher_suites */
    ch_buf[p++]=1; ch_buf[p++]=0; /* compression */

    uint32_t ext_tot_pos = p; p+=2;
    uint32_t ext_start = p;

    /* SNI (server_name) extension */
    if (host) {
        uint32_t hlen = 0; while (host[hlen]) hlen++;
        ch_buf[p++]=0x00; ch_buf[p++]=0x00;  /* type SNI */
        uint16_t ext_len = (uint16_t)(5 + hlen);
        ch_buf[p++]=(uint8_t)(ext_len>>8); ch_buf[p++]=(uint8_t)ext_len;
        uint16_t list_len = (uint16_t)(3 + hlen);
        ch_buf[p++]=(uint8_t)(list_len>>8); ch_buf[p++]=(uint8_t)list_len;
        ch_buf[p++]=0x00;  /* host_name type */
        ch_buf[p++]=(uint8_t)(hlen>>8); ch_buf[p++]=(uint8_t)hlen;
        tmemcpy(ch_buf+p, (const uint8_t*)host, hlen); p+=hlen;
    }

    /* supported_versions: TLS 1.3 */
    ch_buf[p++]=0x00;ch_buf[p++]=0x2b; ch_buf[p++]=0x00;ch_buf[p++]=0x03;
    ch_buf[p++]=0x02; ch_buf[p++]=0x03;ch_buf[p++]=0x04;

    /* key_share: x25519 */
    ch_buf[p++]=0x00;ch_buf[p++]=0x33;
    ch_buf[p++]=0x00;ch_buf[p++]=38;
    ch_buf[p++]=0x00;ch_buf[p++]=36;
    ch_buf[p++]=0x00;ch_buf[p++]=0x1d;
    ch_buf[p++]=0x00;ch_buf[p++]=32;
    tmemcpy(ch_buf+p, c->c_pub, 32); p+=32;

    /* supported_groups: x25519 */
    ch_buf[p++]=0x00;ch_buf[p++]=0x0a; ch_buf[p++]=0x00;ch_buf[p++]=4;
    ch_buf[p++]=0x00;ch_buf[p++]=2; ch_buf[p++]=0x00;ch_buf[p++]=0x1d;

    /* signature_algorithms: rsa_pss_rsae_sha256 */
    ch_buf[p++]=0x00;ch_buf[p++]=0x0d; ch_buf[p++]=0x00;ch_buf[p++]=4;
    ch_buf[p++]=0x00;ch_buf[p++]=2; ch_buf[p++]=0x08;ch_buf[p++]=0x04;

    uint32_t el = p - ext_start;
    ch_buf[ext_tot_pos]=(uint8_t)(el>>8); ch_buf[ext_tot_pos+1]=(uint8_t)el;
    uint32_t bl = p - body_start;
    ch_buf[hs_len_pos]=(uint8_t)(bl>>16);
    ch_buf[hs_len_pos+1]=(uint8_t)(bl>>8);
    ch_buf[hs_len_pos+2]=(uint8_t)bl;

    sha256_update(&c->transcript, ch_buf, p);

    uint8_t hdr[5]={0x16,0x03,0x01,(uint8_t)(p>>8),(uint8_t)p};
    tcp_send_all(c->tcp_id, hdr, 5);
    tcp_send_all(c->tcp_id, ch_buf, p);
    return 0;
}

/* Parse ServerHello - ekstrak server X25519 public key */
static int parse_server_hello(TlsConn *c, const uint8_t *d, uint32_t len)
{
    if (len < 4 || d[0] != HS_SERVER_HELLO) return -1;
    uint32_t p = 4 + 2 + 32; /* HS header + legacy_version + random */
    if (p >= len) return -1;
    uint8_t sid_len = d[p++];
    p += sid_len + 2 + 1; /* session_id + cipher_suite + compression */
    if (p+2 > len) return -1;
    uint32_t ext_end = p + 2 + (((uint32_t)d[p]<<8)|d[p+1]); p+=2;
    while (p+4 <= ext_end) {
        uint16_t etype = ((uint16_t)d[p]<<8)|d[p+1];
        uint16_t elen  = ((uint16_t)d[p+2]<<8)|d[p+3];
        p += 4;
        if (etype == 0x0033) {
            if (elen < 36) return -1;
            uint16_t grp = ((uint16_t)d[p]<<8)|d[p+1];
            if (grp != 0x001d) return -1;
            tmemcpy(c->s_pub, d+p+4, 32);
            return 0;
        }
        p += elen;
    }
    return -1;
}

/* Simpan hs_secret untuk derive_app_keys */
static uint8_t saved_hs_secret[32];

/* Derive handshake keys */
static void derive_hs_keys(TlsConn *c, const uint8_t th1[32])
{
    uint8_t zero32[32]={0};
    uint8_t empty_hash[32]; sha256((const uint8_t*)"", 0, empty_hash);
    uint8_t early[32], derived1[32], hs_sec[32];
    uint8_t s_traffic[32], c_traffic[32];

    hkdf_extract(zero32, 32, zero32, 32, early);
    tls13_derive_secret(early, "derived", empty_hash, derived1);
    hkdf_extract(derived1, 32, c->ecdhe, 32, hs_sec);
    tmemcpy(saved_hs_secret, hs_sec, 32);

    tls13_derive_secret(hs_sec, "s hs traffic", th1, s_traffic);
    tls13_derive_secret(hs_sec, "c hs traffic", th1, c_traffic);
    tmemcpy(c->c_hs_traffic, c_traffic, 32);  /* simpan untuk Finished */

    hkdf_expand_label(s_traffic, "key", 0, 0, c->hs_s_key, 16);
    hkdf_expand_label(s_traffic, "iv",  0, 0, c->hs_s_iv,  12);
    hkdf_expand_label(c_traffic, "key", 0, 0, c->hs_c_key, 16);
    hkdf_expand_label(c_traffic, "iv",  0, 0, c->hs_c_iv,  12);
    c->hs_s_seq = 0; c->hs_c_seq = 0;
}

/* Derive application keys */
static void derive_app_keys(TlsConn *c, const uint8_t th2[32])
{
    uint8_t zero32[32]={0};
    uint8_t empty_hash[32]; sha256((const uint8_t*)"", 0, empty_hash);
    uint8_t derived2[32], master[32];
    uint8_t s_traffic[32], c_traffic[32];

    tls13_derive_secret(saved_hs_secret, "derived", empty_hash, derived2);
    hkdf_extract(derived2, 32, zero32, 32, master);

    tls13_derive_secret(master, "s ap traffic", th2, s_traffic);
    tls13_derive_secret(master, "c ap traffic", th2, c_traffic);

    hkdf_expand_label(s_traffic, "key", 0, 0, c->ap_s_key, 16);
    hkdf_expand_label(s_traffic, "iv",  0, 0, c->ap_s_iv,  12);
    hkdf_expand_label(c_traffic, "key", 0, 0, c->ap_c_key, 16);
    hkdf_expand_label(c_traffic, "iv",  0, 0, c->ap_c_iv,  12);
    c->ap_s_seq = 0; c->ap_c_seq = 0;
}

/* ======================================================
 * tls13_connect - full TLS 1.3 handshake
 * ====================================================== */
int tls13_connect(int tcp_id, const char *host)
{
    TlsConn *c = 0;
    for (int i = 0; i < TLS_MAX_CONN; i++) {
        if (!tls_conns[i].used) { c = &tls_conns[i]; break; }
    }
    if (!c) return -1;

    tmemset(c, 0, sizeof(TlsConn));
    c->used = 1;
    c->tcp_id = tcp_id;
    sha256_init(&c->transcript);

    x25519_generate_private(c->c_priv);
    x25519_public(c->c_priv, c->c_pub);

    if (send_client_hello(c, host) != 0) goto fail;

    /* Baca ServerHello — skip ChangeCipherSpec jika ada */
    {
        uint8_t rtype; uint8_t *rdata; uint32_t rlen;
        int tries = 0;
        for (;;) {
            if (read_record(c, &rtype, &rdata, &rlen) != 0) goto fail;
            if (rtype == TLS_CHANGE_CIPHER) { tries++; if (tries > 5) goto fail; continue; }
            if (rtype == TLS_ALERT) goto fail;
            break;
        }
        if (rtype != TLS_HANDSHAKE) goto fail;
        sha256_update(&c->transcript, rdata, rlen);
        if (parse_server_hello(c, rdata, rlen) != 0) goto fail;
    }

    x25519_shared(c->c_priv, c->s_pub, c->ecdhe);

    /* Derive HS keys */
    {
        uint8_t th1[32]; Sha256 tmp = c->transcript; sha256_final(&tmp, th1);
        derive_hs_keys(c, th1);
    }

    /* Baca encrypted HS messages dari server */
    {
        int got_fin = 0;
        for (int pass = 0; pass < 30 && !got_fin; pass++) {
            uint8_t rtype; uint8_t *rdata; uint32_t rlen;
            if (read_record(c, &rtype, &rdata, &rlen) != 0) goto fail;
            if (rtype == TLS_ALERT) goto fail;
            if (rtype == TLS_CHANGE_CIPHER) continue;
            if (rtype != TLS_APP_DATA) continue;

            uint8_t aad[5]={0x17,0x03,0x03,(uint8_t)(rlen>>8),(uint8_t)rlen};
            uint32_t dlen = rlen;
            int ict = decrypt_record(rdata, &dlen, c->hs_s_key, c->hs_s_iv, &c->hs_s_seq, aad);
            if (ict < 0) continue;
            if (ict == TLS_HANDSHAKE) {
                sha256_update(&c->transcript, rdata, dlen);
                /* Scan untuk HS_FINISHED dan HS_CERTIFICATE */
                uint32_t hp = 0;
                while (hp + 4 <= dlen) {
                    uint8_t hs_type = rdata[hp];
                    uint32_t hs_len = ((uint32_t)rdata[hp+1]<<16)|((uint32_t)rdata[hp+2]<<8)|rdata[hp+3];
                    if (hs_type == HS_FINISHED) got_fin = 1;
                    /* Fondasi AQ: parse Certificate message */
                    if (hs_type == HS_CERTIFICATE && hs_len > 4) {
                        /* TLS 1.3 Certificate: [0]=req_ctx_len, [1..3]=cert_list_len
                         * cert_entry: [0..2]=cert_data_len, [3..]=DER */
                        uint32_t cp = hp + 4;
                        uint32_t cp_end = cp + hs_len;
                        if (cp < cp_end) cp++; /* skip request_context length */
                        if (cp + 3 <= cp_end) {
                            /* cert_list_len */
                            cp += 3;
                            if (cp + 3 <= cp_end) {
                                uint32_t cder_len = ((uint32_t)rdata[cp]<<16)|
                                                    ((uint32_t)rdata[cp+1]<<8)|rdata[cp+2];
                                cp += 3;
                                if (cp + cder_len <= cp_end) {
                                    /* Parse first (leaf) certificate */
                                    tls_last_cert_valid = x509_parse(
                                        rdata + cp, cder_len, &tls_last_cert);
                                }
                            }
                        }
                    }
                    hp += 4 + hs_len;
                }
            }
        }
        if (!got_fin) goto fail;
    }

    /* Derive app keys */
    {
        uint8_t th2[32]; Sha256 tmp = c->transcript; sha256_final(&tmp, th2);
        derive_app_keys(c, th2);
    }

    /* Kirim client Finished */
    {
        uint8_t fin_key[32];
        hkdf_expand_label(c->c_hs_traffic, "finished", 0, 0, fin_key, 32);
        uint8_t th_fin[32]; Sha256 tmp2 = c->transcript; sha256_final(&tmp2, th_fin);
        uint8_t verify[32];
        hmac_sha256(fin_key, 32, th_fin, 32, verify);

        uint8_t fin_msg[36];
        fin_msg[0]=HS_FINISHED; fin_msg[1]=0; fin_msg[2]=0; fin_msg[3]=32;
        tmemcpy(fin_msg+4, verify, 32);

        sha256_update(&c->transcript, fin_msg, 36);
        send_encrypted(c, TLS_HANDSHAKE, fin_msg, 36, c->hs_c_key, c->hs_c_iv, &c->hs_c_seq);
    }

    c->state = 1;
    return 0;

fail:
    tmemset(c, 0, sizeof(TlsConn));
    return -1;
}

/* ======================================================
 * tls13_send
 * ====================================================== */
int tls13_send(int tcp_id, const void *data, uint32_t len)
{
    TlsConn *c = 0;
    for (int i = 0; i < TLS_MAX_CONN; i++)
        if (tls_conns[i].used && tls_conns[i].tcp_id==tcp_id && tls_conns[i].state==1)
            { c=&tls_conns[i]; break; }
    if (!c) return -1;
    return send_encrypted(c, TLS_APP_DATA, (const uint8_t*)data, len,
                          c->ap_c_key, c->ap_c_iv, &c->ap_c_seq);
}

/* ======================================================
 * tls13_recv
 * ====================================================== */
int tls13_recv(int tcp_id, void *buf, uint32_t maxlen)
{
    TlsConn *c = 0;
    for (int i = 0; i < TLS_MAX_CONN; i++)
        if (tls_conns[i].used && tls_conns[i].tcp_id==tcp_id && tls_conns[i].state==1)
            { c=&tls_conns[i]; break; }
    if (!c) return -1;

    /* Drain buffer */
    if (c->ap_head != c->ap_tail) {
        uint32_t avail = (c->ap_tail - c->ap_head + TLS_APP_BUF) % TLS_APP_BUF;
        uint32_t take = (avail < maxlen) ? avail : maxlen;
        for (uint32_t i=0;i<take;i++) {
            ((uint8_t*)buf)[i]=c->ap_buf[c->ap_head];
            c->ap_head=(c->ap_head+1)%TLS_APP_BUF;
        }
        return (int)take;
    }

    for (int pass=0; pass<10; pass++) {
        uint8_t rtype; uint8_t *rdata; uint32_t rlen;
        if (read_record(c, &rtype, &rdata, &rlen) != 0) return 0;
        if (rtype == TLS_ALERT) return 0;
        if (rtype == TLS_CHANGE_CIPHER) continue;
        if (rtype != TLS_APP_DATA) continue;

        uint8_t aad[5]={0x17,0x03,0x03,(uint8_t)(rlen>>8),(uint8_t)rlen};
        uint32_t dlen = rlen;
        int ict = decrypt_record(rdata, &dlen, c->ap_s_key, c->ap_s_iv, &c->ap_s_seq, aad);
        if (ict < 0) continue;
        if (ict == TLS_ALERT) return 0;
        if (ict == TLS_APP_DATA) {
            uint32_t take = (dlen < maxlen) ? dlen : maxlen;
            tmemcpy(buf, rdata, take);
            for (uint32_t i=take;i<dlen;i++) {
                c->ap_buf[c->ap_tail]=rdata[i];
                c->ap_tail=(c->ap_tail+1)%TLS_APP_BUF;
            }
            return (int)take;
        }
    }
    return 0;
}

/* ======================================================
 * tls13_close
 * ====================================================== */
void tls13_close(int tcp_id)
{
    for (int i=0;i<TLS_MAX_CONN;i++) {
        if (tls_conns[i].used && tls_conns[i].tcp_id==tcp_id) {
            if (tls_conns[i].state==1) {
                uint8_t alert[2]={1,0};
                send_encrypted(&tls_conns[i], TLS_ALERT, alert, 2,
                               tls_conns[i].ap_c_key, tls_conns[i].ap_c_iv,
                               &tls_conns[i].ap_c_seq);
            }
            tmemset(&tls_conns[i], 0, sizeof(TlsConn));
            return;
        }
    }
}

int tls13_is_open(int tcp_id)
{
    for (int i=0;i<TLS_MAX_CONN;i++)
        if (tls_conns[i].used && tls_conns[i].tcp_id==tcp_id && tls_conns[i].state==1)
            return 1;
    return 0;
}

/* ================================================================
 * Fondasi AQ — Certificate info API
 * ================================================================ */
/* Salin cert server terakhir ke out. Return 1 jika ada, 0 jika tidak ada. */
int tls13_get_last_cert(X509Cert *out) {
    if (!tls_last_cert_valid) return 0;
    /* manual copy */
    uint8_t *d = (uint8_t*)out, *s = (uint8_t*)&tls_last_cert;
    uint32_t n = sizeof(X509Cert);
    while (n--) *d++ = *s++;
    return 1;
}

/* Periksa apakah hostname cocok dengan cert server terakhir.
 * Return 1 cocok, 0 tidak cocok, -1 cert tidak tersedia. */
int tls13_check_hostname(const char *hostname) {
    if (!tls_last_cert_valid) return -1;
    return x509_check_hostname(&tls_last_cert, hostname);
}
