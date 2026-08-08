#include <ntclks/elf.h>
#include <ntclks/mm.h>
#include <ntclks/paging.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>

#define EI_NIDENT 16
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PAGE_SIZE 4096ULL
#define ELF_HEADER_READ_BYTES 4096U

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

static uint8_t elf_header_scratch[ELF_HEADER_READ_BYTES];

static uint64_t align_down(uint64_t value)
{
    return value & ~(PAGE_SIZE - 1);
}

static uint64_t align_up(uint64_t value)
{
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static bool elf64_program_headers_fit(const struct elf64_ehdr *eh, size_t len)
{
    if (!eh || eh->e_phentsize != sizeof(struct elf64_phdr) ||
        eh->e_phoff > len) {
        return false;
    }
    return eh->e_phnum <= (len - eh->e_phoff) / eh->e_phentsize;
}

bool elf64_probe(const void *image, size_t len, struct elf_image_info *out)
{
    if (!image || len < sizeof(struct elf64_ehdr) || !out) {
        return false;
    }

    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)image;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F' ||
        eh->e_ident[4] != 2 || eh->e_ident[5] != 1 || eh->e_ident[6] != 1 ||
        eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64 ||
        eh->e_version != 1 || eh->e_ehsize != sizeof(*eh) ||
        !elf64_program_headers_fit(eh, len)) {
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
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)((const uint8_t *)image + eh->e_phoff +
                                        (uint64_t)i * eh->e_phentsize);
        uint64_t end;
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (ph->p_memsz > UINT64_MAX - ph->p_vaddr) {
            out->valid = false;
            return false;
        }
        end = ph->p_vaddr + ph->p_memsz;
        if (ph->p_vaddr < out->low_vaddr) {
            out->low_vaddr = ph->p_vaddr;
        }
        if (end > out->high_vaddr) {
            out->high_vaddr = end;
        }
    }

    return true;
}

