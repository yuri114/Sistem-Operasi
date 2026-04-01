#ifndef RTL8139_H
#define RTL8139_H
/* rtl8139.h — RTL8139 NIC driver (polling, tanpa IRQ)
 * Detect via PCI scan (vendor 0x10EC, device 0x8139).
 * RX: ring buffer 8K (WRAP=0).  TX: 4 descriptor round-robin. */
#include <stdint.h>

void rtl8139_init(void);
int  rtl8139_present(void);
void rtl8139_get_mac(uint8_t out[6]);

/* Kirim frame Ethernet mentah (panjang termasuk header 14 byte, maks 1514).
 * Return 0 sukses, -1 gagal / timeout. */
int  rtl8139_send(const uint8_t *data, uint16_t len);

/* Terima satu frame ke buf (maks 1514 byte), simpan panjang ke *out_len.
 * Return 0 jika ada paket, -1 jika buffer kosong. */
int  rtl8139_recv(uint8_t *buf, uint16_t *out_len);

#endif
