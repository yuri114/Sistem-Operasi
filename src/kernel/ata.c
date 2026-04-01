/*
 * ata.c — ATA driver untuk Primary Slave (disk kedua)
 *
 * Disk kedua digunakan sebagai persistent storage untuk filesystem.
 * Mode utama: Bus Master DMA (PIIX3/PIIX4).
 * Fallback   : PIO polling jika DMA tidak tersedia/gagal.
 *
 * Port ATA primary channel:
 *   0x1F0  DATA (16-bit)
 *   0x1F1  ERROR/FEATURES
 *   0x1F2  SECTOR COUNT
 *   0x1F3  LBA LOW
 *   0x1F4  LBA MID
 *   0x1F5  LBA HIGH
 *   0x1F6  DRIVE/HEAD  (0xE0=master LBA, 0xF0=slave LBA)
 *   0x1F7  STATUS/COMMAND
 *   0x3F6  ALT STATUS (baca saja, untuk delay)
 */
#include "ata.h"

#define ATA_DATA     0x1F0u
#define ATA_FEATURES 0x1F1u
#define ATA_SECT_CNT 0x1F2u
#define ATA_LBA_LO   0x1F3u
#define ATA_LBA_MID  0x1F4u
#define ATA_LBA_HI   0x1F5u
#define ATA_DRIVE    0x1F6u
#define ATA_STAT_CMD 0x1F7u
#define ATA_ALT_STAT 0x3F6u

#define ATA_CMD_READ     0x20u  /* READ SECTORS (PIO) */
#define ATA_CMD_WRITE    0x30u  /* WRITE SECTORS (PIO) */
#define ATA_CMD_FLUSH    0xE7u  /* FLUSH CACHE */
#define ATA_CMD_READ_DMA 0xC8u  /* READ DMA */
#define ATA_CMD_WRIT_DMA 0xCAu  /* WRITE DMA */
#define ATA_CMD_IDENTIFY 0xECu  /* IDENTIFY DEVICE */

#define ATA_BSY 0x80u
#define ATA_DRQ 0x08u
#define ATA_ERR 0x01u

/* LBA28 sector boundary: max addressable sector */
#define ATA_LBA28_MAX 0x0FFFFFFFu

/* Primary Slave drive select (LBA mode) */
#define FS_DRIVE_SEL 0xF0u

/* ATAPI signature bytes on LBA_MID / LBA_HI after IDENTIFY attempt */
#define ATAPI_SIG_MID 0x14u
#define ATAPI_SIG_HI  0xEBu

/* ------------------------------------------------------------------ */
/* PCI Config Space helpers                                             */
/* ------------------------------------------------------------------ */
#define PCI_CFG_ADDR 0xCF8u
#define PCI_CFG_DATA 0xCFCu

