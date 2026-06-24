#include <ntclks/elf.h>
#include <ntclks/mm.h>
#include <ntclks/paging.h>

#define EI_NIDENT 16
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_W 2
#define PAGE_SIZE 4096ULL

struct elf64_ehdr {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

bool elf64_probe(const void *image, size_t len, struct elf_image_info *out)
{
    if (!image || len < sizeof(struct elf64_ehdr) || !out) {
        return false;
    }

    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)image;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F' ||
        eh->e_ident[4] != 2 || eh->e_type != ET_EXEC ||
        eh->e_machine != EM_X86_64) {
        out->valid = false;
        return false;
    }
    if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > len) {
        out->valid = false;
        return false;
    }

    out->valid = true;
    out->entry = eh->e_entry;
    out->machine = eh->e_machine;
    out->phnum = eh->e_phnum;
    out->low_vaddr = UINT64_MAX;
    out->high_vaddr = 0;

    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)((const uint8_t *)image + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (ph->p_vaddr < out->low_vaddr) {
            out->low_vaddr = ph->p_vaddr;
        }
        if (ph->p_vaddr + ph->p_memsz > out->high_vaddr) {
            out->high_vaddr = ph->p_vaddr + ph->p_memsz;
        }
    }

    return true;
}

static uint64_t align_down(uint64_t value)
{
    return value & ~(PAGE_SIZE - 1);
}

static uint64_t align_up(uint64_t value)
{
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static bool ensure_segment_pages(struct address_space *as, uint64_t start,
                                 uint64_t end, uint64_t flags)
{
    for (uint64_t page = align_down(start); page < align_up(end); page += PAGE_SIZE) {
        if (address_space_user_page_phys(as, page)) {
            continue;
        }
        uint64_t phys = mm_alloc_page();
        if (!phys || !address_space_map_user_page(as, page, phys, flags)) {
            if (phys) {
                mm_free_page(phys);
            }
            return false;
        }
    }
    return true;
}

static bool copy_to_address_space(struct address_space *as, uint64_t vaddr,
                                  const uint8_t *src, uint64_t len)
{
    for (uint64_t i = 0; i < len; ++i) {
        uint64_t phys = address_space_user_page_phys(as, vaddr + i);
        if (!phys) {
            return false;
        }
        uint8_t *dst = (uint8_t *)(uintptr_t)(phys + ((vaddr + i) & (PAGE_SIZE - 1)));
        *dst = src[i];
    }
    return true;
}

bool elf64_load_address_space(struct address_space *as, const void *image, size_t len,
                              struct elf_image_info *out)
{
    if (!as || !elf64_probe(image, len, out)) {
        return false;
    }

    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)image;
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)((const uint8_t *)image + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (ph->p_offset + ph->p_filesz > len || ph->p_vaddr < NTCLKS_USER_BASE ||
            ph->p_vaddr + ph->p_memsz > NTCLKS_USER_TOP || ph->p_filesz > ph->p_memsz) {
            out->valid = false;
            return false;
        }
        uint64_t flags = (ph->p_flags & PF_W) ? NTCLKS_PAGE_WRITABLE : 0;
        if (!ensure_segment_pages(as, ph->p_vaddr, ph->p_vaddr + ph->p_memsz, flags)) {
            out->valid = false;
            return false;
        }
        if (!copy_to_address_space(as, ph->p_vaddr, (const uint8_t *)image + ph->p_offset,
                                   ph->p_filesz)) {
            out->valid = false;
            return false;
        }
    }

    return true;
}
