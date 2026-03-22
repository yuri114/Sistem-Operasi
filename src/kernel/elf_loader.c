#include "elf_loader.h"
#include "vmm.h"

// struktur ELF32 header

typedef struct {
    uint8_t e_ident[16]; //identifikasi ELF
    uint16_t e_type; //tipe file (relocatable, executable, dll)
    uint16_t e_machine; //arsitektur (0x03 = x86)
    uint32_t e_version; //versi ELF (1)
    uint32_t e_entry; //alamat entry point untuk eksekusi
    uint32_t e_phoff; //offset ke program header table
    uint32_t e_shoff; //offset ke section header table
    uint32_t e_flags; //flag khusus arsitektur
    uint16_t e_ehsize; //ukuran ELF header
    uint16_t e_phentsize; //ukuran setiap entry di program header table
    uint16_t e_phnum; //jumlah entry di program header table
    uint16_t e_shentsize; //ukuran setiap entry di section header table
    uint16_t e_shnum; //jumlah entry di section header table
    uint16_t e_shstrndx; //index section header string table
}__attribute__((packed)) Elf32_Ehdr;

//struktur ELF32 program header
typedef struct {
    uint32_t p_type; //tipe segmen (1 = loadable)
    uint32_t p_offset; //offset ke data segmen di file
    uint32_t p_vaddr; //alamat virtual untuk memuat segmen
    uint32_t p_paddr; //alamat fisik (tidak digunakan di OS modern)
    uint32_t p_filesz; //ukuran segmen di file
    uint32_t p_memsz; //ukuran segmen di memori (bisa lebih besar dari filesz untuk .bss)
    uint32_t p_flags; //flag akses (bit 0 = eksekusi, bit 1 = tulis, bit 2 = baca)
    uint32_t p_align; //alignmen segmen di memori
}__attribute__((packed)) Elf32_Phdr;

uint32_t elf_load(const uint8_t *data, uint32_t size, uint32_t *page_dir) {
    Elf32_Ehdr *hdr = (Elf32_Ehdr*)data;
    
    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' || hdr->e_ident[2] != 'L' || hdr->e_ident[3] != 'F') {
        return 0; //bukan file ELF yang valid
    }

    int i;
    for (i = 0; i < hdr->e_phnum; i++) {
        Elf32_Phdr *phdr = (Elf32_Phdr*)(data + hdr->e_phoff + i * hdr->e_phentsize);
        if (phdr->p_type != 1) continue; //hanya proses segmen yang bisa dimuat

        // iterasi per halaman (4KB) dalam segmen ini
        uint32_t virt_page = phdr->p_vaddr & 0xFFFFF000; //halaman pertama, dibulatkan ke bawah
        uint32_t virt_end  = (phdr->p_vaddr + phdr->p_memsz + PAGE_SIZE - 1) & 0xFFFFF000; //halaman terakhir

        while (virt_page < virt_end) {
            // alokasikan frame fisik dari PMM untuk halaman ini
            uint32_t phys = pmm_alloc_frame();
            if (!phys) return 0; //kehabisan frame fisik

            // zero-fill frame lewat identity mapping kernel (PMM frame ada di 3MB-4MB, sudah di-identity map)
            uint8_t *frame_ptr = (uint8_t*)phys;
            uint32_t j;
            for (j = 0; j < PAGE_SIZE; j++) frame_ptr[j] = 0;

            // salin byte ELF yang jatuh dalam halaman virtual ini
            // range halaman virtual: [virt_page, virt_page + PAGE_SIZE)
            // range data file segmen: [p_vaddr, p_vaddr + p_filesz)
            uint32_t seg_file_end = phdr->p_vaddr + phdr->p_filesz;
            uint32_t page_end     = virt_page + PAGE_SIZE;
            uint32_t copy_start   = virt_page > phdr->p_vaddr ? virt_page : phdr->p_vaddr;
            uint32_t copy_end     = seg_file_end < page_end ? seg_file_end : page_end;

            if (copy_start < copy_end) {
                uint8_t *src = (uint8_t*)data + phdr->p_offset + (copy_start - phdr->p_vaddr);
                uint8_t *dst = frame_ptr + (copy_start - virt_page);
                uint32_t copy_len = copy_end - copy_start;
                for (j = 0; j < copy_len; j++) dst[j] = src[j];
            }

            // petakan halaman virtual ke frame fisik di page directory proses
            vmm_map_page(page_dir, virt_page, phys, 7);
            virt_page += PAGE_SIZE;
        }
    }
    return hdr->e_entry; //kembalikan alamat entry point untuk eksekusi
}