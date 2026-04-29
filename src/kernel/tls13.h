#ifndef TLS13_H
#define TLS13_H
/* tls13.h — TLS 1.3 minimal client
 * Tidak memverifikasi sertifikat server.
 * Cipher suite: TLS_AES_128_GCM_SHA256 (0x1301)
 * Key exchange: X25519 */
#include <stdint.h>

/* Buka koneksi TLS 1.3 ke server di atas TCP conn_id yang sudah ada.
 * host = hostname untuk SNI (boleh NULL untuk skip SNI).
 * Lakukan full handshake (ClientHello → Finished).
 * Return 0 sukses, -1 error. */
int  tls13_connect(int tcp_conn_id, const char *host);

/* Kirim data lewat TLS. Return byte terkirim atau -1. */
int  tls13_send(int tcp_conn_id, const void *data, uint32_t len);

/* Terima data dari TLS (blocking, timeout ~5 detik).
 * Return bytes diterima, 0 = koneksi tutup, -1 = error. */
int  tls13_recv(int tcp_conn_id, void *buf, uint32_t maxlen);

/* Tutup koneksi TLS (kirim close_notify alert). */
void tls13_close(int tcp_conn_id);

/* Cek apakah tcp_conn_id sudah punya sesi TLS aktif. */
int  tls13_is_open(int tcp_conn_id);

#endif /* TLS13_H */
