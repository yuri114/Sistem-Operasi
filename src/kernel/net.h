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

#endif
