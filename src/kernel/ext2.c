/* ext2.c — EXT2 Filesystem read-only driver
 * Fondasi AR — Oria OS
 *
 * Membaca dari ATA secondary IDE drive (disk.img, index 1).
 * Superblock EXT2: offset 1024 byte, magic 0xEF53.
 * Block group descriptor table: blok setelah superblock.
 * Hanya READ-ONLY: mount, ls, cat.
 */
#include "ext2.h"
#include "ata.h"
#include <stdint.h>

extern void print(const char *s);
extern void itoa(uint32_t n, char *buf);
extern void set_color(uint32_t fg, uint32_t bg);

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static void xmemset(void *p, uint8_t v, uint32_t n) {
    uint8_t *b=(uint8_t*)p; while(n--)*b++=v;
}
static void xmemcpy(void *d, const void *s, uint32_t n) {
    uint8_t *dd=(uint8_t*)d; const uint8_t *ss=(const uint8_t*)s;
    while(n--) *dd++=*ss++;
}
static int xstrlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static int xstrcmp(const char *a, const char *b) {
    while(*a&&*b&&*a==*b){a++;b++;}
    return (int)(uint8_t)*a-(int)(uint8_t)*b;
}
static int xstrncmp(const char *a, const char *b, int n) {
    while(n--&&*a&&*b&&*a==*b){a++;b++;}
    if(n<0) return 0;
    return (int)(uint8_t)*a-(int)(uint8_t)*b;
}
static void print_uint(uint32_t n) {
    char buf[12]; itoa(n, buf); print(buf);
}

/* ------------------------------------------------------------------ */
/* EXT2 structs (little-endian, packed)                               */
/* ------------------------------------------------------------------ */
#define EXT2_MAGIC      0xEF53
#define EXT2_ROOT_INO   2
#define EXT2_MAX_BLOCK  4096    /* max block size we support */
#define EXT2_SECT       512

/* Superblock offsets (byte) */
#define SB_INODES_COUNT   0
#define SB_BLOCKS_COUNT   4
#define SB_BLOCKS_PER_GRP 32
#define SB_INODES_PER_GRP 40
#define SB_LOG_BLOCK_SIZE 24   /* block_size = 1024 << val */
#define SB_MAGIC          56
#define SB_INODE_SIZE     88
#define SB_REV_LEVEL      76   /* 0=old, 1=dynamic */

/* Block group descriptor offsets (byte, each entry 32 bytes) */
#define BGD_BLOCK_BITMAP  0
#define BGD_INODE_BITMAP  4
#define BGD_INODE_TABLE   8
#define BGD_SIZE          32

/* Inode offsets (byte) */
#define INO_MODE     0
#define INO_SIZE     4
#define INO_LINKS    26   /* i_links_count (2 bytes) */
#define INO_BLOCKS   28   /* i_blocks (512-byte units, 4 bytes) */
#define INO_BLOCK    40   /* i_block[0..14] (15*4 = 60 bytes) */

/* Directory entry offsets */
#define DE_INODE     0   /* 4 bytes */
#define DE_REC_LEN   4   /* 2 bytes */
#define DE_NAME_LEN  6   /* 1 byte */
#define DE_FILE_TYPE 7   /* 1 byte */
#define DE_NAME      8

/* File type bits */
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFREG  0x8000

/* ------------------------------------------------------------------ */
/* Filesystem state                                                    */
/* ------------------------------------------------------------------ */
static int      g_mounted        = 0;
static uint32_t g_block_size     = 1024;
static uint32_t g_inodes_per_grp = 0;
static uint32_t g_inode_size     = 128;
static uint32_t g_blocks_per_grp = 0;
static uint32_t g_bgdt_block     = 0; /* first block of BG descriptor table */

/* Static I/O buffers — we use multiple to avoid clobber during nesting */
static uint8_t g_sb_buf[1024];         /* superblock raw (1024 bytes at LBA 2) */
static uint8_t g_block_buf[EXT2_MAX_BLOCK];   /* scratch block buffer */
static uint8_t g_inode_buf[256];       /* current inode (max 256 bytes) */
static uint8_t g_indir_buf[EXT2_MAX_BLOCK];   /* indirect block buffer */
static uint8_t g_dir_buf[EXT2_MAX_BLOCK];     /* directory block buffer */

