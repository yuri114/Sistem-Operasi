/* smp.h — SMP bootstrap minimal (BSP + AP startup) */
#ifndef SMP_H
#define SMP_H

#include <stdint.h>

/* Diisi oleh AP setelah berhasil start di mode 64-bit. */
extern volatile uint32_t smp_ap_started;

/* Inisialisasi SMP pada BSP:
 *   - enable LAPIC
 *   - parse MADT, kirim INIT/SIPI ke AP
 *   - tunggu AP melapor via smp_ap_started
 */
void smp_init(void);

/* Dipanggil oleh AP saat masuk kernel via trampoline.
 * Menandai AP online lalu halt (untuk tahap awal). */
void smp_ap_entry(void);

#endif /* SMP_H */
