/* apic.h — Local APIC (xAPIC) interface */
#ifndef APIC_H
#define APIC_H

#include <stdint.h>

/* Deteksi APIC via CPUID EDX bit 9. */
int     apic_present(void);

/* Aktifkan LAPIC pada core saat ini (tulis SVR, bersihkan TPR). */
void    apic_enable(void);

/* Kirim EOI ke LAPIC (gunakan sebagai pengganti PIC EOI jika beralih ke LAPIC timer). */
void    apic_eoi(void);

/* Baca Local APIC ID core saat ini (bits 31:24 dari register LAPIC_ID). */
uint8_t apic_get_id(void);

/* Kirim INIT IPI ke AP dengan APIC ID tertentu. */
void    apic_send_init(uint8_t apic_id);

/* Kirim STARTUP IPI (SIPI) ke AP; vector = trampoline_addr >> 12. */
void    apic_send_sipi(uint8_t apic_id, uint8_t vector);

/* ------------------------------------------------------------------
 * LAPIC Timer
 * ------------------------------------------------------------------ */

/* Inisialisasi LAPIC timer periodic pada core saat ini.
 *   vector : nomor INT yang akan di-trigger (misalnya 0x40 = INT 64)
 *   ms     : periode dalam milidetik (dikalibrasi terhadap PIT tick)
 */
void    apic_timer_calibrate(void);                        /* BSP: kalibrasi saja, timer tetap masked */
void    apic_timer_init(uint8_t vector, uint32_t ms);      /* BSP: kalibrasi + program (opsional) */
void    apic_timer_ap_start(uint8_t vector, uint32_t ms);  /* AP: program pakai kalibrasi BSP */

#endif /* APIC_H */