static inline void outl32(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint32_t inl32(uint16_t port) {
    uint32_t r;
    __asm__ volatile ("inl %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  | (reg & 0xFCu);
    outl32(PCI_CFG_ADDR, addr);
    return inl32(PCI_CFG_DATA);
}
static void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  | (reg & 0xFCu);
    outl32(PCI_CFG_ADDR, addr);
    outl32(PCI_CFG_DATA, val);
}

/* ------------------------------------------------------------------ */
/* Bus Master IDE (BMIDE)                                              */
/* ------------------------------------------------------------------ */
/* Primary channel BMIDE register offsets from bmide_base */
#define BM_CMD  0u  /* Command register  */
#define BM_STAT 2u  /* Status register   */
#define BM_PRDT 4u  /* PRDT address (32-bit) */

#define BM_CMD_START 0x01u  /* Start DMA transfer */
#define BM_CMD_WDIR  0x08u  /* 0=read disk→mem, 1=write mem→disk */
#define BM_STAT_ACT  0x01u  /* Transfer active */
#define BM_STAT_FAIL 0x02u  /* DMA error */
#define BM_STAT_INTR 0x04u  /* Interrupt (transfer done) */

/* Physical Region Descriptor (1 entry — 1 sector at a time) */
static volatile struct {
    uint32_t phys_addr;   /* Physical address of buffer  */
    uint16_t byte_count;  /* Transfer size (0 = 65536)   */
    uint16_t flags;       /* bit15 = End-Of-Table        */
} __attribute__((aligned(4))) bmide_prdt;

static uint16_t bmide_base  = 0;   /* 0 → DMA unavailable */
static int      dma_ready   = 0;

/* ------------------------------------------------------------------ */
/* Basic I/O helpers                                                    */
/* ------------------------------------------------------------------ */
static inline void outb8(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb8(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline uint16_t inw16(uint16_t port) {
    uint16_t r;
    __asm__ volatile ("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outw16(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" :: "a"(val), "Nd"(port));
}

/* ------------------------------------------------------------------ */
/* ATA wait helpers                                                     */
/* ------------------------------------------------------------------ */

/* Delay ~400 ns — baca alt-status 4x */
static void ata_delay400(void) {
    inb8(ATA_ALT_STAT); inb8(ATA_ALT_STAT);
    inb8(ATA_ALT_STAT); inb8(ATA_ALT_STAT);
}

/* Tunggu BSY=0. Return 0=OK, -1=timeout. */
static int ata_wait_not_busy(void) {
    int n = 0x100000;
    while (n--) {
        if ((inb8(ATA_STAT_CMD) & ATA_BSY) == 0) return 0;
    }
    return -1;
}

/* Tunggu BSY=0 dan DRQ=1. Return 0=OK, -1=error/timeout. */
static int ata_wait_drq(void) {
    int n = 0x100000;
    while (n--) {
        uint8_t s = inb8(ATA_STAT_CMD);
        if (s & ATA_ERR)            return -1;
        if (s & ATA_BSY)            continue;
        if (s & ATA_DRQ)            return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Bus Master IDE init — cari PIIX3/PIIX4 via PCI                      */
/* ------------------------------------------------------------------ */
static void bmide_detect(void) {
    /* PCI 00:01.1 = Intel PIIX3/PIIX4 IDE controller */
    uint32_t id = pci_read32(0, 1, 1, 0x00);
    if ((id & 0xFFFFu) != 0x8086u) return;  /* vendor bukan Intel */

    /* BAR4 = Bus Master IDE base I/O address */
    uint32_t bar4 = pci_read32(0, 1, 1, 0x20);
    if ((bar4 & 1u) == 0) return;           /* bukan I/O mapped */
    bmide_base = (uint16_t)(bar4 & 0xFFFCu);

    /* Pastikan Bus Master bit (bit 2) di PCI Command register aktif */
    uint32_t cmd_reg = pci_read32(0, 1, 1, 0x04);
    if (!(cmd_reg & 0x04u)) {
        pci_write32(0, 1, 1, 0x04, cmd_reg | 0x04u);
    }
    dma_ready = 1;
}

/* ------------------------------------------------------------------ */
/* Drive type detection via IDENTIFY                                    */
/* ------------------------------------------------------------------ */
static ata_drive_type_t drive_type = ATA_DRIVE_NONE;
static int disk_present = 0;

static void detect_drive_type(void) {
    /* Select Primary Slave */
    outb8(ATA_DRIVE, 0xB0u);
    ata_delay400();

    /* Status 0xFF = floating bus = tidak ada drive */
    uint8_t st = inb8(ATA_STAT_CMD);
    if (st == 0xFFu || st == 0x00u) { drive_type = ATA_DRIVE_NONE; return; }

    /* Kirim IDENTIFY (0xEC) */
    outb8(ATA_SECT_CNT, 0); outb8(ATA_LBA_LO, 0);
    outb8(ATA_LBA_MID,  0); outb8(ATA_LBA_HI, 0);
    outb8(ATA_STAT_CMD, ATA_CMD_IDENTIFY);
    ata_delay400();

    st = inb8(ATA_STAT_CMD);
    if (st == 0x00u) { drive_type = ATA_DRIVE_NONE; return; }

    /* Tunggu BSY=0 */
    if (ata_wait_not_busy() != 0) { drive_type = ATA_DRIVE_NONE; return; }

    /* Cek signature ATAPI di LBA_MID / LBA_HI */
    uint8_t mid = inb8(ATA_LBA_MID);
    uint8_t hi  = inb8(ATA_LBA_HI);
    if (mid == ATAPI_SIG_MID && hi == ATAPI_SIG_HI) {
        drive_type = ATA_DRIVE_ATAPI;
        disk_present = 0;  /* ATAPI bukan disk biasa, tidak dipakai FS */
        return;
    }

    /* Cek ERR — jika set bisa jadi drive tidak ada */
    st = inb8(ATA_STAT_CMD);
    if (st & ATA_ERR) { drive_type = ATA_DRIVE_NONE; return; }

    /* Harus DRQ=1 untuk baca IDENTIFY data */
    if (!(st & ATA_DRQ)) { drive_type = ATA_DRIVE_NONE; return; }

    /* Baca dan buang 256 word IDENTIFY data */
    int i;
    for (i = 0; i < 256; i++) inw16(ATA_DATA);

    drive_type   = ATA_DRIVE_HDD;
    disk_present = 1;
}

/* ------------------------------------------------------------------ */
/* Public: init                                                         */
/* ------------------------------------------------------------------ */
void ata_init(void) {
    bmide_detect();
    detect_drive_type();
}

int ata_disk_present(void)         { return disk_present; }
int ata_dma_available(void)        { return dma_ready; }
ata_drive_type_t ata_get_drive_type(void) { return drive_type; }

/* ------------------------------------------------------------------ */
/* DMA read/write (internal, only called when dma_ready)               */
/* ------------------------------------------------------------------ */
static int dma_transfer(uint32_t lba, uint8_t *buf, int is_write) {
    /* Buffer phys addr must fit in 32 bits */
    if ((uint64_t)(uintptr_t)buf > 0xFFFFFFFFULL) return -1;

    bmide_prdt.phys_addr  = (uint32_t)(uintptr_t)buf;
    bmide_prdt.byte_count = 512;
    bmide_prdt.flags      = 0x8000u; /* End-Of-Table */

    uint16_t bm = bmide_base;

    /* Clear status bits (error + interrupt) */
    outb8((uint16_t)(bm + BM_STAT), BM_STAT_FAIL | BM_STAT_INTR);

    /* Tulis alamat fisik PRDT ke BMBASE+4 (32-bit register) */
    outl32((uint16_t)(bm + BM_PRDT), (uint32_t)(uintptr_t)&bmide_prdt);

    /* Set direction sebelum Start — 0=disk→mem (read), BM_CMD_WDIR=mem→disk (write) */
    outb8((uint16_t)(bm + BM_CMD), is_write ? BM_CMD_WDIR : 0x00u);

    /* Program LBA + sector count */
    if (ata_wait_not_busy() != 0) return -1;
    outb8(ATA_DRIVE,    (uint8_t)(FS_DRIVE_SEL | ((lba >> 24) & 0x0Fu)));
    outb8(ATA_FEATURES, 0x01u);  /* DMA mode */
    outb8(ATA_SECT_CNT, 1);
    outb8(ATA_LBA_LO,   (uint8_t)(lba        & 0xFFu));
    outb8(ATA_LBA_MID,  (uint8_t)((lba >>  8) & 0xFFu));
    outb8(ATA_LBA_HI,   (uint8_t)((lba >> 16) & 0xFFu));
    outb8(ATA_STAT_CMD, is_write ? ATA_CMD_WRIT_DMA : ATA_CMD_READ_DMA);
    ata_delay400();

    /* Start DMA */
    outb8((uint16_t)(bm + BM_CMD),
          (uint8_t)((is_write ? BM_CMD_WDIR : 0x00u) | BM_CMD_START));

    /* Poll: tunggu INTR=1 atau ACTIVE=0 */
    int n = 0x400000;
    uint8_t bm_st;
    do {
        bm_st = inb8((uint16_t)(bm + BM_STAT));
        if ((bm_st & BM_STAT_INTR) || !(bm_st & BM_STAT_ACT)) break;
    } while (--n);

    /* Stop DMA, reset status */
    outb8((uint16_t)(bm + BM_CMD), 0x00u);
    outb8((uint16_t)(bm + BM_STAT), BM_STAT_FAIL | BM_STAT_INTR);

    if (n == 0)               return -1;  /* timeout */
    if (bm_st & BM_STAT_FAIL) return -1;  /* DMA error */

    /* Jika write, juga flush cache */
    if (is_write) {
        outb8(ATA_STAT_CMD, ATA_CMD_FLUSH);
        ata_wait_not_busy();
    }

    /* Cek ATA error bit */
    if (inb8(ATA_STAT_CMD) & ATA_ERR) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public: read / write sector                                          */
/* ------------------------------------------------------------------ */
int ata_read_sector(uint32_t lba, uint8_t *buf) {
    if (!buf || !disk_present)  return -1;
    if (lba > ATA_LBA28_MAX)    return -1;  /* sector boundary check */

    /* Coba DMA dulu, fallback ke PIO jika gagal */
    if (dma_ready && dma_transfer(lba, buf, 0) == 0) return 0;

    /* ---- PIO fallback ---- */
    if (ata_wait_not_busy() != 0) return -1;
    outb8(ATA_DRIVE,    (uint8_t)(FS_DRIVE_SEL | ((lba >> 24) & 0x0Fu)));
    outb8(ATA_FEATURES, 0x00);
    outb8(ATA_SECT_CNT, 1);
    outb8(ATA_LBA_LO,   (uint8_t)(lba        & 0xFFu));
    outb8(ATA_LBA_MID,  (uint8_t)((lba >>  8) & 0xFFu));
    outb8(ATA_LBA_HI,   (uint8_t)((lba >> 16) & 0xFFu));
    outb8(ATA_STAT_CMD, ATA_CMD_READ);
    ata_delay400();
    if (ata_wait_drq() != 0) return -1;
    uint16_t *w = (uint16_t *)buf;
    int i;
    for (i = 0; i < 256; i++) w[i] = inw16(ATA_DATA);
    return 0;
}

int ata_write_sector(uint32_t lba, const uint8_t *buf) {
    if (!buf || !disk_present)  return -1;
    if (lba > ATA_LBA28_MAX)    return -1;  /* sector boundary check */

    /* Coba DMA dulu */
    if (dma_ready && dma_transfer(lba, (uint8_t *)buf, 1) == 0) return 0;

    /* ---- PIO fallback ---- */
    if (ata_wait_not_busy() != 0) return -1;
    outb8(ATA_DRIVE,    (uint8_t)(FS_DRIVE_SEL | ((lba >> 24) & 0x0Fu)));
    outb8(ATA_FEATURES, 0x00);
    outb8(ATA_SECT_CNT, 1);
    outb8(ATA_LBA_LO,   (uint8_t)(lba        & 0xFFu));
    outb8(ATA_LBA_MID,  (uint8_t)((lba >>  8) & 0xFFu));
    outb8(ATA_LBA_HI,   (uint8_t)((lba >> 16) & 0xFFu));
    outb8(ATA_STAT_CMD, ATA_CMD_WRITE);
    ata_delay400();
    if (ata_wait_drq() != 0) return -1;
    const uint16_t *w = (const uint16_t *)buf;
    int i;
    for (i = 0; i < 256; i++) outw16(ATA_DATA, w[i]);
    outb8(ATA_STAT_CMD, ATA_CMD_FLUSH);
    ata_wait_not_busy();
    return 0;
}
