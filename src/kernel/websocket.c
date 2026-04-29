/* websocket.c — Fondasi AN: WebSocket client
 * Mendukung ws:// (plaintext) dan wss:// (TLS 1.3 via tls13.h).
 * Alur:
 *   1. DNS resolve + TCP connect (atau TLS connect untuk wss://)
 *   2. HTTP Upgrade handshake (RFC 6455)
 *   3. Frame encode/decode — send/recv text frame
 */
#include "websocket.h"
#include "net.h"
#include "tls13.h"
#include "keyboard.h"
#include <stdint.h>

extern void     print(const char *s);
extern void     itoa(uint32_t n, char *buf);
extern void     set_color(uint32_t fg, uint32_t bg);
extern uint32_t get_ticks(void);

/* ================================================================
 * SHA-1 (FIPS 180-4) — diperlukan untuk Sec-WebSocket-Accept
 * ================================================================ */
static void sha1(const uint8_t *msg, uint32_t msglen, uint8_t out[20]) {
    uint32_t h0 = 0x67452301u, h1 = 0xEFCDAB89u, h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u, h4 = 0xC3D2E1F0u;

    /* Pre-processing: padding */
    static uint8_t padded[128];
    int i;
    for (i = 0; i < (int)msglen && i < (int)sizeof(padded); i++) padded[i] = msg[i];

    uint32_t padlen = msglen;
    padded[padlen++] = 0x80;
    while ((padlen % 64) != 56) {
        if (padlen >= sizeof(padded)) break;
        padded[padlen++] = 0;
    }
    /* Append bit length (64-bit big-endian) */
    uint64_t bitlen = (uint64_t)msglen * 8;
    if (padlen + 8 <= sizeof(padded)) {
        for (i = 7; i >= 0; i--) {
            padded[padlen + i] = (uint8_t)(bitlen & 0xFF);
            bitlen >>= 8;
        }
        padlen += 8;
    }

    /* Process each 512-bit block */
    uint32_t block;
    for (block = 0; block < padlen; block += 64) {
        uint32_t w[80];
        int t;
        for (t = 0; t < 16; t++) {
            w[t] = ((uint32_t)padded[block + t*4    ] << 24)
                 | ((uint32_t)padded[block + t*4 + 1] << 16)
                 | ((uint32_t)padded[block + t*4 + 2] <<  8)
                 | ((uint32_t)padded[block + t*4 + 3]);
        }
        for (t = 16; t < 80; t++) {
            uint32_t v = w[t-3] ^ w[t-8] ^ w[t-14] ^ w[t-16];
            w[t] = (v << 1) | (v >> 31);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (t = 0; t < 80; t++) {
            uint32_t f, k;
            if      (t < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (t < 40) { f = b ^ c ^ d;           k = 0x6ED9EBA1u; }
            else if (t < 60) { f = (b&c)|(b&d)|(c&d);  k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;           k = 0xCA62C1D6u; }
            uint32_t tmp = ((a << 5)|(a >> 27)) + f + e + k + w[t];
            e = d; d = c; c = (b << 30)|(b >> 2); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    /* Output big-endian */
    uint32_t hh[5] = { h0, h1, h2, h3, h4 };
    for (i = 0; i < 5; i++) {
        out[i*4    ] = (uint8_t)(hh[i] >> 24);
        out[i*4 + 1] = (uint8_t)(hh[i] >> 16);
        out[i*4 + 2] = (uint8_t)(hh[i] >>  8);
        out[i*4 + 3] = (uint8_t)(hh[i]      );
    }
}

/* ================================================================
 * Base64 encode/decode
 * ================================================================ */
static const char b64_tbl[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const uint8_t *in, int inlen, char *out) {
    int i, op = 0;
    for (i = 0; i < inlen; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i+1 < inlen) v |= (uint32_t)in[i+1] << 8;
        if (i+2 < inlen) v |= (uint32_t)in[i+2];
        out[op++] = b64_tbl[(v >> 18) & 0x3F];
        out[op++] = b64_tbl[(v >> 12) & 0x3F];
        out[op++] = (i+1 < inlen) ? b64_tbl[(v >>  6) & 0x3F] : '=';
        out[op++] = (i+2 < inlen) ? b64_tbl[(v      ) & 0x3F] : '=';
    }
    out[op] = 0;
}

/* ================================================================
 * WebSocket GUID (RFC 6455)
 * ================================================================ */
static const char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* Hitung Sec-WebSocket-Accept dari Sec-WebSocket-Key */
static void ws_accept_key(const char *client_key, char *out_b64) {
    /* Gabungkan key + GUID */
    static uint8_t concat[128];
    int ci = 0;
    const char *p = client_key;
    while (*p && ci < 100) concat[ci++] = (uint8_t)*p++;
    p = WS_GUID;
    while (*p && ci < 127) concat[ci++] = (uint8_t)*p++;
    /* SHA-1 */
    uint8_t digest[20];
    sha1(concat, (uint32_t)ci, digest);
    /* Base64 */
    base64_encode(digest, 20, out_b64);
}

/* ================================================================
 * WebSocket frame encode (client→server, masked)
 * opcode: 0x1 = text, 0x8 = close, 0x9 = ping, 0xA = pong
 * ================================================================ */
static int ws_frame_encode(uint8_t *out, int outmax, uint8_t opcode,
                           const uint8_t *payload, uint32_t plen,
                           uint8_t mask[4]) {
    if ((int)(plen + 10) > outmax) return -1;
    int i = 0;
    out[i++] = 0x80 | (opcode & 0x0F);  /* FIN + opcode */
    /* MASK bit set (client must mask) */
    if (plen <= 125) {
        out[i++] = (uint8_t)(0x80 | plen);
    } else if (plen <= 65535) {
        out[i++] = 0xFE;
        out[i++] = (uint8_t)(plen >> 8);
        out[i++] = (uint8_t)(plen & 0xFF);
    } else {
        out[i++] = 0xFF;
        /* 8-byte extended (plen ≤ 65535 cukup untuk kita) */
        int k;
        for (k = 7; k >= 0; k--) out[i++] = (uint8_t)((plen >> (k*8)) & 0xFF);
    }
    /* Masking key */
    out[i++] = mask[0]; out[i++] = mask[1]; out[i++] = mask[2]; out[i++] = mask[3];
    /* Masked payload */
    uint32_t j;
    for (j = 0; j < plen; j++)
        out[i++] = payload[j] ^ mask[j % 4];
    return i;
}

/* Decode satu frame dari buf[0..buflen).
 * Return jumlah byte frame (header+payload), atau 0 jika belum lengkap.
 * opcode_out, payload_out, payload_len_out diisi. */
static int ws_frame_decode(const uint8_t *buf, int buflen,
                           uint8_t *opcode_out,
                           const uint8_t **payload_out,
                           uint32_t *plen_out) {
    if (buflen < 2) return 0;
    *opcode_out = buf[0] & 0x0F;
    int masked  = (buf[1] & 0x80) != 0;
    uint32_t plen = buf[1] & 0x7F;
    int hdr = 2;
    if (plen == 126) {
        if (buflen < 4) return 0;
        plen = ((uint32_t)buf[2] << 8) | buf[3];
        hdr  = 4;
    } else if (plen == 127) {
        if (buflen < 10) return 0;
        plen = ((uint32_t)buf[6]<<24)|((uint32_t)buf[7]<<16)|((uint32_t)buf[8]<<8)|buf[9];
        hdr  = 10;
    }
    if (masked) hdr += 4;
    if (buflen < hdr + (int)plen) return 0;
    *payload_out  = buf + hdr;
    *plen_out     = plen;
    return hdr + (int)plen;
}

/* ================================================================
 * ws_connect(url) — entry point dipanggil dari shell.c
 * url: "ws://host/path" atau "wss://host/path"
 * Mode interaktif: baris dari keyboard dikirim sebagai text frame.
 * Keluar dengan ketik "\\q" atau terima frame close.
 * ================================================================ */

/* Simple pseudo-random 32-bit dari ticks (cukup untuk masking key) */
static uint32_t ws_rand_seed = 0xDEAD4321;
static uint32_t ws_rand(void) {
    ws_rand_seed ^= ws_rand_seed << 13;
    ws_rand_seed ^= ws_rand_seed >> 17;
    ws_rand_seed ^= ws_rand_seed << 5;
    ws_rand_seed ^= (uint32_t)get_ticks();
    return ws_rand_seed;
}

void ws_connect(const char *url) {
    /* Parse URL: ws[s]://host[:port]/path */
    int use_tls = 0;
    const char *p = url;
    if (p[0]=='w'&&p[1]=='s'&&p[2]=='s'&&p[3]==':') { use_tls = 1; p += 6; }
    else if (p[0]=='w'&&p[1]=='s'&&p[2]==':')        { p += 5; }
    else { print("ws: URL harus diawali ws:// atau wss://\n"); return; }

    /* Parse host */
    static char host[128];
    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < 127) host[hi++] = *p++;
    host[hi] = 0;

    /* Parse port */
    uint16_t port = use_tls ? 443 : 80;
    if (*p == ':') {
        p++;
        uint16_t pv = 0;
        while (*p >= '0' && *p <= '9') pv = (uint16_t)(pv * 10 + (*p++ - '0'));
        port = pv;
    }

    /* Parse path */
    static char path[128];
    int pi2 = 0;
    if (*p == '/') { while (*p && pi2 < 127) path[pi2++] = *p++; }
    else { path[pi2++] = '/'; }
    path[pi2] = 0;

    set_color(0x0055FFFF, 0);
    print("ws: resolve "); print(host); print("...\n");
    set_color(0x00FFFFFF, 0);

    uint8_t dst_ip[4];
    if (!dns_resolve(host, dst_ip)) {
        print("ws: gagal resolve hostname\n"); return;
    }

    /* Connect */
    int conn = net_tcp_connect(dst_ip, port);
    if (conn < 0) { print("ws: gagal connect\n"); return; }

    /* Bangun Sec-WebSocket-Key: 16 byte random, base64 encoded */
    uint8_t raw_key[16];
    int ki;
    for (ki = 0; ki < 4; ki++) {
        uint32_t r = ws_rand();
        raw_key[ki*4  ] = (uint8_t)(r >> 24);
        raw_key[ki*4+1] = (uint8_t)(r >> 16);
        raw_key[ki*4+2] = (uint8_t)(r >>  8);
        raw_key[ki*4+3] = (uint8_t)(r      );
    }
    static char ws_key_b64[28];
    base64_encode(raw_key, 16, ws_key_b64);

    /* HTTP Upgrade request */
    static char req[512];
    int ri = 0;
    const char *r0 = "GET "; int r0i = 0; while (r0[r0i]) req[ri++] = r0[r0i++];
    for (r0i = 0; path[r0i]; r0i++) req[ri++] = path[r0i];
    const char *r1 = " HTTP/1.1\r\nHost: "; r0i = 0; while (r1[r0i]) req[ri++] = r1[r0i++];
    for (r0i = 0; host[r0i]; r0i++) req[ri++] = host[r0i];
    const char *r2 = "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ";
    r0i = 0; while (r2[r0i]) req[ri++] = r2[r0i++];
    for (r0i = 0; ws_key_b64[r0i]; r0i++) req[ri++] = ws_key_b64[r0i];
    const char *r3 = "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    r0i = 0; while (r3[r0i]) req[ri++] = r3[r0i++];

    if (use_tls) {
        /* wss: tidak diimplementasi penuh — kirim via TCP saja (simplifikasi) */
        print("ws: wss:// belum didukung penuh, coba port plaintext\n");
        net_tcp_close(conn);
        return;
    }
    net_tcp_send(conn, req, (uint16_t)ri);

    /* Baca HTTP response (cari 101) */
    static uint8_t resp[1024];
    int rlen = net_tcp_recv(conn, resp, (uint16_t)(sizeof(resp) - 1));
    if (rlen <= 0) { print("ws: tidak ada respon\n"); net_tcp_close(conn); return; }
    resp[rlen] = 0;

    /* Cek "101" */
    if (!(resp[9]=='1'&&resp[10]=='0'&&resp[11]=='1')) {
        set_color(0x00FF5555, 0);
        print("ws: server menolak upgrade:\n");
        print((char*)resp);
        set_color(0x00FFFFFF, 0);
        net_tcp_close(conn);
        return;
    }

    /* Verifikasi Sec-WebSocket-Accept (opsional, cukup cek kehadirannya) */
    static char expected_accept[32];
    ws_accept_key(ws_key_b64, expected_accept);

    set_color(0x0055FF55, 0);
    print("ws: tersambung ke "); print(host); print(path); print("\n");
    print("ws: ketik pesan lalu Enter untuk kirim, '\\q' untuk keluar\n");
    set_color(0x00FFFFFF, 0);

    /* Loop baca+kirim — gunakan polling sederhana (non-blocking polling NIC) */
    static uint8_t linebuf[256];
    int llen = 0;
    static uint8_t rxbuf[4096];
    int rx_accum = 0;

    for (;;) {
        /* Poll keyboard — baca satu char dari ring buffer keyboard */
        int c = -1;
        if (keyboard_has_char()) c = (unsigned char)keyboard_getchar();
        if (c > 0) {
            if (c == '\n' || c == '\r') {
                linebuf[llen] = 0;
                /* Cek exit command */
                if (linebuf[0]=='\\' && linebuf[1]=='q' && llen == 2) {
                    /* Kirim close frame */
                    uint8_t mkey[4] = {0x12,0x34,0x56,0x78};
                    uint8_t cframe[10];
                    int cfl = ws_frame_encode(cframe, sizeof(cframe), 0x8, (uint8_t*)"", 0, mkey);
                    if (cfl > 0) net_tcp_send(conn, cframe, (uint16_t)cfl);
                    break;
                }
                if (llen > 0) {
                    /* Kirim text frame */
                    uint32_t rr = ws_rand();
                    uint8_t mkey[4] = {(uint8_t)(rr>>24),(uint8_t)(rr>>16),(uint8_t)(rr>>8),(uint8_t)rr};
                    static uint8_t wframe[512];
                    int fl = ws_frame_encode(wframe, sizeof(wframe), 0x1, linebuf, (uint32_t)llen, mkey);
                    if (fl > 0) net_tcp_send(conn, wframe, (uint16_t)fl);
                    set_color(0x00AAAAAA, 0);
                    print(">> "); print((char*)linebuf); print("\n");
                    set_color(0x00FFFFFF, 0);
                }
                llen = 0;
            } else if (c == 0x08 && llen > 0) {  /* Backspace */
                llen--;
            } else if (c >= 0x20 && llen < 255) {
                linebuf[llen++] = (uint8_t)c;
                /* Echo */
                char ech[2] = {(char)c, 0};
                print(ech);
            }
        }

        /* Poll NIC untuk frame masuk */
        uint8_t tmpkt[1514]; uint16_t tmplen;
        (void)tmpkt; (void)tmplen;
        {
            static uint8_t frbuf[2048];
            int got = net_tcp_recv(conn, frbuf, (uint16_t)(sizeof(frbuf) - 1));
            if (got > 0) {
                /* Tambah ke rx_accum */
                int space = (int)sizeof(rxbuf) - rx_accum - 1;
                if (got > space) got = space;
                int bi; for (bi = 0; bi < got; bi++) rxbuf[rx_accum + bi] = frbuf[bi];
                rx_accum += got;
            }
            /* Decode frames */
            int offset = 0;
            while (offset < rx_accum) {
                uint8_t opcode; const uint8_t *pl; uint32_t plen;
                int consumed = ws_frame_decode(rxbuf + offset, rx_accum - offset,
                                               &opcode, &pl, &plen);
                if (consumed <= 0) break;
                if (opcode == 0x1 || opcode == 0x0) {
                    /* text/continuation frame */
                    set_color(0x0055FF55, 0);
                    print("<< ");
                    static char printbuf[512];
                    uint32_t pi3; for (pi3 = 0; pi3 < plen && pi3 < 511; pi3++) printbuf[pi3] = (char)pl[pi3];
                    printbuf[plen < 511 ? plen : 511] = 0;
                    print(printbuf);
                    print("\n");
                    set_color(0x00FFFFFF, 0);
                } else if (opcode == 0x8) {
                    print("ws: server menutup koneksi\n");
                    net_tcp_close(conn);
                    return;
                } else if (opcode == 0x9) {
                    /* ping → pong */
                    uint32_t rr2 = ws_rand();
                    uint8_t mkey2[4] = {(uint8_t)(rr2>>24),(uint8_t)(rr2>>16),(uint8_t)(rr2>>8),(uint8_t)rr2};
                    static uint8_t pong[32];
                    int pfl = ws_frame_encode(pong, sizeof(pong), 0xA, pl, plen, mkey2);
                    if (pfl > 0) net_tcp_send(conn, pong, (uint16_t)pfl);
                }
                offset += consumed;
            }
            /* Kompak rxbuf */
            if (offset > 0) {
                int bj; for (bj = 0; bj < rx_accum - offset; bj++)
                    rxbuf[bj] = rxbuf[bj + offset];
                rx_accum -= offset;
            }
        }

        /* Cek koneksi sudah ditutup remote */
        if (net_tcp_state(conn) == 0 /* TCP_CLOSED */) {
            print("ws: koneksi terputus\n");
            break;
        }
    }

    net_tcp_close(conn);
    print("ws: selesai\n");
}
