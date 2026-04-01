/* rtl8139.c — RTL8139 NIC driver (polling, tanpa IRQ)
 * PCI scan → BAR0 (I/O port base) → reset → TX/RX init → send/recv.
 * Semua akses memakai I/O port karena BAR0 = I/O space (bit 0 set).
 * Buffer RX/TX dideklarasikan static (BSS) sehingga VA = PA
 * (kernel memakai identity mapping untuk memori rendah). */
#include "rtl8139.h"
#include <stdint.h>

#define RTL_VENDOR  0x10EC
#define RTL_DEVICE  0x8139

/* Register offset dari iobase */
#define RTL_IDR0     0x00   /* MAC address (6 byte) */
#define RTL_TSD0     0x10   /* TX Status Desc 0-3 (4×4B) */
#define RTL_TSAD0    0x20   /* TX Start Address 0-3 (4×4B) */
#define RTL_RBSTART  0x30   /* RX Buffer Start Address (32-bit PA) */
#define RTL_CMD      0x37   /* Command Register */
#define RTL_CAPR     0x38   /* Current Address of Packet Read (16-bit) */
#define RTL_IMR      0x3C   /* Interrupt Mask Register */
#define RTL_ISR      0x3E   /* Interrupt Status Register */
#define RTL_TCR      0x40   /* TX Config Register */
#define RTL_RCR      0x44   /* RX Config Register */
#define RTL_CONFIG1  0x52   /* Config Register 1 */

/* RX ring buffer: 8K data + 16 header guard + 1518 overflow guard (WRAP=0).
 * Dengan WRAP=0, NIC menulis paket secara linear tanpa wrap di batas 8K.
 * Guard bytes memastikan kita bisa membaca paket yang melewati 8K. */
#define RX_BUF_SIZE  (8192 + 16 + 1518)
#define TX_BUF_SIZE  1600

