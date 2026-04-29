/* acpi.c — Parser ACPI minimal untuk menemukan MADT (APIC table)
 *
 * Scope implementasi:
 *   - Cari RSDP di EBDA (0x40E) dan BIOS area 0xE0000..0xFFFFF
 *   - Parse RSDT (ACPI 1.0, 32-bit addresses)
 *   - Temukan MADT (signature "APIC")
 *   - Enumerasi Local APIC entries (type 0)
 *
 * Catatan:
 *   - Ini parser ringkas untuk QEMU/Bochs; belum parse XSDT (ACPI 2+).
 *   - CPU dianggap valid jika flags bit0 (enabled) = 1.
 */
#include "acpi.h"
#include "apic.h"
#include <stdint.h>

int cpu_count = 0;
CpuInfo cpus[SMP_MAX_CPUS];

/* ------------------------------------------------------------------ */
/* Struktur ACPI basic */

typedef struct {
    char     sig[8];
    uint8_t  cksum;
    char     oemid[6];
    uint8_t  rev;
    uint32_t rsdt_addr;
} __attribute__((packed)) Rsdp1;

typedef struct {
    char     sig[4];
    uint32_t len;
    uint8_t  rev;
    uint8_t  cksum;
    char     oemid[6];
    char     oemtbl[8];
    uint32_t oemrev;
    uint32_t creator;
    uint32_t creator_rev;
} __attribute__((packed)) AcpiSdt;

/* MADT header setelah SDT header */
typedef struct {
    AcpiSdt  h;
    uint32_t lapic_addr;
    uint32_t flags;
} __attribute__((packed)) Madt;

/* MADT entry type 0: Processor Local APIC */
typedef struct {
    uint8_t  type;
    uint8_t  len;
    uint8_t  acpi_cpu_id;
    uint8_t  apic_id;
    uint32_t flags;
} __attribute__((packed)) MadtLapic;

/* ------------------------------------------------------------------ */

static int checksum_ok(const void *p, uint32_t len)
{
    const uint8_t *b = (const uint8_t *)p;
    uint8_t sum = 0;
    uint32_t i;
    for (i = 0; i < len; i++) sum = (uint8_t)(sum + b[i]);
    return sum == 0;
}

static Rsdp1 *find_rsdp(void)
{
    uint16_t *ebda_seg_ptr = (uint16_t *)(uintptr_t)0x40E;
    uint32_t ebda = ((uint32_t)(*ebda_seg_ptr)) << 4;

    /* 1) Cek EBDA 1KB pertama, boundary 16-byte */
    uint32_t a;
    for (a = ebda; a < ebda + 1024; a += 16) {
        Rsdp1 *r = (Rsdp1 *)(uintptr_t)a;
        if (r->sig[0]=='R' && r->sig[1]=='S' && r->sig[2]=='D' && r->sig[3]==' ' &&
            r->sig[4]=='P' && r->sig[5]=='T' && r->sig[6]=='R' && r->sig[7]==' ') {
            if (checksum_ok(r, 20)) return r;
        }
    }

    /* 2) Scan BIOS area 0xE0000..0xFFFFF, boundary 16-byte */
    for (a = 0xE0000; a < 0x100000; a += 16) {
        Rsdp1 *r = (Rsdp1 *)(uintptr_t)a;
        if (r->sig[0]=='R' && r->sig[1]=='S' && r->sig[2]=='D' && r->sig[3]==' ' &&
            r->sig[4]=='P' && r->sig[5]=='T' && r->sig[6]=='R' && r->sig[7]==' ') {
            if (checksum_ok(r, 20)) return r;
        }
    }
    return 0;
}

static Madt *find_madt_from_rsdt(uint32_t rsdt_addr)
{
    AcpiSdt *rsdt = (AcpiSdt *)(uintptr_t)rsdt_addr;
    if (!rsdt) return 0;
    if (!(rsdt->sig[0]=='R' && rsdt->sig[1]=='S' && rsdt->sig[2]=='D' && rsdt->sig[3]=='T'))
        return 0;
    if (!checksum_ok(rsdt, rsdt->len))
        return 0;

    uint32_t entries = (rsdt->len - sizeof(AcpiSdt)) / 4;
    uint32_t *ptrs = (uint32_t *)((uint8_t *)rsdt + sizeof(AcpiSdt));
    uint32_t i;
    for (i = 0; i < entries; i++) {
        AcpiSdt *s = (AcpiSdt *)(uintptr_t)ptrs[i];
        if (!s) continue;
        if (s->sig[0]=='A' && s->sig[1]=='P' && s->sig[2]=='I' && s->sig[3]=='C') {
            if (checksum_ok(s, s->len))
                return (Madt *)s;
        }
    }
    return 0;
}

