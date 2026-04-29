#ifndef NET_H
#define NET_H
/* net.h — minimal network stack: Ethernet, ARP, IPv4, ICMP
 * Konfigurasi statis QEMU SLIRP:
 *   IP  guest  : 10.0.2.15
 *   IP  gateway: 10.0.2.2   (tempat kita ping untuk test)
 *   MAC : dibaca dari RTL8139 hardware */
#include <stdint.h>

/* Inisialisasi: panggil rtl8139_init(), baca MAC dari NIC */
void net_init(void);

/* Return 1 jika RTL8139 berhasil diinit */
int  net_present(void);

/* Tampilkan info interface (MAC, IP, gateway) ke shell */
void net_ifconfig(void);

/* Kirim 'count' ICMP echo request ke dst_ip, tunggu reply tiap 3 detik.
 * dst_ip harus array 4 byte (IPv4).
 * Return 0 atau -1 jika NIC tidak ada. */
int  net_ping(const uint8_t dst_ip[4], int count);

/* --- UDP --- */
/* Kirim UDP datagram ke dst_ip:dst_port dari src_port.
 * Return 0 sukses, -1 gagal. */
int  net_udp_send(const uint8_t dst_ip[4], uint16_t dst_port,
                  uint16_t src_port, const void *data, uint16_t dlen);

/* --- TCP --- */
/* Buat koneksi TCP ke dst_ip:dst_port (blocking, timeout 5s).
 * Return conn_id (0..3) sukses, -1 = timeout, -2 = connection refused (RST). */
int  net_tcp_connect(const uint8_t dst_ip[4], uint16_t dst_port);

/* Kirim data ke koneksi id (max 1400 byte sekali kirim).
 * Return jumlah byte terkirim, atau -1 gagal. */
int  net_tcp_send(int id, const void *data, uint16_t len);

/* Terima data dari koneksi id ke buf (max maxlen byte, timeout 5s).
 * Return jumlah byte diterima, 0 = koneksi ditutup, -1 = error. */
int  net_tcp_recv(int id, void *buf, uint16_t maxlen);

/* Tutup koneksi TCP (kirim FIN, tunggu handshake selesai). */
void net_tcp_close(int id);

/* Kembalikan state koneksi (0=CLOSED, 1=SYN_SENT, 2=ESTABLISHED, ...) */
int  net_tcp_state(int id);

/* Proses satu paket masuk dari NIC (panggil dari polling loop). */
void net_poll(void);

/* F-X1/X3: Periodic TCP timer — dipanggil dari timer IRQ tiap ~10ms. */
void net_tcp_tick(void);

/* F-X4: DNS resolver — resolve hostname ke IPv4 via UDP ke 8.8.8.8:53.
 * Return 1 + out_ip diisi jika sukses, 0 jika timeout/error. */
int  dns_resolve(const char *hostname, uint8_t out_ip[4]);

/* F-Y2: HTTP GET client — fetch URL via HTTP/1.0, cetak response ke layar.
 * url format: "http://hostname/path" atau "http://hostname"
 * Return 0 sukses, -1 error. */
int  http_get(const char *url);

/* F-AJ: HTTPS GET client — fetch URL via TLS 1.3 (port 443).
 * url format: "https://hostname/path" atau "https://hostname"
 * Return 0 sukses, -1 error. */
int  https_get(const char *url);

/* ================================================================
 * Fondasi AL — NTP Client
 * ================================================================ */
/* Sinkronisasi waktu via NTP ke pool.ntp.org (UDP port 123).
 * Tampilkan tanggal/jam UTC ke layar.
 * Return 1 sukses, 0 gagal/timeout. */
int  ntp_sync(void);

/* ================================================================
 * Fondasi AM — HTTP Server (TCP listen/accept)
 * ================================================================ */
/* Mulai listen di port. Return listen_id (>=0) sukses, -1 gagal. */
int  net_tcp_listen(uint16_t port);
/* Tunggu koneksi masuk. Return conn_id (>=0) sukses, -1 timeout, -2 invalid. */
int  net_tcp_accept(int listen_id, uint32_t timeout_ms);
/* Berhenti listen. */
void net_tcp_unlisten(int listen_id);

/* ================================================================
 * Fondasi AP — IPv6 + ICMPv6 ping6
 * ================================================================ */
/* Kirim ICMPv6 Echo Request ke dst_ip6 sebanyak count kali.
 * dst_ip6: array 16 byte (IPv6 address).
 * Return 0 sukses, -1 NIC tidak ada. */
int  net_ping6(const uint8_t dst_ip6[16], int count);

#endif
