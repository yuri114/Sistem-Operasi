/* elf_loader.c ? Muat ELF64 executable ke address space user (4-level paging) */
#include "elf_loader.h"
#include "vmm.h"

/* ELF64 header */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;    /* 0x3E = AMD64 */
    uint32_t e_version;
    uint64_t e_entry;      /* entry point virtual address */
    uint64_t e_phoff;      /* program header table offset */
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

/* ELF64 program header */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;     /* offset di file */
    uint64_t p_vaddr;      /* virtual address tujuan */
    uint64_t p_paddr;
    uint64_t p_filesz;     /* ukuran di file */
    uint64_t p_memsz;      /* ukuran di memori (>= filesz untuk .bss) */
    uint64_t p_align;
} __attribute__((packed)) Elf64_Phdr;

uint64_t elf_load(const uint8_t *data, uint32_t size, uint64_t *pml4) {
    if (size < sizeof(Elf64_Ehdr)) return 0;
    Elf64_Ehdr *hdr = (Elf64_Ehdr *)data;

    /* Validasi magic ELF */
    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
        hdr->e_ident[2] != 'L'  || hdr->e_ident[3] != 'F') return 0;
    /* EI_CLASS harus 2 (64-bit) */
    if (hdr->e_ident[4] != 2) return 0;
    /* ET_EXEC (2) dan EM_X86_64 (0x3E) */
    if (hdr->e_type != 2 || hdr->e_machine != 0x3E) return 0;
    if (hdr->e_phnum == 0) return 0;

    int i;
    for (i = 0; i < (int)hdr->e_phnum; i++) {
        uint64_t ph_off = hdr->e_phoff + (uint64_t)i * hdr->e_phentsize;
        if (ph_off + hdr->e_phentsize > (uint64_t)size) return 0;

        Elf64_Phdr *phdr = (Elf64_Phdr *)(data + ph_off);
        if (phdr->p_type != 1) continue;  /* hanya PT_LOAD */

        /* Bounds check data segmen */
        if (phdr->p_filesz > 0) {
            if (phdr->p_offset >= size || phdr->p_offset + phdr->p_filesz > size)
                return 0;
        }

        /* Iterasi per 4KB halaman virtual */
        uint64_t virt_page = phdr->p_vaddr & ~(uint64_t)(PAGE_SIZE - 1);
        uint64_t virt_end  = (phdr->p_vaddr + phdr->p_memsz + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

        while (virt_page < virt_end) {
            uint64_t phys = pmm_alloc_frame();
            if (!phys) return 0;

            /* Zero-fill frame terlebih dahulu */
            uint8_t *frame_ptr = (uint8_t *)phys;
            uint64_t j;
            for (j = 0; j < PAGE_SIZE; j++) frame_ptr[j] = 0;

            /* Salin byte ELF yang jatuh dalam halaman ini */
            uint64_t seg_file_end = phdr->p_vaddr + phdr->p_filesz;
            uint64_t page_end     = virt_page + PAGE_SIZE;
            uint64_t copy_start   = virt_page > phdr->p_vaddr ? virt_page : phdr->p_vaddr;
            uint64_t copy_end     = seg_file_end < page_end ? seg_file_end : page_end;

            if (copy_start < copy_end) {
                const uint8_t *src = data + phdr->p_offset + (copy_start - phdr->p_vaddr);
                uint8_t *dst       = frame_ptr + (copy_start - virt_page);
                uint64_t len       = copy_end - copy_start;
                for (j = 0; j < len; j++) dst[j] = src[j];
            }

            /* Map page: User-accessible (flags=7: P+RW+User) */
            vmm_map_page(pml4, virt_page, phys, 7);
            virt_page += PAGE_SIZE;
        }
    }
    return hdr->e_entry;
}