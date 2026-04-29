/* virtio_blk.c — Fondasi AO: VirtIO Block Device Driver (Legacy 0.9.5)
 *
 * Mendukung VirtIO blk PCI device (Vendor 0x1AF4, Device 0x1001).
 * Menggunakan virtqueue sederhana dengan 16 deskriptor.
 *
 * QEMU invocation untuk menambah VirtIO disk:
 *   -drive file=disk.img,if=virtio,format=raw
 *
 * Register PCI BAR0 (I/O):
 *   +0x00  DEVICE_FEATURES (R)
 *   +0x04  GUEST_FEATURES  (W)
 *   +0x08  QUEUE_PFN       (W) — alamat fisik virtqueue >> 12
 *   +0x0E  QUEUE_SIZE      (R) — jumlah deskriptor
 *   +0x10  QUEUE_SELECT    (W) — pilih virtqueue (0 = blk)
 *   +0x14  QUEUE_NOTIFY    (W) — kick queue
 *   +0x12  DEVICE_STATUS   (W)
 *   +0x13  ISR_STATUS      (R)
 */
#include "virtio_blk.h"
#include <stdint.h>

extern void     print(const char *s);
extern void     itoa(uint32_t n, char *buf);
extern void     set_color(uint32_t fg, uint32_t bg);

/* ================================================================
 * PCI Config Space Access (port I/O)
 * ================================================================ */
static uint32_t pci_cfg_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    return (uint32_t)(0x80000000u
        | ((uint32_t)bus << 16)
        | ((uint32_t)dev << 11)
        | ((uint32_t)fn  <<  8)
        | ((uint32_t)(reg & 0xFC)));
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t addr = pci_cfg_addr(bus, dev, fn, reg);
    __asm__ volatile ("outl %0, %w1" :: "a"(addr), "Nd"((uint16_t)0xCF8));
    uint32_t val;
    __asm__ volatile ("inl %w1, %0" : "=a"(val) : "Nd"((uint16_t)0xCFC));
    return val;
}
static uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t v = pci_read32(bus, dev, fn, reg & 0xFC);
    return (uint16_t)((v >> ((reg & 2) * 8)) & 0xFFFF);
}

/* Port I/O */
static void outl_p(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %w1" :: "a"(val), "d"(port));
}
static void outw_p(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %w1" :: "a"(val), "d"(port));
}
static void outb_p(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %w1" :: "a"(val), "d"(port));
}
static uint32_t inl_p(uint16_t port) {
    uint32_t v; __asm__ volatile ("inl %w1, %0" : "=a"(v) : "d"(port)); return v;
}
static uint16_t inw_p(uint16_t port) {
    uint16_t v; __asm__ volatile ("inw %w1, %0" : "=a"(v) : "d"(port)); return v;
}
static uint8_t inb_p(uint16_t port) {
    uint8_t v; __asm__ volatile ("inb %w1, %0" : "=a"(v) : "d"(port)); return v;
}

/* ================================================================
 * VirtIO Register offsets (relative to BAR0 I/O base)
 * ================================================================ */
#define VIRT_DEVICE_FEATURES  0x00
#define VIRT_GUEST_FEATURES   0x04
#define VIRT_QUEUE_PFN        0x08
#define VIRT_QUEUE_SIZE       0x0C
#define VIRT_QUEUE_SELECT     0x0E
#define VIRT_QUEUE_NOTIFY     0x10
#define VIRT_DEVICE_STATUS    0x12
#define VIRT_ISR_STATUS       0x13

/* Device status bits */
#define VIRT_STATUS_ACK       0x01
#define VIRT_STATUS_DRIVER    0x02
#define VIRT_STATUS_DRIVER_OK 0x04
#define VIRT_STATUS_FAILED    0x80

/* VirtIO BLK request types */
#define VIRTIO_BLK_T_IN       0   /* read */
#define VIRTIO_BLK_T_OUT      1   /* write */
#define VIRTIO_BLK_T_FLUSH    4   /* flush */

/* VirtIO BLK status */
#define VIRTIO_BLK_S_OK       0
#define VIRTIO_BLK_S_IOERR    1
#define VIRTIO_BLK_S_UNSUPP   2

/* Descriptor flags */
#define VRING_DESC_F_NEXT     1
#define VRING_DESC_F_WRITE    2   /* device writes this descriptor */

/* ================================================================
 * Virtqueue structures (4096-byte aligned)
 * ================================================================ */
#define VRING_SIZE  16  /* power of 2; harus ≥ 1 */

typedef struct __attribute__((packed)) {
    uint64_t addr;      /* physical address of buffer */
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} VringDesc;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VRING_SIZE];
} VringAvail;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} VringUsedElem;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    VringUsedElem ring[VRING_SIZE];
} VringUsed;