/* ------------------------------------------------------------------ */
/* Low-level: read N sectors from LBA into buf                        */
/* ------------------------------------------------------------------ */
static int read_sectors(uint32_t lba, uint32_t count, uint8_t *buf) {
    uint32_t i;
    for (i=0; i<count; i++) {
        if (ata_read_sector(lba+i, buf + i*512) != 0) return -1;
    }
    return 0;
}

/* Read one EXT2 block into buf (buf must be >= g_block_size bytes) */
static int ext2_read_block(uint32_t blk, uint8_t *buf) {
    if (blk == 0) { xmemset(buf, 0, g_block_size); return 0; }
    uint32_t sectors_per_block = g_block_size / EXT2_SECT;
    uint32_t lba = blk * sectors_per_block;
    return read_sectors(lba, sectors_per_block, buf);
}

/* Read 32-bit LE from byte array */
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint16_t u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1]<<8);
}

/* ------------------------------------------------------------------ */
/* ext2_mount                                                          */
/* ------------------------------------------------------------------ */
int ext2_mount(void) {
    g_mounted = 0;
    if (!ata_disk_present()) {
        print("ext2: ATA disk tidak ditemukan\n");
        return 0;
    }
    /* Superblock: byte offset 1024 = LBA 2 (sector 2, offset 0) */
    if (read_sectors(2, 2, g_sb_buf) != 0) {
        print("ext2: gagal baca superblock\n");
        return 0;
    }
    uint16_t magic = u16(g_sb_buf + SB_MAGIC);
    if (magic != EXT2_MAGIC) {
        print("ext2: magic tidak cocok (bukan EXT2 atau disk kosong)\n");
        return 0;
    }
    uint32_t log_bs      = u32(g_sb_buf + SB_LOG_BLOCK_SIZE);
    g_block_size         = 1024u << log_bs;
    if (g_block_size > EXT2_MAX_BLOCK) {
        print("ext2: block size terlalu besar\n");
        return 0;
    }
    g_blocks_per_grp  = u32(g_sb_buf + SB_BLOCKS_PER_GRP);
    g_inodes_per_grp  = u32(g_sb_buf + SB_INODES_PER_GRP);
    uint32_t rev      = u32(g_sb_buf + SB_REV_LEVEL);
    if (rev >= 1) {
        uint16_t isz = u16(g_sb_buf + SB_INODE_SIZE);
        g_inode_size = isz ? isz : 128;
    } else {
        g_inode_size = 128;
    }
    if (g_inode_size > 256) g_inode_size = 256;

    /* BGDT starts at block 2 for 1KB blocks, block 1 for larger blocks */
    g_bgdt_block = (g_block_size == 1024) ? 2 : 1;

    g_mounted = 1;
    set_color(0x0055FF55, 0);
    print("ext2: mounted OK  blksz="); print_uint(g_block_size);
    print("  inosz="); print_uint(g_inode_size);
    print("  ino/grp="); print_uint(g_inodes_per_grp);
    print("\n");
    set_color(0x00FFFFFF, 0);
    return 1;
}

int ext2_is_mounted(void) { return g_mounted; }

