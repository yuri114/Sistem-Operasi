#include "tss.h"
#include "acpi.h"

/* Array TSS64 per-CPU — satu slot untuk setiap CPU (BSP + AP). */
static TSS64 tss_table[SMP_MAX_CPUS];

/* tss64_desc: pointer ke awal blok TSS64 di GDT.
 * Setiap CPU menggunakan offset cpu_idx*16 dari pointer ini. */
extern uint8_t tss64_desc[];

/* ------------------------------------------------------------------ */
/* Helper internal: isi 16-byte GDT TSS descriptor untuk cpu ke-cpu_idx */

static void tss_fill_gdt_slot(int cpu_idx)
{
    uint8_t  *desc  = tss64_desc + (cpu_idx * 16);
    uint64_t  base  = (uint64_t)&tss_table[cpu_idx];
    uint32_t  limit = (uint32_t)sizeof(TSS64) - 1;

    /* Layout 16-byte TSS descriptor (System Segment, 64-bit):
     *  [0-1]  limit[15:0]
     *  [2-4]  base[23:0]
     *  [5]    access = 0x89 (P=1, DPL=0, Type=9 = Available 64-bit TSS)
     *  [6]    0x00 (granularity + limit[19:16] — limit ≤ 0xFFFF)
     *  [7]    base[31:24]
     *  [8-11] base[63:32]
     * [12-15] reserved = 0 */
    desc[0]  = (uint8_t)(limit & 0xFF);
    desc[1]  = (uint8_t)((limit >> 8) & 0xFF);
    desc[2]  = (uint8_t)(base & 0xFF);
    desc[3]  = (uint8_t)((base >> 8) & 0xFF);
    desc[4]  = (uint8_t)((base >> 16) & 0xFF);
    desc[5]  = 0x89;
    desc[6]  = 0x00;
    desc[7]  = (uint8_t)((base >> 24) & 0xFF);
    desc[8]  = (uint8_t)((base >> 32) & 0xFF);
    desc[9]  = (uint8_t)((base >> 40) & 0xFF);
    desc[10] = (uint8_t)((base >> 48) & 0xFF);
    desc[11] = (uint8_t)((base >> 56) & 0xFF);
    desc[12] = 0;
    desc[13] = 0;
    desc[14] = 0;
    desc[15] = 0;
}

/* ------------------------------------------------------------------ */

uint16_t tss64_selector(int cpu_idx)
{
    /* CPU0 = 0x30, CPU1 = 0x40, CPU2 = 0x50, ... */
    return (uint16_t)(0x30 + cpu_idx * 16);
}

void tss64_init(uint64_t kernel_stack)
{
    int i;
    uint8_t *p = (uint8_t *)&tss_table[0];
    for (i = 0; i < (int)sizeof(TSS64); i++) p[i] = 0;

    tss_table[0].rsp0       = kernel_stack;
    tss_table[0].iomap_base = (uint16_t)sizeof(TSS64);

    tss_fill_gdt_slot(0);

    /* Load TSS register: selector 0x30 */
    __asm__ volatile("ltr %%ax" :: "a"((uint16_t)0x30));
}

void tss64_ap_init(int cpu_idx, uint64_t kernel_stack)
{
    int i;
    uint8_t  *p;
    uint16_t  sel;

    if (cpu_idx <= 0 || cpu_idx >= SMP_MAX_CPUS) return;

    p = (uint8_t *)&tss_table[cpu_idx];
    for (i = 0; i < (int)sizeof(TSS64); i++) p[i] = 0;

    tss_table[cpu_idx].rsp0       = kernel_stack;
    tss_table[cpu_idx].iomap_base = (uint16_t)sizeof(TSS64);

    /* Tulis deskriptor ke GDT — aman karena BSP tidak pakai slot ini */
    tss_fill_gdt_slot(cpu_idx);

    /* Load TSS register di CPU ini (harus dipanggil dari AP sendiri) */
    sel = tss64_selector(cpu_idx);
    __asm__ volatile("ltr %%ax" :: "a"(sel));
}

void tss64_set_kernel_stack(uint64_t rsp)
{
    /* BSP: update rsp0 saat task switch ring-3 -> ring-0 */
    tss_table[0].rsp0 = rsp;
}