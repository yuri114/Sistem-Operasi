/* acpi.h — ACPI table parser minimal (hanya MADT / APIC table) */
#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

/* Maksimum AP yang didukung (di luar BSP) */
#define SMP_MAX_CPUS  8

/* Informasi tiap CPU dari MADT */
typedef struct {
    uint8_t apic_id;   /* Local APIC ID hardware */
    uint8_t acpi_id;   /* ACPI Processor UID */
    uint8_t enabled;   /* 1 = CPU enabled */
} CpuInfo;

/* Setelah acpi_init(): jumlah CPU yang ditemukan dan daftar CPU */
extern int      cpu_count;
extern CpuInfo  cpus[SMP_MAX_CPUS];

/* Inisialisasi: scan RSDP, temukan MADT, populasikan cpu_count + cpus[].
 * BSP sudah ada di cpus[0] (APIC ID yang menjalankan fungsi ini). */
void acpi_init(void);

/* ================================================================
 * Fondasi AK — ACPI Power Management
 * ================================================================ */

/* Matikan sistem via ACPI PM1a_CNT (QEMU: port 0x604, nilai 0x2000).
 * Setelah memanggil ini OS berhenti; tidak kembali. */
void acpi_shutdown(void);

/* Reboot via PS/2 keyboard controller reset line (port 0x64 <- 0xFE).
 * Sama dengan perintah 'reboot' yang sudah ada. */
void acpi_reboot(void);

#endif /* ACPI_H */
