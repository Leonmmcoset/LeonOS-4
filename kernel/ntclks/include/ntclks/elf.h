#ifndef NTCLKS_ELF_H
#define NTCLKS_ELF_H

#include <ntclks/types.h>

struct address_space;

struct elf_image_info {
    bool valid;
    uint64_t entry;
    uint16_t machine;
    uint16_t phnum;
    uint64_t low_vaddr;
    uint64_t high_vaddr;
};

bool elf64_probe(const void *image, size_t len, struct elf_image_info *out);
bool elf64_load_address_space(struct address_space *as, const void *image, size_t len,
                              struct elf_image_info *out);

#endif
