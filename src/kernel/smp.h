/* smp.h — SMP bootstrap minimal (BSP + AP startup) */
#ifndef SMP_H
#define SMP_H

#include <stdint.h>

/* Diisi oleh AP setelah berhasil start di mode 64-bit. */
extern volatile uint32_t smp_ap_started;

/* Counter yang dinaikkan background task AP setiap 500ms.
 * Dipakai sebagai demonstrasi bahwa AP benar-benar mengeksekusi task. */
extern volatile uint32_t smp_ap_ticks;

/* Inisialisasi SMP pada BSP. */
void smp_init(void);

/* Dipanggil oleh AP saat masuk kernel via trampoline. */
void smp_ap_entry(void);

/* Background kernel task yang dijalankan AP; bisa di-spawn via task_create(). */
void smp_background_task(void);

#endif /* SMP_H */
