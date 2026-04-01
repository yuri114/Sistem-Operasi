/* smp.c — SMP bootstrap minimal (Tahap H)
 *
 * Strategi implementasi awal:
 *   1) BSP: aktifkan LAPIC, parse ACPI MADT untuk daftar APIC ID
 *   2) Copy trampoline real-mode code ke 0x7000
 *   3) Kirim INIT + SIPI (+ SIPI kedua) ke tiap AP
 *   4) AP masuk long mode via trampoline, lompat ke smp_ap_entry
 *   5) AP increment smp_ap_started dan halt
 *
 * Catatan penting:
 *   - Trampoline disediakan di smp_trampoline.asm (flat binary header)
 *   - Trampoline menggunakan page table kernel di 0x1000
 *   - Ini baseline bootstrap; scheduler per-core dan per-AP IDT/TSS
 *     akan ditingkatkan di iterasi berikutnya.
 */
#include "smp.h"
#include "apic.h"
#include "acpi.h"
#include "shell.h"
#include "smp_trampoline_bin_data.h"
#include <stdint.h>

/* Alamat fisik trampoline AP (< 1MB, 4KB aligned) */
#define AP_TRAMPOLINE_ADDR  0x7000U
#define AP_TRAMPOLINE_VEC   (AP_TRAMPOLINE_ADDR >> 12)

/* Patch offsets di binary trampoline (disepakati dengan asm) */
#define PATCH_PML4_ADDR_OFF   0x10
#define PATCH_GDT_PTR_OFF     0x18
#define PATCH_ENTRY_ADDR_OFF  0x20

volatile uint32_t smp_ap_started = 0;

static void mem_copy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

static void patch_u64(uint8_t *base, uint32_t off, uint64_t v)
{
    uint32_t i;
    for (i = 0; i < 8; i++)
        base[off + i] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

/* Symbol dari kernel_entry.asm */
extern uint8_t kernel_gdt64_ptr[];

void smp_ap_entry(void)
{
    /* AP sudah di long mode dan stack lokal dari trampoline.
     * Untuk tahap awal cukup tandai online lalu halt. */
    __sync_fetch_and_add(&smp_ap_started, 1);

    for (;;)
        __asm__ volatile("hlt");
}

void smp_init(void)
{
    int i;

    if (!apic_present()) {
        print("[SMP] APIC tidak tersedia\n");
        return;
    }

    /* PIC legacy sengaja belum dimatikan permanen karena IRQ timer/keyboard
     * masih lewat PIC (IOAPIC routing belum diaktifkan pada iterasi ini). */
    apic_enable();

    /* Enumerasi CPU dari ACPI MADT. */
    acpi_init();

    print("[SMP] CPU terdeteksi: ");
    {
        char nbuf[16];
        itoa((uint32_t)cpu_count, nbuf);
        print(nbuf);
        print("\n");
    }

    if (cpu_count <= 1) {
        print("[SMP] Single-core, tidak ada AP\n");
        return;
    }

    /* Salin trampoline dan patch parameter runtime. */
    {
        uint8_t *tr = (uint8_t *)(uintptr_t)AP_TRAMPOLINE_ADDR;
        uint32_t len = (uint32_t)build_smp_trampoline_bin_len;
        if (len > 4096) len = 4096;

        mem_copy(tr, build_smp_trampoline_bin, len);

        patch_u64(tr, PATCH_PML4_ADDR_OFF, 0x1000ULL);                  /* boot PML4 */
        patch_u64(tr, PATCH_GDT_PTR_OFF, (uint64_t)(uintptr_t)kernel_gdt64_ptr);
        patch_u64(tr, PATCH_ENTRY_ADDR_OFF, (uint64_t)(uintptr_t)&smp_ap_entry);
    }

    /* Start setiap AP (selain BSP yang id-nya cpus[0]). */
    for (i = 1; i < cpu_count; i++) {
        uint8_t apic_id = cpus[i].apic_id;

        apic_send_init(apic_id);

        /* Intel spec: kirim SIPI dua kali (redundansi) */
        apic_send_sipi(apic_id, AP_TRAMPOLINE_VEC);
        {
            volatile int d;
            for (d = 0; d < 200000; d++) __asm__ volatile("pause");
        }
        apic_send_sipi(apic_id, AP_TRAMPOLINE_VEC);
    }

    /* Tunggu AP online (timeout sederhana). */
    {
        volatile uint32_t wait;
        for (wait = 0; wait < 50000000; wait++) {
            if ((int)smp_ap_started >= (cpu_count - 1)) break;
            __asm__ volatile("pause");
        }
    }

    print("[SMP] AP online: ");
    {
        char nbuf[16];
        itoa(smp_ap_started, nbuf);
        print(nbuf);
        print("/");
        itoa((uint32_t)(cpu_count - 1), nbuf);
        print(nbuf);
        print("\n");
    }
}