static uint8_t  rx_buf[RX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t  tx_buf[4][TX_BUF_SIZE] __attribute__((aligned(4)));

static uint16_t g_iobase  = 0;
static int      g_tx_idx  = 0;
static uint16_t g_rx_ptr  = 0;   /* offset berikutnya untuk dibaca di ring */
static uint8_t  g_mac[6];
static int      g_present = 0;

/* ------------------------------------------------------------------ */
/* I/O port helpers (static inline → tidak ada simbol yang di-export)  */
/* ------------------------------------------------------------------ */
static inline void io_outb(uint16_t p, uint8_t v)  { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void io_outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void io_outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t  io_inb(uint16_t p) { uint8_t  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t io_inw(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint32_t io_inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }

/* ------------------------------------------------------------------ */
/* PCI config space (I/O port 0xCF8/0xCFC)                            */
/* ------------------------------------------------------------------ */
static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t reg) {
    uint32_t addr = 0x80000000u
        | ((uint32_t)bus << 16)
        | ((uint32_t)(dev & 31) << 11)
        | (reg & 0xFC);
    io_outl(0xCF8, addr);
    return io_inl(0xCFC);
}
static uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t reg) {
    return (uint16_t)(pci_read32(bus, dev, reg & ~3u) >> ((reg & 2) * 8));
}
static void pci_write16(uint8_t bus, uint8_t dev, uint8_t reg, uint16_t val) {
    uint32_t addr = 0x80000000u
        | ((uint32_t)bus << 16)
        | ((uint32_t)(dev & 31) << 11)
        | (reg & 0xFC);
    io_outl(0xCF8, addr);
    uint32_t cur = io_inl(0xCFC);
    int sh = (reg & 2) * 8;
    cur = (cur & ~(0xFFFFu << sh)) | ((uint32_t)val << sh);
    io_outl(0xCF8, addr);
    io_outl(0xCFC, cur);
}

/* Scan PCI bus 0-7, semua device, cari RTL8139.
 * Jika ditemukan: aktifkan I/O space + bus master, kembalikan BAR0 (I/O base).
 * Return 0 jika tidak ditemukan. */
static uint16_t pci_find_rtl8139(void) {
    uint8_t bus, dev;
    for (bus = 0; bus < 8; bus++) {
        for (dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32(bus, dev, 0);
            if ((id & 0xFFFF) != RTL_VENDOR) continue;
            if ((id >> 16)    != RTL_DEVICE)  continue;
            /* Aktifkan I/O Space (bit 0) + Bus Master (bit 2) */
            uint16_t cmd = pci_read16(bus, dev, 0x04);
            pci_write16(bus, dev, 0x04, (uint16_t)(cmd | 0x05));
            /* BAR0 = I/O port base (bit 0 = I/O space indicator, bukan bagian alamat) */
            uint32_t bar0 = pci_read32(bus, dev, 0x10);
            return (uint16_t)(bar0 & ~3u);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Inisialisasi driver                                                  */
/* ------------------------------------------------------------------ */
void rtl8139_init(void) {
    g_iobase = pci_find_rtl8139();
    if (!g_iobase) return;

    /* Power on: clear CONFIG1 bit 2 (PWRDN) */
    io_outb(g_iobase + RTL_CONFIG1, 0x00);

    /* Software reset — tunggu sampai RST bit (bit 4 CMD) clear */
    io_outb(g_iobase + RTL_CMD, 0x10);
    uint32_t timeout = 500000;
    while ((io_inb(g_iobase + RTL_CMD) & 0x10) && --timeout);
    if (!timeout) { g_iobase = 0; return; }

    /* Baca MAC address dari IDR0-5 */
    int i;
    for (i = 0; i < 6; i++) g_mac[i] = io_inb(g_iobase + RTL_IDR0 + i);

    /* Set RX buffer start address (physical = virtual, identity-mapped) */
    io_outl(g_iobase + RTL_RBSTART, (uint32_t)(uintptr_t)rx_buf);

    /* Enable TX + RX */
    io_outb(g_iobase + RTL_CMD, 0x0C);

    /* RX config:
     *   bit[3:0] = 0xF → AB|AM|APM|AAP (terima semua)
     *   bit[7]   = 0   → WRAP=0 (tulis linear, tidak wrap di 8K)
     *   bit[12:11]=00  → RBLEN=8K+16
     *   bit[18:16]=110 → MXDMA=256B burst */
    io_outl(g_iobase + RTL_RCR, 0x0006000F);

    /* TX config: IFG=11b (standard), MXDMA=111b (unlimited) */
    io_outl(g_iobase + RTL_TCR, 0x03000700);

    /* Masking semua IRQ (kita pakai polling) */
    io_outw(g_iobase + RTL_IMR, 0x0000);
    io_outw(g_iobase + RTL_ISR, 0xFFFF);     /* clear semua pending flag */

    /* CAPR awal = 0xFFF0 artinya NIC mulai tulis paket di offset 0 */
    io_outw(g_iobase + RTL_CAPR, 0xFFF0);
    g_rx_ptr = 0;
    g_tx_idx = 0;
    g_present = 1;
}

int rtl8139_present(void) { return g_present; }

void rtl8139_get_mac(uint8_t out[6]) {
    int i;
    for (i = 0; i < 6; i++) out[i] = g_mac[i];
}

/* ------------------------------------------------------------------ */
/* Kirim satu frame Ethernet                                           */
/* ------------------------------------------------------------------ */
int rtl8139_send(const uint8_t *data, uint16_t len) {
    if (!g_present || len > TX_BUF_SIZE) return -1;

    /* Salin frame ke TX buffer (DMA buffer harus statis/physical) */
    uint8_t *buf = tx_buf[g_tx_idx];
    uint16_t i;
    for (i = 0; i < len; i++) buf[i] = data[i];

    /* Tulis alamat fisik buffer ke TSAD, lalu panjang ke TSD.
     * Menulis TSD dengan OWN=0 (bit 13 = 0 karena len < 8192)
     * menyerahkan descriptor ke NIC untuk dikirim. */
    io_outl(g_iobase + RTL_TSAD0 + g_tx_idx * 4, (uint32_t)(uintptr_t)buf);
    io_outl(g_iobase + RTL_TSD0  + g_tx_idx * 4, (uint32_t)len);

    /* Poll TOK (bit 15) atau TABT (bit 31) */
    uint32_t to = 2000000;
    while (--to) {
        uint32_t tsd = io_inl(g_iobase + RTL_TSD0 + g_tx_idx * 4);
        if (tsd & (1u << 15)) break;   /* TOK: transmit OK */
        if (tsd & (1u << 31)) return -1; /* TABT: aborted */
    }

    g_tx_idx = (g_tx_idx + 1) & 3;
    return to ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Terima satu frame dari RX ring buffer                              */
/* ------------------------------------------------------------------ */
int rtl8139_recv(uint8_t *buf, uint16_t *out_len) {
    if (!g_present) return -1;

    /* CMD bit 0 (BUFE) = 1 → buffer kosong, tidak ada paket */
    if (io_inb(g_iobase + RTL_CMD) & 0x01) return -1;

    /* Baca header paket dari ring: [status 2B][pkt_len 2B][data ...] */
    uint8_t  *ring    = rx_buf + g_rx_ptr;
    uint16_t  status  = (uint16_t)(ring[0] | ((uint16_t)ring[1] << 8));
    uint16_t  pkt_len = (uint16_t)(ring[2] | ((uint16_t)ring[3] << 8));

    /* Status bit 0 (ROK) harus set; pkt_len termasuk 4 byte CRC */
    if (!(status & 0x01) || pkt_len < 8 || pkt_len > 1522) {
        /* Paket rusak: skip */
        g_rx_ptr = (uint16_t)((g_rx_ptr + 4 + 3) & ~3u);
        if (g_rx_ptr >= 8192) g_rx_ptr -= 8192;
        io_outw(g_iobase + RTL_CAPR, (uint16_t)(g_rx_ptr - 16));
        io_outw(g_iobase + RTL_ISR,  0x0001);
        return -1;
    }

    /* Data Ethernet = pkt_len - 4 (tanpa CRC) */
    uint16_t data_len = pkt_len - 4;
    if (data_len > 1514) data_len = 1514;

    /* Salin data: rx_buf[g_rx_ptr+4 .. +4+data_len-1].
     * Aman karena WRAP=0 + alokasi RX_BUF_SIZE = 8K+1518+16,
     * sehingga paket terbesar yang dimulai di offset 8191 tetap dalam buffer. */
    uint16_t i;
    for (i = 0; i < data_len; i++)
        buf[i] = rx_buf[g_rx_ptr + 4 + i];
    *out_len = data_len;

    /* Maju rx_ptr (4-byte aligned), wrap di 8K */
    g_rx_ptr = (uint16_t)((g_rx_ptr + 4 + pkt_len + 3) & ~3u);
    if (g_rx_ptr >= 8192) g_rx_ptr -= 8192;

    /* Update CAPR: uint16_t underflow (g_rx_ptr < 16) ditangani secara alami */
    io_outw(g_iobase + RTL_CAPR, (uint16_t)(g_rx_ptr - 16));
    io_outw(g_iobase + RTL_ISR,  0x0001);  /* clear ROK flag */
    return 0;
}
