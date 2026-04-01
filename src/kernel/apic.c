/* apic.c — Local APIC (xAPIC) driver
 *
 * Menggunakan MMIO base 0xFEE00000 (default APIC base, identity-mapped
 * oleh kernel_entry.asm karena PD[3-4GB] maps 0xC0000000-0xFFFFFFFF).
 *
 * Urutan inisialisasi tiap-core:
 *   1. apic_present()  — verifikasi CPUID
 *   2. apic_enable()   — tulis IA32_APIC_BASE MSR + SVR register
 *   3. (opsional tahap lanjut) migrasi IRQ ke IOAPIC/LAPIC timer
 *
 * IRQ routing setelah APIC diaktifkan:
 *   - Timer    : LAPIC timer (jika dipakai) atau IOAPIC redirect PIT IRQ0
 *   - Keyboard : IOAPIC redirect (IRQ1 → LAPIC LVT, INT 33)
 *   - Saat ini kita tetap pakai PIT + PIC untuk IO IRQ, hanya
 *     inisialisasi LAPIC untuk mendukung INIT/SIPI ke AP.
 */
#include "apic.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* LAPIC MMIO base (identity-mapped, VA == PA di kernel kita) */
#define LAPIC_BASE  0xFEE00000UL

/* Register offset dalam MMIO LAPIC */
#define LAPIC_ID          0x0020  /* APIC ID Register */
#define LAPIC_VER         0x0030  /* Version Register */
#define LAPIC_TPR         0x0080  /* Task Priority Register */
#define LAPIC_SVR         0x00F0  /* Spurious Interrupt Vector Register */
#define LAPIC_ICR_LO      0x0300  /* Interrupt Command Register [31:0] */
#define LAPIC_ICR_HI      0x0310  /* Interrupt Command Register [63:32] */
#define LAPIC_EOI         0x00B0  /* EOI Register */

/* SVR: bit 8 = APIC Software Enable; vektor 0xFF (spurious) */
#define SVR_ENABLE        (0x100 | 0xFF)

/* ICR flags */
#define ICR_INIT          0x00004500UL  /* INIT: delivery=101, level=assert */
#define ICR_INIT_DEASSERT 0x00008500UL  /* INIT: de-assert */
#define ICR_STARTUP       0x00004600UL  /* SIPI: delivery=110 (start-up) */

/* ------------------------------------------------------------------ */
/* Helper baca/tulis LAPIC register (32-bit MMIO) */

static inline uint32_t lapic_read(uint32_t reg)
{
    volatile uint32_t *p = (volatile uint32_t *)(LAPIC_BASE + reg);
    return *p;
}

static inline void lapic_write(uint32_t reg, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)(LAPIC_BASE + reg);
    *p = val;
}

/* Tunggu ICR[11] (Delivery Status) = 0 (idle) sebelum kirim IPI berikutnya. */
static void lapic_wait_icr_idle(void)
{
    while (lapic_read(LAPIC_ICR_LO) & (1u << 12))
        __asm__ volatile("pause");
}

/* ------------------------------------------------------------------ */

int apic_present(void)
{
    uint32_t edx;
    __asm__ volatile("cpuid" : "=d"(edx) : "a"(1) : "ebx", "ecx");
    return (edx >> 9) & 1;   /* bit 9 = APIC On-Chip */
}

void apic_enable(void)
{
    /* Aktifkan via IA32_APIC_BASE MSR (MSR 0x1B): set bit 11 (global enable).
     * Sekedar memastikan hardware flag aktif; QEMU biasanya default enabled. */
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1BU));
    lo |= (1u << 11);   /* Global APIC Enable */
    __asm__ volatile("wrmsr" :: "c"(0x1BU), "a"(lo), "d"(hi));

    /* Clear Task Priority → terima semua interrupt */
    lapic_write(LAPIC_TPR, 0);

    /* Tulis SVR: aktifkan LAPIC software, set spurious vektor 0xFF */
    lapic_write(LAPIC_SVR, SVR_ENABLE);
}

void apic_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

uint8_t apic_get_id(void)
{
    /* APIC ID di bits [31:24] register LAPIC_ID */
    return (uint8_t)(lapic_read(LAPIC_ID) >> 24);
}

void apic_send_init(uint8_t apic_id)
{
    lapic_wait_icr_idle();
    /* ICR High: set destination APIC ID di bits [31:24] */
    lapic_write(LAPIC_ICR_HI, (uint32_t)apic_id << 24);
    /* ICR Low: INIT assert */
    lapic_write(LAPIC_ICR_LO, ICR_INIT);
    lapic_wait_icr_idle();

    /* INIT de-assert (level-triggered) setelah ~10ms */
    /* Busy-wait sederhana: ~10ms pada CPU 1GHz ≈ 10M iterasi */
    volatile int w = 0;
    for (w = 0; w < 1000000; w++) __asm__ volatile("pause");

    lapic_write(LAPIC_ICR_HI, (uint32_t)apic_id << 24);
    lapic_write(LAPIC_ICR_LO, ICR_INIT_DEASSERT);
    lapic_wait_icr_idle();
}

void apic_send_sipi(uint8_t apic_id, uint8_t vector)
{
    lapic_wait_icr_idle();
    lapic_write(LAPIC_ICR_HI, (uint32_t)apic_id << 24);
    /* ICR Low: SIPI dengan trampoline page vector */
    lapic_write(LAPIC_ICR_LO, ICR_STARTUP | vector);
    lapic_wait_icr_idle();
}