void acpi_init(void)
{
    int i;
    for (i = 0; i < SMP_MAX_CPUS; i++) {
        cpus[i].apic_id = 0;
        cpus[i].acpi_id = 0;
        cpus[i].enabled = 0;
    }
    cpu_count = 0;

    /* Isi BSP terlebih dahulu dari LAPIC ID saat ini */
    cpus[0].apic_id = apic_get_id();
    cpus[0].acpi_id = 0;
    cpus[0].enabled = 1;
    cpu_count = 1;

    Rsdp1 *rsdp = find_rsdp();
    if (!rsdp) return;

    Madt *madt = find_madt_from_rsdt(rsdp->rsdt_addr);
    if (!madt) return;

    uint8_t *p   = (uint8_t *)madt + sizeof(Madt);
    uint8_t *end = (uint8_t *)madt + madt->h.len;

    while (p + 2 <= end) {
        uint8_t type = p[0];
        uint8_t len  = p[1];
        if (len < 2 || p + len > end) break;

        if (type == 0 && len >= sizeof(MadtLapic)) {
            MadtLapic *e = (MadtLapic *)p;
            if (e->flags & 1) {
                int exists = 0;
                int j;
                for (j = 0; j < cpu_count; j++) {
                    if (cpus[j].apic_id == e->apic_id) {
                        exists = 1;
                        break;
                    }
                }
                if (!exists && cpu_count < SMP_MAX_CPUS) {
                    cpus[cpu_count].apic_id = e->apic_id;
                    cpus[cpu_count].acpi_id = e->acpi_cpu_id;
                    cpus[cpu_count].enabled = 1;
                    cpu_count++;
                }
            }
        }
        p += len;
    }
}

/* ================================================================
 * Fondasi AK — ACPI Power Management
 * ================================================================ */

static inline void outw_ak(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" :: "a"(val), "Nd"(port));
}
static inline void outb_ak(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

/* Cari port PM1a_CNT dari ACPI FADT (table "FACP") di RSDT.
 * Jika tidak ditemukan, kembalikan 0x604 (QEMU default). */
static uint32_t find_pm1a_port(void) {
    Rsdp1 *rsdp = find_rsdp();
    if (!rsdp) return 0x604;
    AcpiSdt *rsdt = (AcpiSdt *)(uintptr_t)rsdp->rsdt_addr;
    if (!rsdt) return 0x604;
    if (!(rsdt->sig[0]=='R' && rsdt->sig[1]=='S' && rsdt->sig[2]=='D' && rsdt->sig[3]=='T'))
        return 0x604;
    if (!checksum_ok(rsdt, rsdt->len)) return 0x604;

    uint32_t entries = (rsdt->len - sizeof(AcpiSdt)) / 4;
    uint32_t *ptrs   = (uint32_t *)((uint8_t *)rsdt + sizeof(AcpiSdt));
    uint32_t i;
    for (i = 0; i < entries; i++) {
        AcpiSdt *s = (AcpiSdt *)(uintptr_t)ptrs[i];
        if (!s) continue;
        /* FACP = Fixed ACPI Description Table */
        if (s->sig[0]=='F' && s->sig[1]=='A' && s->sig[2]=='C' && s->sig[3]=='P') {
            if (!checksum_ok(s, s->len)) continue;
            /* PM1a_CNT_BLK ada di offset 64 dari awal SDT header */
            if (s->len >= 68) {
                uint32_t pm1a = *(uint32_t *)((uint8_t *)s + 64);
                if (pm1a) return pm1a;
            }
        }
    }
    return 0x604; /* QEMU default */
}

void acpi_shutdown(void) {
    uint32_t port = find_pm1a_port();
    /* SLP_TYP=5 (S5 = soft off), SLP_EN=1 → value 0x2000 di QEMU
     * Pada hardware asli SLP_TYP bisa berbeda; scan \_S5_ di DSDT untuk nilai tepat.
     * Untuk QEMU 0x2000 selalu benar. */
    outw_ak((uint16_t)port, 0x2000);
    /* Fallback jika port ACPI berbeda: coba QEMU i440FX port */
    outw_ak(0x604, 0x2000);
    /* Jika masih berjalan (hardware), triple fault */
    __asm__ volatile ("cli; hlt");
}

void acpi_reboot(void) {
    outb_ak(0x64, 0xFE);
    /* Fallback: triple fault */
    __asm__ volatile ("cli; hlt");
}