/* ------------------------------------------------------------------ */
/* Read block group descriptor for group 'grp'                        */
/* Returns 0 OK, -1 error. bg_buf must be BGD_SIZE bytes.            */
/* ------------------------------------------------------------------ */
static int read_bgd(uint32_t grp, uint8_t bg_buf[BGD_SIZE]) {
    /* Each BGD is 32 bytes. They're packed into blocks starting at g_bgdt_block. */
    uint32_t byte_off = grp * BGD_SIZE;
    uint32_t blk_off  = byte_off / g_block_size;
    uint32_t blk_in   = byte_off % g_block_size;
    if (ext2_read_block(g_bgdt_block + blk_off, g_block_buf) != 0) return -1;
    xmemcpy(bg_buf, g_block_buf + blk_in, BGD_SIZE);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Read inode into g_inode_buf                                        */
/* ------------------------------------------------------------------ */
static int read_inode(uint32_t ino) {
    if (ino == 0 || g_inodes_per_grp == 0) return -1;
    uint32_t grp      = (ino - 1) / g_inodes_per_grp;
    uint32_t idx      = (ino - 1) % g_inodes_per_grp;
    uint8_t  bgd[BGD_SIZE];
    if (read_bgd(grp, bgd) != 0) return -1;
    uint32_t itable   = u32(bgd + BGD_INODE_TABLE);
    /* byte offset of inode in inode table */
    uint32_t byte_off = idx * g_inode_size;
    uint32_t blk      = itable + byte_off / g_block_size;
    uint32_t off_in   = byte_off % g_block_size;
    if (ext2_read_block(blk, g_block_buf) != 0) return -1;
    xmemcpy(g_inode_buf, g_block_buf + off_in, g_inode_size < 256 ? g_inode_size : 256);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Get data block number for logical block idx within an inode        */
/*
 * Catatan: fungsi ini memakai buffer scratch statis (g_indir_buf, indir2)
 * yang dipakai ulang antar pemanggilan. Aman untuk pemanggilan berurutan
 * (driver ext2 ini single-threaded, tidak reentrant), tapi JANGAN dipanggil
 * secara nested/rekursif (mis. dari dalam ext2_read_block callback lain)
 * karena akan saling menimpa isi buffer.
 */
/* ------------------------------------------------------------------ */
static uint32_t inode_get_block(uint32_t idx) {
    uint32_t ptrs_per_block = g_block_size / 4;
    uint32_t direct = 12;
    uint32_t blkno;
    if (idx < direct) {
        blkno = u32(g_inode_buf + INO_BLOCK + idx * 4);
        return blkno;
    }
    idx -= direct;
    /* Single indirect */
    if (idx < ptrs_per_block) {
        uint32_t indir = u32(g_inode_buf + INO_BLOCK + 12*4);
        if (indir == 0) return 0;
        if (ext2_read_block(indir, g_indir_buf) != 0) return 0;
        return u32(g_indir_buf + idx * 4);
    }
    idx -= ptrs_per_block;
    /* Double indirect */
    if (idx < ptrs_per_block * ptrs_per_block) {
        uint32_t dindir = u32(g_inode_buf + INO_BLOCK + 13*4);
        if (dindir == 0) return 0;
        if (ext2_read_block(dindir, g_indir_buf) != 0) return 0;
        uint32_t l1 = u32(g_indir_buf + (idx / ptrs_per_block) * 4);
        if (l1 == 0) return 0;
        static uint8_t indir2[EXT2_MAX_BLOCK];
        if (ext2_read_block(l1, indir2) != 0) return 0;
        return u32(indir2 + (idx % ptrs_per_block) * 4);
    }
    return 0; /* triple indirect not supported */
}

/* ------------------------------------------------------------------ */
/* Read up to 'len' bytes from inode starting at 'offset' into buf   */
/* ------------------------------------------------------------------ */
static int inode_read(uint32_t file_size, uint32_t offset,
                      uint8_t *buf, uint32_t len) {
    uint32_t end = offset + len;
    if (end > file_size) end = file_size;
    if (offset >= end) return 0;
    uint32_t total = 0;
    while (offset < end) {
        uint32_t blk_idx  = offset / g_block_size;
        uint32_t blk_off  = offset % g_block_size;
        uint32_t blk_no   = inode_get_block(blk_idx);
        uint32_t chunk    = g_block_size - blk_off;
        if (chunk > end - offset) chunk = end - offset;
        if (blk_no == 0) {
            xmemset(buf + total, 0, chunk);
        } else {
            if (ext2_read_block(blk_no, g_dir_buf) != 0) return (int)total;
            xmemcpy(buf + total, g_dir_buf + blk_off, chunk);
        }
        offset += chunk;
        total  += chunk;
    }
    return (int)total;
}

/* ------------------------------------------------------------------ */
/* Find a directory entry 'name' in directory inode, return inode no */
/* Returns 0 if not found.                                            */
/* ------------------------------------------------------------------ */
static uint32_t dir_find(uint32_t dir_ino, const char *name) {
    if (read_inode(dir_ino) != 0) return 0;
    uint16_t mode = u16(g_inode_buf + INO_MODE);
    if ((mode & EXT2_S_IFDIR) == 0) return 0;
    uint32_t dir_size = u32(g_inode_buf + INO_SIZE);
    uint32_t offset   = 0;
    int      namelen  = xstrlen(name);
    static uint8_t inode_save[256];
    xmemcpy(inode_save, g_inode_buf, 256);
    while (offset < dir_size) {
        uint32_t blk_idx = offset / g_block_size;
        uint32_t blk_off = offset % g_block_size;
        /* Restore inode buf (may be clobbered by block reads) */
        xmemcpy(g_inode_buf, inode_save, 256);
        uint32_t blk_no = inode_get_block(blk_idx);
        xmemcpy(g_inode_buf, inode_save, 256);
        if (blk_no == 0) break;
        if (ext2_read_block(blk_no, g_dir_buf) != 0) break;
        /* Walk directory entries in this block */
        uint32_t block_limit = g_block_size;
        if (offset + block_limit > dir_size)
            block_limit = dir_size - (blk_idx * g_block_size);
        uint32_t p = blk_off; /* within this block we start from blk_off initially */
        /* Actually, dir entries always start at block boundary; reset p to 0 if blk_off==0 */
        if (blk_off != 0) { offset += (g_block_size - blk_off); continue; }
        p = 0;
        while (p < block_limit) {
            const uint8_t *de = g_dir_buf + p;
            uint32_t ino     = u32(de + DE_INODE);
            uint16_t rec_len = u16(de + DE_REC_LEN);
            if (rec_len == 0) break;
            uint8_t  nlen    = de[DE_NAME_LEN];
            if (ino != 0 && nlen == (uint8_t)namelen &&
                xstrncmp((const char*)(de + DE_NAME), name, namelen) == 0) {
                return ino;
            }
            p += rec_len;
        }
        offset += g_block_size;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Resolve absolute path to inode number                              */
/* Path format: "/" or "/dir/file"                                    */
/* ------------------------------------------------------------------ */
static uint32_t resolve_path(const char *path) {
    uint32_t cur = EXT2_ROOT_INO;
    if (!path || path[0] == '\0' || (path[0]=='/'&&path[1]=='\0')) return cur;
    const char *p = path;
    if (*p == '/') p++;
    while (*p) {
        const char *slash = p;
        while (*slash && *slash != '/') slash++;
        /* Component is p[0..slash-p-1] */
        int complen = (int)(slash - p);
        if (complen == 0) { p = slash + 1; continue; }
        /* Make a null-terminated component */
        char comp[256];
        if (complen >= 255) return 0;
        xmemcpy(comp, p, (uint32_t)complen);
        comp[complen] = 0;
        cur = dir_find(cur, comp);
        if (cur == 0) return 0;
        p = *slash ? slash + 1 : slash;
    }
    return cur;
}

/* ------------------------------------------------------------------ */
/* ext2_ls — list directory                                           */
/* ------------------------------------------------------------------ */
void ext2_ls(const char *path) {
    if (!g_mounted) { print("ext2: tidak di-mount. Gunakan: mount ext2\n"); return; }
    uint32_t dir_ino = resolve_path(path);
    if (dir_ino == 0) {
        print("ext2: path tidak ditemukan: "); print(path); print("\n");
        return;
    }
    if (read_inode(dir_ino) != 0) { print("ext2: gagal baca inode\n"); return; }
    uint16_t mode = u16(g_inode_buf + INO_MODE);
    if ((mode & EXT2_S_IFDIR) == 0) {
        print("ext2: bukan direktori: "); print(path); print("\n");
        return;
    }
    uint32_t dir_size = u32(g_inode_buf + INO_SIZE);
    uint32_t offset   = 0;
    static uint8_t inode_save[256];
    xmemcpy(inode_save, g_inode_buf, 256);

    set_color(0x0055FFFF, 0);
    print("  inode   size     name\n");
    print("  ------  -------  ----\n");
    set_color(0x00FFFFFF, 0);

    while (offset < dir_size) {
        uint32_t blk_idx = offset / g_block_size;
        xmemcpy(g_inode_buf, inode_save, 256);
        uint32_t blk_no = inode_get_block(blk_idx);
        xmemcpy(g_inode_buf, inode_save, 256);
        if (blk_no == 0) { offset += g_block_size; continue; }
        if (ext2_read_block(blk_no, g_dir_buf) != 0) break;
        uint32_t block_limit = g_block_size;
        if ((blk_idx+1)*g_block_size > dir_size)
            block_limit = dir_size - blk_idx*g_block_size;
        uint32_t p = 0;
        while (p < block_limit) {
            const uint8_t *de = g_dir_buf + p;
            uint32_t ino     = u32(de + DE_INODE);
            uint16_t rec_len = u16(de + DE_REC_LEN);
            if (rec_len == 0) break;
            uint8_t nlen = de[DE_NAME_LEN];
            if (ino != 0 && nlen > 0) {
                char name[256]; char nm[32];
                uint8_t ft = de[DE_FILE_TYPE];
                int take = nlen < 255 ? nlen : 255;
                xmemcpy(name, de + DE_NAME, (uint32_t)take); name[take]=0;
                /* Skip . and .. */
                if (!(name[0]=='.'&&(name[1]=='\0'||(name[1]=='.'&&name[2]=='\0')))) {
                    /* Print inode */
                    itoa(ino, nm); print("  ");
                    { int ll=xstrlen(nm); int pad=6-ll; while(pad-->0)print(" "); print(nm); }
                    print("  ");
                    /* Get file size */
                    if (read_inode(ino) == 0) {
                        uint32_t fsz = u32(g_inode_buf + INO_SIZE);
                        itoa(fsz, nm);
                        { int ll=xstrlen(nm); int pad=7-ll; while(pad-->0)print(" "); print(nm); }
                    } else { print("       "); }
                    print("  ");
                    if (ft==2) { set_color(0x0055FFFF,0); print(name); set_color(0x00FFFFFF,0); }
                    else print(name);
                    print("\n");
                    /* Restore inode after sub-read */
                    xmemcpy(g_inode_buf, inode_save, 256);
                }
            }
            p += rec_len;
        }
        offset += g_block_size;
    }
}

/* ------------------------------------------------------------------ */
/* ext2_cat — print file content                                      */
/* ------------------------------------------------------------------ */
#define CAT_CHUNK 512
void ext2_cat(const char *path) {
    if (!g_mounted) { print("ext2: tidak di-mount. Gunakan: mount ext2\n"); return; }
    uint32_t ino = resolve_path(path);
    if (ino == 0) {
        print("ext2: tidak ditemukan: "); print(path); print("\n"); return;
    }
    if (read_inode(ino) != 0) { print("ext2: gagal baca inode\n"); return; }
    uint16_t mode = u16(g_inode_buf + INO_MODE);
    if ((mode & EXT2_S_IFREG) == 0) {
        print("ext2: bukan file biasa: "); print(path); print("\n"); return;
    }
    uint32_t fsz = u32(g_inode_buf + INO_SIZE);
    if (fsz == 0) { print("(file kosong)\n"); return; }
    /* Save inode */
    static uint8_t inode_save[256];
    xmemcpy(inode_save, g_inode_buf, 256);
    uint32_t offset = 0;
    static uint8_t cat_buf[CAT_CHUNK+1];
    while (offset < fsz) {
        xmemcpy(g_inode_buf, inode_save, 256);
        uint32_t chunk = fsz - offset;
        if (chunk > CAT_CHUNK) chunk = CAT_CHUNK;
        int got = inode_read(fsz, offset, cat_buf, chunk);
        if (got <= 0) break;
        cat_buf[got] = 0;
        print((const char*)cat_buf);
        offset += (uint32_t)got;
    }
    if (fsz > 0 && cat_buf[0] != '\0') {
        /* Ensure newline at end */
        char last = cat_buf[(offset > (uint32_t)CAT_CHUNK ? CAT_CHUNK : offset % CAT_CHUNK) - 1];
        if (last != '\n') print("\n");
    }
}
