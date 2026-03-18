#ifndef MEMORY_H
#define MEMORY_H
#include <stdint.h>

void mem_init();
void* malloc(uint32_t size);
void free(void* ptr);

#endif