#ifndef ELF_LOADER_H
#define ELF_LOADER_H
#include <stdint.h>

uint32_t elf_load(const uint8_t *data, uint32_t size, uint32_t *page_dir);

#endif