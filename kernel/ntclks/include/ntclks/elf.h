#ifndef NTCLKS_ELF_H
#define NTCLKS_ELF_H

#include <ntclks/types.h>

struct address_space;
struct storage_node;
struct task;

struct elf_image_info {
    bool valid;
    bool dynamic;
    uint64_t entry;
    uint64_t load_bias;
    uint16_t machine;
    uint16_t phnum;
    uint64_t low_vaddr;
    uint64_t high_vaddr;
    uint64_t phdr_vaddr;
    uint64_t interpreter_entry;
    uint32_t abi_major;
    char interp[64];
};

bool elf64_probe(const void *image, size_t len, struct elf_image_info *out);
bool elf64_load_address_space(struct address_space *as, const void *image, size_t len,
                              struct elf_image_info *out);
bool elf64_map_task_image(struct task *task, const struct storage_node *node,
                          struct elf_image_info *out);

#endif
