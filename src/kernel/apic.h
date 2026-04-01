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

#endif /* APIC_H */