static bool elf64_segment_valid(const struct elf64_phdr *ph, uint64_t file_len)
{
    if (!ph || ph->p_filesz > ph->p_memsz || ph->p_offset > file_len ||
        ph->p_filesz > file_len - ph->p_offset ||
        ph->p_vaddr < NTCLKS_USER_BASE ||
        ph->p_memsz > NTCLKS_USER_TOP - ph->p_vaddr) {
        return false;
    }
    return true;
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
    uint64_t copied = 0;
    while (copied < len) {
        uint64_t address = vaddr + copied;
        uint64_t phys = address_space_user_page_phys(as, address);
        uint64_t page_left = PAGE_SIZE - (address & (PAGE_SIZE - 1));
        uint64_t take = len - copied < page_left ? len - copied : page_left;
        uint8_t *dst;
        if (!phys) {
            return false;
        }
        dst = (uint8_t *)(uintptr_t)(phys + (address & (PAGE_SIZE - 1)));
        for (uint64_t i = 0; i < take; ++i) {
            dst[i] = src[copied + i];
        }
        copied += take;
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
            (const struct elf64_phdr *)((const uint8_t *)image + eh->e_phoff +
                                        (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        if (!elf64_segment_valid(ph, len)) {
            out->valid = false;
            return false;
        }
        uint64_t flags = (ph->p_flags & PF_W) ? NTCLKS_PAGE_WRITABLE : 0;
        if (!ensure_segment_pages(as, ph->p_vaddr, ph->p_vaddr + ph->p_memsz, flags) ||
            !copy_to_address_space(as, ph->p_vaddr,
                                   (const uint8_t *)image + ph->p_offset,
                                   ph->p_filesz)) {
            out->valid = false;
            return false;
        }
    }

    return true;
}

static bool elf64_read_headers(const struct storage_node *node, const void **out_image,
                               size_t *out_len)
{
    const struct elf64_ehdr *eh;
    uint64_t header_len;
    uint32_t got = 0;

    if (!node || node->type != LEONOS_FS_TYPE_FILE ||
        node->size < sizeof(struct elf64_ehdr) || !out_image || !out_len) {
        return false;
    }
    if (storage_read_node(node, 0, elf_header_scratch, sizeof(struct elf64_ehdr), &got) < 0 ||
        got != sizeof(struct elf64_ehdr)) {
        return false;
    }
    eh = (const struct elf64_ehdr *)elf_header_scratch;
    if (eh->e_phentsize != sizeof(struct elf64_phdr) || eh->e_phoff > node->size ||
        eh->e_phnum > (node->size - eh->e_phoff) / eh->e_phentsize) {
        return false;
    }
    header_len = eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize;
    if (header_len < sizeof(*eh)) {
        header_len = sizeof(*eh);
    }
    if (header_len > sizeof(elf_header_scratch)) {
        return false;
    }
    got = 0;
    if (storage_read_node(node, 0, elf_header_scratch, (uint32_t)header_len, &got) < 0 ||
        got != header_len) {
        return false;
    }
    *out_image = elf_header_scratch;
    *out_len = (size_t)header_len;
    return true;
}

static bool elf64_task_range_available(const struct task *task, uint64_t start, uint64_t end)
{
    uint64_t stack_low;
    if (!task || start < NTCLKS_USER_BASE || start >= end || end > NTCLKS_USER_TOP ||
        task->stack_top < (uint64_t)NTCLKS_USER_STACK_PAGES * PAGE_SIZE) {
        return false;
    }
    stack_low = task->stack_top - (uint64_t)NTCLKS_USER_STACK_PAGES * PAGE_SIZE;
    if (end > stack_low) {
        return false;
    }
    for (uint32_t i = 0; i < SCHED_TASK_VMA_MAX; ++i) {
        const struct task_vma *vma = &task->vmas[i];
        if (vma->used && start < vma->end && end > vma->start) {
            return false;
        }
    }
    return true;
}

static struct task_vma *elf64_task_free_vma(struct task *task)
{
    if (!task) {
        return NULL;
    }
    for (uint32_t i = 0; i < SCHED_TASK_VMA_MAX; ++i) {
        if (!task->vmas[i].used) {
            return &task->vmas[i];
        }
    }
    return NULL;
}

bool elf64_map_task_image(struct task *task, const struct storage_node *node,
                          struct elf_image_info *out)
{
    const void *image;
    size_t image_len;
    const struct elf64_ehdr *eh;
    uint32_t load_count = 0;
    uint32_t free_vmas = 0;
    bool entry_found = false;

    if (!task || !node || node->size == 0 ||
        !elf64_read_headers(node, &image, &image_len) ||
        !elf64_probe(image, image_len, out)) {
        return false;
    }
    eh = (const struct elf64_ehdr *)image;
    for (uint32_t i = 0; i < SCHED_TASK_VMA_MAX; ++i) {
        if (!task->vmas[i].used) {
            ++free_vmas;
        }
    }
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)((const uint8_t *)image + eh->e_phoff +
                                        (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        if (!elf64_segment_valid(ph, node->size) ||
            (ph->p_offset & (PAGE_SIZE - 1)) != (ph->p_vaddr & (PAGE_SIZE - 1)) ||
            ph->p_memsz > task->stack_top -
                              (uint64_t)NTCLKS_USER_STACK_PAGES * PAGE_SIZE - ph->p_vaddr) {
            out->valid = false;
            return false;
        }
        ++load_count;
        if ((ph->p_flags & PF_X) && out->entry >= ph->p_vaddr &&
            out->entry - ph->p_vaddr < ph->p_memsz) {
            entry_found = true;
        }
    }
    if (!load_count || load_count > free_vmas || !entry_found) {
        out->valid = false;
        return false;
    }

    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)((const uint8_t *)image + eh->e_phoff +
                                        (uint64_t)i * eh->e_phentsize);
        struct task_vma *vma;
        struct storage_node segment_node;
        uint64_t start;
        uint64_t end;
        uint64_t file_offset;
        uint64_t file_limit;
        uint32_t prot = TASK_VMA_PROT_READ;

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        if (ph->p_memsz > UINT64_MAX - ph->p_vaddr - (PAGE_SIZE - 1)) {
            out->valid = false;
            return false;
        }
        start = align_down(ph->p_vaddr);
        end = align_up(ph->p_vaddr + ph->p_memsz);
        if (ph->p_offset < ph->p_vaddr - start ||
            !elf64_task_range_available(task, start, end)) {
            out->valid = false;
            return false;
        }
        if (!address_space_prepare_user_range(&task->as, start, end)) {
            out->valid = false;
            return false;
        }
        vma = elf64_task_free_vma(task);
        if (!vma) {
            out->valid = false;
            return false;
        }
        file_offset = ph->p_offset - (ph->p_vaddr - start);
        file_limit = ph->p_offset + ph->p_filesz;
        segment_node = *node;
        segment_node.size = file_limit;
        if (ph->p_flags & PF_W) {
            prot |= TASK_VMA_PROT_WRITE;
        }
        if (ph->p_flags & PF_X) {
            prot |= TASK_VMA_PROT_EXEC;
        }
        *vma = (struct task_vma){
            .used = 1,
            .prot = prot,
            .flags = TASK_VMA_FLAG_PRIVATE | TASK_VMA_FLAG_FILE | TASK_VMA_FLAG_LAZY,
            .start = start,
            .end = end,
            .file_offset = file_offset,
            .file_limit = file_limit,
            .file_node = segment_node,
        };
    }
    return true;
}
