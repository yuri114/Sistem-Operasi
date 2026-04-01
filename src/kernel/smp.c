/* smp.c — SMP bootstrap (Tahap H)
 *
 * Strategi:
 *   1) BSP: aktifkan LAPIC, parse ACPI MADT untuk daftar APIC ID
 *   2) Copy trampoline real-mode code ke 0x7000
 *   3) Kirim INIT + SIPI (+ SIPI kedua) ke tiap AP
 *   4) AP masuk long mode via trampoline, lompat ke smp_ap_entry
 *   5) AP setup IDT + TSS + LAPIC sendiri, lalu sti dan idle
 */
#include "smp.h"
#include "apic.h"
#include "acpi.h"
#include "idt.h"
#include "tss.h"
#include "task.h"
#include "shell.h"
#include "smp_trampoline_bin_data.h"
#include <stdint.h>

volatile uint32_t smp_ap_ticks = 0;

/* Alamat fisik trampoline AP (< 1MB, 4KB aligned) */
#define AP_TRAMPOLINE_ADDR  0x7000U
#define AP_TRAMPOLINE_VEC   (AP_TRAMPOLINE_ADDR >> 12)

/* Patch offsets di binary trampoline (disepakati dengan asm) */
#define PATCH_PML4_ADDR_OFF   0x10
#define PATCH_GDT_PTR_OFF     0x18
#define PATCH_ENTRY_ADDR_OFF  0x20

volatile uint32_t smp_ap_started = 0;

/* Dipanggil dari lapic_timer_isr (asm) saat LAPIC timer AP firing */
void lapic_timer_handler(void)
{
    int i;
    uint8_t apic_id = apic_get_id();
    int cpu_idx = 0;
    for (i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == apic_id) { cpu_idx = i; break; }
    }
    task_switch_ap(cpu_idx);
}

void smp_background_task(void)
{
    for (;;) {
        volatile int d;
        for (d = 0; d < 10000000; d++) __asm__ volatile("pause");
        __sync_fetch_and_add(&smp_ap_ticks, 1);
    }
}

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
    int i;
    int cpu_idx = 0;
    uint64_t stack_top;
    uint8_t my_apic_id;

    /* --- 1. Temukan indeks CPU ini dari APIC ID --- */
    my_apic_id = apic_get_id();
    for (i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == my_apic_id) {
            cpu_idx = i;
            break;
        }
    }

    /* --- 2. Load IDT BSP (shared read-only setelah idt_init()) --- */
    idt_reload();

    /* --- 3. Setup TSS per-AP dan load TR ---
     * Stack top sama dengan formula di trampoline:
     *   0x9F000 - apic_id * 8192
     * (APIC ID dipakai karena itulah yang dipakai trampoline) */
    stack_top = 0x9F000ULL - (uint64_t)my_apic_id * 8192ULL;
    tss64_ap_init(cpu_idx, stack_top);

    /* --- 4. Aktifkan LAPIC pada AP ini ---
    /* --- 4. Aktifkan LAPIC pada AP ini --- */
    apic_enable();

    /* --- 5. Tandai AP online — BSP sudah selesai nunggu setelah ini --- */
    __sync_fetch_and_add(&smp_ap_started, 1);

    /* --- 6. Enable interrupts --- */
    __asm__ volatile("sti");

    /* --- 7. Aktifkan LAPIC timer AP: pakai kalibrasi BSP, tanpa busy-wait PIT --- */
    apic_timer_ap_start(0x40, 10);

    /* --- 8. Idle loop --- */
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

    /* Daftarkan LAPIC timer ISR ke IDT slot 0x40 */
    {
        extern void lapic_timer_isr(void);
        idt_set_gate(0x40, (uint64_t)lapic_timer_isr);
    }

    /* Kalibrasi LAPIC timer pada BSP: ukur ticks_per_ms, timer tetap masked.
     * AP akan pakai hasilnya via apic_timer_ap_start tanpa busy-wait PIT. */
    apic_timer_calibrate();

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

    /* Beritahu scheduler BSP agar tidak mencuri kernel task (is_user==0) —
     * biarkan AP yang menjalankannya via work stealing. */
    if (smp_ap_started > 0)
        task_set_has_ap(1);
}