/* Entire virtqueue laid out in one aligned 4096-byte page.
 * Layout: descriptors | available ring | padding | used ring
 * Semua di-align ke 4096 bytes agar QUEUE_PFN benar. */
#define VRING_PAGES  2  /* cukup untuk VRING_SIZE=16 */
static uint8_t vring_mem[VRING_PAGES * 4096] __attribute__((aligned(4096)));

static VringDesc  *vring_desc  = 0;
static VringAvail *vring_avail = 0;
static VringUsed  *vring_used  = 0;
static uint16_t    last_used_idx = 0;

/* VirtIO BLK request header */
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} VirtioBlkReq;

/* ================================================================
 * State
 * ================================================================ */
static int       vblk_present  = 0;
static uint16_t  vblk_io_base  = 0;
static uint64_t  vblk_capacity = 0;  /* in sectors (512 bytes each) */

/* ================================================================
 * Scan PCI untuk VirtIO blk
 * ================================================================ */
static int find_virtio_blk(uint16_t *io_base_out) {
    uint8_t bus, dev;
    for (bus = 0; bus < 8; bus++) {
        for (dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32(bus, dev, 0, 0);
            if (id == 0xFFFFFFFF) continue;
            uint16_t vendor = (uint16_t)(id & 0xFFFF);
            uint16_t device = (uint16_t)(id >> 16);
            if (vendor == 0x1AF4 && (device == 0x1001 || device == 0x1042)) {
                /* Baca BAR0 (I/O) */
                uint32_t bar0 = pci_read32(bus, dev, 0, 0x10);
                if (bar0 & 1) {  /* I/O space */
                    *io_base_out = (uint16_t)(bar0 & 0xFFFC);
                    /* Enable bus mastering + I/O */
                    uint16_t cmd = pci_read16(bus, dev, 0, 0x04);
                    cmd |= 0x0005;
                    /* Write back via full 32-bit */
                    uint32_t cmd32 = pci_read32(bus, dev, 0, 0x04);
                    cmd32 = (cmd32 & 0xFFFF0000) | cmd;
                    { uint32_t a2 = pci_cfg_addr(bus, dev, 0, 0x04);
                      __asm__ volatile ("outl %0, %w1" :: "a"(a2), "Nd"((uint16_t)0xCF8)); }
                    __asm__ volatile ("outl %0, %w1" :: "a"(cmd32), "Nd"((uint16_t)0xCFC));
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* ================================================================
 * virtio_blk_init() — init driver
 * ================================================================ */
void virtio_blk_init(void) {
    uint16_t io;
    if (!find_virtio_blk(&io)) {
        return;  /* tidak ada VirtIO blk */
    }
    vblk_io_base = io;

    /* 1. Reset device */
    outb_p((uint16_t)(io + VIRT_DEVICE_STATUS), 0);

    /* 2. ACK + DRIVER */
    outb_p((uint16_t)(io + VIRT_DEVICE_STATUS), VIRT_STATUS_ACK | VIRT_STATUS_DRIVER);

    /* 3. Read device features (log tapi tidak filter) */
    uint32_t features = inl_p((uint16_t)(io + VIRT_DEVICE_FEATURES));
    (void)features;
    /* Set guest features = 0 (tidak pakai feature negosiasi kompleks) */
    outl_p((uint16_t)(io + VIRT_GUEST_FEATURES), 0);

    /* 4. Setup virtqueue 0 */
    outw_p((uint16_t)(io + VIRT_QUEUE_SELECT), 0);
    uint16_t qsize = inw_p((uint16_t)(io + VIRT_QUEUE_SIZE));
    if (qsize == 0) qsize = VRING_SIZE;
    if (qsize > VRING_SIZE) qsize = VRING_SIZE;

    /* Layout di vring_mem:
     * [0..N*16-1]      = VringDesc[]  (16 bytes each)
     * [N*16..]         = VringAvail
     * [4096..]         = VringUsed (page 1, 4096-byte aligned)
     */
    int i;
    for (i = 0; i < (int)sizeof(vring_mem); i++) vring_mem[i] = 0;

    vring_desc  = (VringDesc  *)(vring_mem);
    vring_avail = (VringAvail *)(vring_mem + qsize * sizeof(VringDesc));
    /* VringUsed harus di 4096-byte aligned boundary */
    vring_used  = (VringUsed  *)(vring_mem + 4096);

    /* Init free chain */
    for (i = 0; i < qsize - 1; i++) {
        vring_desc[i].flags = VRING_DESC_F_NEXT;
        vring_desc[i].next  = (uint16_t)(i + 1);
    }
    vring_desc[qsize - 1].flags = 0;
    vring_desc[qsize - 1].next  = 0;

    /* Beritahu device alamat virtqueue (physical page number) */
    uint64_t phys_addr = (uint64_t)(uintptr_t)vring_mem;
    outl_p((uint16_t)(io + VIRT_QUEUE_PFN), (uint32_t)(phys_addr >> 12));

    /* 5. DRIVER_OK */
    outb_p((uint16_t)(io + VIRT_DEVICE_STATUS),
           VIRT_STATUS_ACK | VIRT_STATUS_DRIVER | VIRT_STATUS_DRIVER_OK);

    /* 6. Baca kapasitas (di config space: offset 0x18 dari BAR0 untuk legacy) */
    uint32_t cap_lo = inl_p((uint16_t)(io + 0x14));
    uint32_t cap_hi = inl_p((uint16_t)(io + 0x18));
    vblk_capacity = ((uint64_t)cap_hi << 32) | cap_lo;

    vblk_present = 1;
    last_used_idx = 0;

    set_color(0x0055FF55, 0);
    print("virtio_blk: OK, kapasitas=");
    { char nb[12]; itoa((uint32_t)(vblk_capacity & 0xFFFFFFFF), nb); print(nb); }
    print(" sektor (");
    { char nb[12]; itoa((uint32_t)((vblk_capacity * 512) / (1024*1024)), nb); print(nb); }
    print(" MB)\n");
    set_color(0x00FFFFFF, 0);
}

int virtio_blk_present(void) { return vblk_present; }

/* ================================================================
 * Kirim satu request lewat virtqueue (3 deskriptor: header, data, status)
 * ================================================================ */
static int vblk_do_request(uint32_t type, uint64_t sector,
                           uint8_t *buf, uint32_t buflen) {
    if (!vblk_present) return -1;

    static VirtioBlkReq req_hdr;
    static uint8_t      req_status;

    req_hdr.type     = type;
    req_hdr.reserved = 0;
    req_hdr.sector   = sector;
    req_status       = 0xFF;  /* belum diisi device */

    /* Alokasi 3 deskriptor dari free list (0, 1, 2) */
    int d0 = 0, d1 = 1, d2 = 2;

    /* Desc 0: request header (read-only oleh device) */
    vring_desc[d0].addr  = (uint64_t)(uintptr_t)&req_hdr;
    vring_desc[d0].len   = sizeof(VirtioBlkReq);
    vring_desc[d0].flags = VRING_DESC_F_NEXT;
    vring_desc[d0].next  = (uint16_t)d1;

    /* Desc 1: data buffer */
    vring_desc[d1].addr  = (uint64_t)(uintptr_t)buf;
    vring_desc[d1].len   = buflen;
    vring_desc[d1].flags = (uint16_t)(VRING_DESC_F_NEXT |
                            (type == VIRTIO_BLK_T_IN ? VRING_DESC_F_WRITE : 0));
    vring_desc[d1].next  = (uint16_t)d2;

    /* Desc 2: status byte (writable oleh device) */
    vring_desc[d2].addr  = (uint64_t)(uintptr_t)&req_status;
    vring_desc[d2].len   = 1;
    vring_desc[d2].flags = VRING_DESC_F_WRITE;
    vring_desc[d2].next  = 0;

    /* Masukkan d0 ke available ring */
    uint16_t avail_idx = vring_avail->idx % VRING_SIZE;
    vring_avail->ring[avail_idx] = (uint16_t)d0;
    /* Memory barrier sebelum update idx */
    __asm__ volatile ("" ::: "memory");
    vring_avail->idx++;
    __asm__ volatile ("" ::: "memory");

    /* Kick device (queue 0) */
    outw_p((uint16_t)(vblk_io_base + VIRT_QUEUE_NOTIFY), 0);

    /* Tunggu used ring diupdate (polling timeout ~10000 iterasi) */
    volatile int timeout = 100000;
    while (timeout-- > 0) {
        __asm__ volatile ("" ::: "memory");
        if (vring_used->idx != last_used_idx) break;
    }
    if (timeout <= 0) return -2;  /* timeout */
    last_used_idx = vring_used->idx;

    if (req_status != VIRTIO_BLK_S_OK) return -3;
    return 0;
}

/* ================================================================
 * Public API
 * ================================================================ */
int virtio_blk_read(uint64_t sector, uint8_t *buf, uint32_t nsectors) {
    if (!vblk_present) return -1;
    uint32_t i;
    for (i = 0; i < nsectors; i++) {
        int r = vblk_do_request(VIRTIO_BLK_T_IN, sector + i, buf + i * 512, 512);
        if (r < 0) return r;
    }
    return 0;
}

int virtio_blk_write(uint64_t sector, const uint8_t *buf, uint32_t nsectors) {
    if (!vblk_present) return -1;
    uint32_t i;
    for (i = 0; i < nsectors; i++) {
        int r = vblk_do_request(VIRTIO_BLK_T_OUT, sector + i, (uint8_t*)(buf + i * 512), 512);
        if (r < 0) return r;
    }
    return 0;
}

uint64_t virtio_blk_capacity(void) { return vblk_capacity; }
