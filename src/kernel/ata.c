/*
 * ata.c — ATA PIO driver untuk Primary Slave (disk kedua)
 *
 * Disk kedua digunakan sebagai persistent storage untuk filesystem.
 * Menggunakan mode polling (PIO) tanpa DMA/IRQ.
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

#define ATA_CMD_READ   0x20u  /* READ SECTORS */
#define ATA_CMD_WRITE  0x30u  /* WRITE SECTORS */
#define ATA_CMD_FLUSH  0xE7u  /* FLUSH CACHE */

#define ATA_BSY 0x80u  /* busy */
#define ATA_DRQ 0x08u  /* data request ready */
#define ATA_ERR 0x01u  /* error bit */

/* Kita pakai Primary Slave (drive select = 0xF0 | lba_high_nibble) */
#define FS_DRIVE_SEL 0xF0u

static int disk_present = 0;

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

/* Delay ~400ns dengan membaca alt-status 4 kali */
static void ata_delay400() {
    inb8(ATA_ALT_STAT);
    inb8(ATA_ALT_STAT);
    inb8(ATA_ALT_STAT);
    inb8(ATA_ALT_STAT);
}

/* Tunggu BSY=0.  Return 0 OK, -1 timeout/error. */
static int ata_wait_not_busy() {
    int n = 0x100000;
    while (n--) {
        uint8_t s = inb8(ATA_STAT_CMD);
        if ((s & ATA_BSY) == 0) return 0;
    }
    return -1;
}

/* Tunggu BSY=0 DAN DRQ=1.  Return 0 OK, -1 error/timeout. */
static int ata_wait_drq() {
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
void ata_init() {
    /* Pilih Primary Slave dalam mode CHS sementara untuk cek identitas */
    outb8(ATA_DRIVE, 0xB0);
    ata_delay400();

    /* 0xFF pada STATUS = tidak ada drive */
    uint8_t status = inb8(ATA_STAT_CMD);
    disk_present = (status != 0xFF && status != 0x00);

    if (disk_present) {
        /* Pastikan drive idle sebelum dipakai */
        ata_wait_not_busy();
    }
}

int ata_disk_present() {
    return disk_present;
}

/* ------------------------------------------------------------------ */
int ata_read_sector(uint32_t lba, uint8_t *buf) {
    if (!buf || !disk_present) return -1;

    if (ata_wait_not_busy() != 0) return -1;

    /* LBA 28-bit untuk Primary Slave */
    outb8(ATA_DRIVE,    (uint8_t)(FS_DRIVE_SEL | ((lba >> 24) & 0x0Fu)));
    outb8(ATA_FEATURES, 0x00);
    outb8(ATA_SECT_CNT, 1);
    outb8(ATA_LBA_LO,   (uint8_t)(lba & 0xFFu));
    outb8(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFFu));
    outb8(ATA_LBA_HI,   (uint8_t)((lba >> 16) & 0xFFu));
    outb8(ATA_STAT_CMD, ATA_CMD_READ);

    ata_delay400();

    if (ata_wait_drq() != 0) return -1;

    /* Baca 256 word (512 byte) dari port data */
    uint16_t *w = (uint16_t*)buf;
    int i;
    for (i = 0; i < 256; i++)
        w[i] = inw16(ATA_DATA);

    return 0;
}

int ata_write_sector(uint32_t lba, const uint8_t *buf) {
    if (!buf || !disk_present) return -1;

    if (ata_wait_not_busy() != 0) return -1;

    outb8(ATA_DRIVE,    (uint8_t)(FS_DRIVE_SEL | ((lba >> 24) & 0x0Fu)));
    outb8(ATA_FEATURES, 0x00);
    outb8(ATA_SECT_CNT, 1);
    outb8(ATA_LBA_LO,   (uint8_t)(lba & 0xFFu));
    outb8(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFFu));
    outb8(ATA_LBA_HI,   (uint8_t)((lba >> 16) & 0xFFu));
    outb8(ATA_STAT_CMD, ATA_CMD_WRITE);

    ata_delay400();

    if (ata_wait_drq() != 0) return -1;

    /* Tulis 256 word */
    const uint16_t *w = (const uint16_t*)buf;
    int i;
    for (i = 0; i < 256; i++)
        outw16(ATA_DATA, w[i]);

    /* Flush cache */
    outb8(ATA_STAT_CMD, ATA_CMD_FLUSH);
    ata_wait_not_busy();

    return 0;
}
