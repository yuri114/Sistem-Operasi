#ifndef ELF_LOADER_H
#define ELF_LOADER_H
#include <stdint.h>

/* Muat ELF64 dari buffer data ke halaman user process.
 * Kembalikan entry point (uint64_t), atau 0 jika gagal. */
uint64_t elf_load(const uint8_t *data, uint32_t size, uint64_t *pml4);

#endif