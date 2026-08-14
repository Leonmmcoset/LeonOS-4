/*
 * LeonOS ELF loader: validates and maps static and dynamic x86_64 images.
 * Enforces ABI, W^X, PT_INTERP, segment-layout, and ASLR startup rules.
 */
#include <leonos/elf_abi.h>
#include <ntclks/console.h>
#include <ntclks/elf.h>
#include <ntclks/mm.h>
#include <ntclks/paging.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>

#define EI_NIDENT 16
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64 62
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define PF_X 1
#define PF_W 2
#define PAGE_SIZE 4096ULL
#define ELF_HEADER_READ_BYTES 4096U
#define ELF_DYN_MAIN_MIN 0x01000000ULL
#define ELF_DYN_MAIN_MAX 0x03800000ULL
#define ELF_DYN_INTERP_MIN 0x03800000ULL
#define ELF_DYN_INTERP_MAX 0x05000000ULL

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

struct elf64_nhdr {
    uint32_t n_namesz;
    uint32_t n_descsz;
    uint32_t n_type;
};

static uint8_t elf_header_scratch[ELF_HEADER_READ_BYTES];
static uint64_t aslr_counter;
static bool weak_entropy_reported;

/**
 * @brief Coordinates the align down operation.
 * @param value Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint64_t align_down(uint64_t value)
{
    return value & ~(PAGE_SIZE - 1ULL);
}

/**
 * @brief Coordinates the align up operation.
 * @param value Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint64_t align_up(uint64_t value)
{
    if (value > UINT64_MAX - (PAGE_SIZE - 1ULL)) {
        return 0;
    }
    return (value + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
}

/**
 * @brief Coordinates the align4 up operation.
 * @param value Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint64_t align4_up(uint64_t value)
{
    if (value > UINT64_MAX - 3ULL) {
        return 0;
    }
    return (value + 3ULL) & ~3ULL;
}

/**
 * @brief Coordinates the elf64 random u64 operation.
 * @param strong Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint64_t elf64_random_u64(bool *strong)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint64_t result = 0;
    unsigned char ok = 0;
    uint64_t cycles;

    __asm__ volatile("rdtsc" : "=a"(eax), "=d"(edx));
    cycles = ((uint64_t)edx << 32) | eax;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));
    if (ecx & (1u << 30)) {
        __asm__ volatile("rdrand %0; setc %1" : "=r"(result), "=qm"(ok));
    }
    if (ok) {
        if (strong) {
            *strong = true;
        }
        return result;
    }
    ++aslr_counter;
    result = cycles ^ ((uint64_t)(uintptr_t)&result << 17) ^
             ((uint64_t)sched_tick_count() << 32) ^ (aslr_counter * 0x9e3779b97f4a7c15ULL);
    if (strong) {
        *strong = false;
    }
    return result;
}

/**
 * @brief Coordinates the elf64 fill random operation.
 * @param out Caller-provided storage that receives output from this operation.
 */
static void elf64_fill_random(uint8_t out[16])
{
    bool strong = false;
    uint64_t first = elf64_random_u64(&strong);
    uint64_t second = elf64_random_u64(NULL);
    for (uint32_t i = 0; i < 8; ++i) {
        out[i] = (uint8_t)(first >> (i * 8));
        out[8 + i] = (uint8_t)(second >> (i * 8));
    }
    if (!strong && !weak_entropy_reported) {
        console_printf("[ntclks] ASLR active with weak entropy (RDRAND unavailable)\n");
        weak_entropy_reported = true;
    }
}

/**
 * @brief Coordinates the elf64 program headers fit operation.
 * @param eh Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
static bool elf64_program_headers_fit(const struct elf64_ehdr *eh, size_t len)
{
    if (!eh || eh->e_phentsize != sizeof(struct elf64_phdr) || eh->e_phoff > len) {
        return false;
    }
    return eh->e_phnum <= (len - eh->e_phoff) / eh->e_phentsize;
}

/**
 * @brief Coordinates the elf64 phdr at operation.
 * @param eh Input or output value used by this operation.
 * @param image Input or output value used by this operation.
 * @param index Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static const struct elf64_phdr *elf64_phdr_at(const struct elf64_ehdr *eh,
                                               const void *image, uint16_t index)
{
    return (const struct elf64_phdr *)((const uint8_t *)image + eh->e_phoff +
                                       (uint64_t)index * eh->e_phentsize);
}

/**
 * @brief Coordinates the elf64 note abi operation.
 * @param eh Input or output value used by this operation.
 * @param image Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @param out_major Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
static bool elf64_note_abi(const struct elf64_ehdr *eh, const void *image, size_t len,
                           uint32_t *out_major)
{
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph = elf64_phdr_at(eh, image, i);
        uint64_t cursor;
        uint64_t end;
        if (ph->p_type != PT_NOTE || ph->p_offset > len || ph->p_filesz > len - ph->p_offset) {
            continue;
        }
        cursor = ph->p_offset;
        end = ph->p_offset + ph->p_filesz;
        while (cursor + sizeof(struct elf64_nhdr) <= end) {
            const struct elf64_nhdr *note =
                (const struct elf64_nhdr *)((const uint8_t *)image + cursor);
            uint64_t names = cursor + sizeof(*note);
            uint64_t desc = align4_up(names + note->n_namesz);
            uint64_t next = align4_up(desc + note->n_descsz);
            if (!desc || !next || desc > end || next > end) {
                return false;
            }
            if (note->n_type == LEONOS_ELF_NOTE_TYPE &&
                note->n_namesz == sizeof(LEONOS_ELF_NOTE_NAME) &&
                note->n_descsz >= sizeof(struct leonos_elf_abi_note)) {
                const char *name = (const char *)image + names;
                const struct leonos_elf_abi_note *abi =
                    (const struct leonos_elf_abi_note *)((const uint8_t *)image + desc);
                bool match = true;
                for (uint32_t n = 0; n < sizeof(LEONOS_ELF_NOTE_NAME); ++n) {
                    if (name[n] != LEONOS_ELF_NOTE_NAME[n]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    *out_major = abi->major;
                    return abi->major == LEONOS_ELF_ABI_MAJOR;
                }
            }
            cursor = next;
        }
    }
    return false;
}

/**
 * @brief Coordinates the elf64 interp operation.
 * @param eh Input or output value used by this operation.
 * @param image Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
static bool elf64_interp(const struct elf64_ehdr *eh, const void *image, size_t len,
                         char out[64])
{
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph = elf64_phdr_at(eh, image, i);
        if (ph->p_type != PT_INTERP || !ph->p_filesz || ph->p_filesz >= 64 ||
            ph->p_offset > len || ph->p_filesz > len - ph->p_offset) {
            continue;
        }
        const char *src = (const char *)image + ph->p_offset;
        uint64_t j = 0;
        while (j < ph->p_filesz && src[j]) {
            out[j] = src[j];
            ++j;
        }
        if (j == ph->p_filesz) {
            return false;
        }
        out[j] = 0;
        return true;
    }
    return false;
}

/**
 * @brief Coordinates the elf64 validate dynamic header operation.
 * @param eh Input or output value used by this operation.
 * @param image Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @param is_interpreter Input or output value used by this operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
static bool elf64_validate_dynamic_header(const struct elf64_ehdr *eh, const void *image,
                                          size_t len, bool is_interpreter,
                                          struct elf_image_info *out)
{
    bool dynamic = false;
    bool phdr = false;
    bool interp = false;
    uint32_t abi_major = 0;
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph = elf64_phdr_at(eh, image, i);
        if (ph->p_type == PT_DYNAMIC) {
            dynamic = true;
        } else if (ph->p_type == PT_PHDR) {
            phdr = true;
            out->phdr_vaddr = ph->p_vaddr;
        } else if (ph->p_type == PT_INTERP) {
            interp = true;
        }
    }
    if ((!is_interpreter && !dynamic) || !phdr || !elf64_note_abi(eh, image, len, &abi_major)) {
        return false;
    }
    if (!is_interpreter) {
        if (!interp || !elf64_interp(eh, image, len, out->interp)) {
            return false;
        }
        if (!out->interp[0]) {
            return false;
        }
        for (uint32_t i = 0; LEONOS_ELF_INTERP_PATH[i] || out->interp[i]; ++i) {
            if (LEONOS_ELF_INTERP_PATH[i] != out->interp[i]) {
                return false;
            }
        }
    } else if (interp) {
        return false;
    }
    out->abi_major = abi_major;
    return true;
}

/* header_len is the amount available in image. file_len is the full backing
 * file size, which is larger when the kernel lazily maps an ELF from FAT32. */
static bool elf64_probe_image(const void *image, size_t header_len, uint64_t file_len,
                              bool is_interpreter, struct elf_image_info *out)
{
    const struct elf64_ehdr *eh;
    uint32_t loads = 0;
    if (!image || header_len < sizeof(struct elf64_ehdr) || !out) {
        return false;
    }
    *out = (struct elf_image_info){0};
    eh = (const struct elf64_ehdr *)image;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' ||
        eh->e_ident[3] != 'F' || eh->e_ident[4] != 2 || eh->e_ident[5] != 1 ||
        eh->e_ident[6] != 1 || (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) ||
        eh->e_machine != EM_X86_64 || eh->e_version != 1 ||
        eh->e_ehsize != sizeof(*eh) || !elf64_program_headers_fit(eh, header_len)) {
        return false;
    }
    out->dynamic = eh->e_type == ET_DYN;
    if (is_interpreter && eh->e_type != ET_DYN) {
        return false;
    }
    out->entry = eh->e_entry;
    out->machine = eh->e_machine;
    out->phnum = eh->e_phnum;
    out->low_vaddr = UINT64_MAX;
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph = elf64_phdr_at(eh, image, i);
        uint64_t end;
        if (ph->p_type != PT_LOAD || !ph->p_memsz) {
            continue;
        }
        if (ph->p_filesz > ph->p_memsz || ph->p_memsz > UINT64_MAX - ph->p_vaddr ||
            ph->p_offset > file_len || ph->p_filesz > file_len - ph->p_offset ||
            (ph->p_flags & PF_W && ph->p_flags & PF_X)) {
            return false;
        }
        end = ph->p_vaddr + ph->p_memsz;
        if (ph->p_vaddr < out->low_vaddr) {
            out->low_vaddr = ph->p_vaddr;
        }
        if (end > out->high_vaddr) {
            out->high_vaddr = end;
        }
        ++loads;
    }
    if (!loads || out->low_vaddr == UINT64_MAX) {
        return false;
    }
    if (out->dynamic &&
        !elf64_validate_dynamic_header(eh, image, header_len, is_interpreter, out)) {
        return false;
    }
    out->valid = true;
    return true;
}

/**
 * @brief Coordinates the elf64 probe operation.
 * @param image Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
bool elf64_probe(const void *image, size_t len, struct elf_image_info *out)
{
    return elf64_probe_image(image, len, len, false, out);
}

/**
 * @brief Coordinates the elf64 segment valid operation.
 * @param ph Input or output value used by this operation.
 * @param file_len Length, size, or element count associated with the operation.
 * @param bias Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static bool elf64_segment_valid(const struct elf64_phdr *ph, uint64_t file_len,
                                uint64_t bias)
{
    uint64_t start;
    if (!ph || ph->p_filesz > ph->p_memsz || ph->p_offset > file_len ||
        ph->p_filesz > file_len - ph->p_offset || ph->p_memsz > UINT64_MAX - ph->p_vaddr ||
        ph->p_vaddr > UINT64_MAX - bias) {
        return false;
    }
    start = bias + ph->p_vaddr;
    return start >= NTCLKS_USER_BASE && ph->p_memsz <= NTCLKS_USER_TOP - start;
}

/**
 * @brief Coordinates the ensure segment pages operation.
 * @param as Input or output value used by this operation.
 * @param start Input or output value used by this operation.
 * @param end Input or output value used by this operation.
 * @param flags Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static bool ensure_segment_pages(struct address_space *as, uint64_t start,
                                 uint64_t end, uint64_t flags)
{
    for (uint64_t page = align_down(start); page < align_up(end); page += PAGE_SIZE) {
        uint64_t phys;
        if (address_space_user_page_phys(as, page)) {
            continue;
        }
        phys = mm_alloc_page();
        if (!phys || !address_space_map_user_page(as, page, phys, flags)) {
            if (phys) {
                mm_free_page(phys);
            }
            return false;
        }
    }
    return true;
}

/**
 * @brief Copies to address space.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @param src Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
static bool copy_to_address_space(struct address_space *as, uint64_t vaddr,
                                  const uint8_t *src, uint64_t len)
{
    uint64_t copied = 0;
    while (copied < len) {
        uint64_t address = vaddr + copied;
        uint64_t phys = address_space_user_page_phys(as, address);
        uint64_t page_left = PAGE_SIZE - (address & (PAGE_SIZE - 1ULL));
        uint64_t take = len - copied < page_left ? len - copied : page_left;
        uint8_t *dst;
        if (!phys) {
            return false;
        }
        dst = (uint8_t *)(uintptr_t)(phys + (address & (PAGE_SIZE - 1ULL)));
        for (uint64_t i = 0; i < take; ++i) {
            dst[i] = src[copied + i];
        }
        copied += take;
    }
    return true;
}

/**
 * @brief Coordinates the elf64 load address space operation.
 * @param as Input or output value used by this operation.
 * @param image Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
bool elf64_load_address_space(struct address_space *as, const void *image, size_t len,
                              struct elf_image_info *out)
{
    const struct elf64_ehdr *eh;
    if (!as || !elf64_probe(image, len, out) || out->dynamic) {
        return false;
    }
    eh = (const struct elf64_ehdr *)image;
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph = elf64_phdr_at(eh, image, i);
        uint64_t flags = (ph->p_flags & PF_W) ? NTCLKS_PAGE_WRITABLE : 0;
        if (ph->p_type != PT_LOAD || !ph->p_memsz) {
            continue;
        }
        if (!elf64_segment_valid(ph, len, 0)) {
            out->valid = false;
            return false;
        }
        if (!(ph->p_flags & PF_X)) {
            flags |= NTCLKS_PAGE_NOEXEC;
        }
        if (!ensure_segment_pages(as, ph->p_vaddr, ph->p_vaddr + ph->p_memsz, flags) ||
            !copy_to_address_space(as, ph->p_vaddr,
                                   (const uint8_t *)image + ph->p_offset, ph->p_filesz)) {
            out->valid = false;
            return false;
        }
    }
    return true;
}

/**
 * @brief Coordinates the elf64 read headers operation.
 * @param node Input or output value used by this operation.
 * @param out_image Caller-provided storage that receives output from this operation.
 * @param out_len Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
static bool elf64_read_headers(const struct storage_node *node, const void **out_image,
                               size_t *out_len)
{
    const struct elf64_ehdr *eh;
    uint64_t header_len;
    uint32_t wanted;
    uint32_t got = 0;
    if (!node || node->type != LEONOS_FS_TYPE_FILE || node->size < sizeof(struct elf64_ehdr) ||
        !out_image || !out_len) {
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
    wanted = (uint32_t)(node->size < ELF_HEADER_READ_BYTES ? node->size : ELF_HEADER_READ_BYTES);
    if (header_len > wanted || header_len < sizeof(*eh)) {
        return false;
    }
    got = 0;
    if (storage_read_node(node, 0, elf_header_scratch, wanted, &got) < 0 || got != wanted) {
        return false;
    }
    *out_image = elf_header_scratch;
    *out_len = wanted;
    return true;
}

/**
 * @brief Coordinates the elf64 task range available operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param start Input or output value used by this operation.
 * @param end Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static bool elf64_task_range_available(const struct task *task, uint64_t start, uint64_t end)
{
    uint64_t stack_low;
    if (!task || start < NTCLKS_USER_BASE || start >= end || end > NTCLKS_USER_TOP ||
        task->stack_top < (uint64_t)NTCLKS_USER_STACK_PAGES * PAGE_SIZE) {
        console_printf("[ntclks] ELF range invalid start=0x%llx end=0x%llx stack=0x%llx\n",
                       (unsigned long long)start, (unsigned long long)end,
                       task ? (unsigned long long)task->stack_top : 0ULL);
        return false;
    }
    stack_low = task->stack_top - (uint64_t)NTCLKS_USER_STACK_PAGES * PAGE_SIZE;
    if (end > stack_low) {
        console_printf("[ntclks] ELF range overlaps stack start=0x%llx end=0x%llx stack=0x%llx\n",
                       (unsigned long long)start, (unsigned long long)end,
                       (unsigned long long)stack_low);
        return false;
    }
    for (uint32_t i = 0; i < SCHED_TASK_VMA_MAX; ++i) {
        const struct task_vma *vma = &task->vmas[i];
        if (vma->used && start < vma->end && end > vma->start) {
            console_printf("[ntclks] ELF range overlaps VMA=%u range=0x%llx-0x%llx existing=0x%llx-0x%llx\n",
                           i, (unsigned long long)start, (unsigned long long)end,
                           (unsigned long long)vma->start, (unsigned long long)vma->end);
            return false;
        }
    }
    return true;
}

/**
 * @brief Coordinates the elf64 task free vma operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
 */
static struct task_vma *elf64_task_free_vma(struct task *task)
{
    for (uint32_t i = 0; task && i < SCHED_TASK_VMA_MAX; ++i) {
        if (!task->vmas[i].used) {
            return &task->vmas[i];
        }
    }
    return NULL;
}

/**
 * @brief Coordinates the elf64 segments nonoverlapping operation.
 * @param eh Input or output value used by this operation.
 * @param image Input or output value used by this operation.
 * @param file_len Length, size, or element count associated with the operation.
 * @param bias Input or output value used by this operation.
 * @param strict_page_layout Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static bool elf64_segments_nonoverlapping(const struct elf64_ehdr *eh, const void *image,
                                          uint64_t file_len, uint64_t bias,
                                          bool strict_page_layout)
{
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *left = elf64_phdr_at(eh, image, i);
        uint64_t left_start;
        uint64_t left_end;
        if (left->p_type != PT_LOAD || !left->p_memsz) {
            continue;
        }
        if (!elf64_segment_valid(left, file_len, bias) ||
            left->p_offset % PAGE_SIZE != left->p_vaddr % PAGE_SIZE) {
            return false;
        }
        left_start = align_down(bias + left->p_vaddr);
        left_end = align_up(bias + left->p_vaddr + left->p_memsz);
        if (!left_end) {
            return false;
        }
        if (!strict_page_layout) {
            continue;
        }
        for (uint16_t j = 0; j < i; ++j) {
            const struct elf64_phdr *right = elf64_phdr_at(eh, image, j);
            uint64_t right_start;
            uint64_t right_end;
            if (right->p_type != PT_LOAD || !right->p_memsz) {
                continue;
            }
            right_start = align_down(bias + right->p_vaddr);
            right_end = align_up(bias + right->p_vaddr + right->p_memsz);
            if (left_start < right_end && left_end > right_start) {
                return false;
            }
        }
    }
    return true;
}

/* Older ET_EXEC images can place a tiny writable GOT at the tail of a
 * read-only file page.  They remain supported by recording one VMA for that
 * common file page, with the union of their page permissions.  PIE images
 * must keep page-granular LOAD ranges disjoint so their protections are
 * exact from the first fault onward. */
static struct task_vma *elf64_legacy_shared_page_vma(struct task *task,
                                                      const struct storage_node *node,
                                                      uint64_t start, uint64_t end,
                                                      uint64_t file_offset)
{
    for (uint32_t i = 0; task && i < SCHED_TASK_VMA_MAX; ++i) {
        struct task_vma *vma = &task->vmas[i];
        uint64_t expected_offset;
        if (!vma->used || end <= vma->start || start >= vma->end) {
            continue;
        }
        if (!(vma->flags & TASK_VMA_FLAG_FILE) || start < vma->start ||
            vma->file_node.type != node->type || vma->file_node.drive != node->drive ||
            vma->file_node.first_cluster != node->first_cluster ||
            start - vma->start > UINT64_MAX - vma->file_offset) {
            return NULL;
        }
        expected_offset = vma->file_offset + start - vma->start;
        return expected_offset == file_offset ? vma : NULL;
    }
    return NULL;
}

/**
 * @brief Coordinates the elf64 choose bias operation.
 * @param info Input or output value used by this operation.
 * @param interpreter Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint64_t elf64_choose_bias(const struct elf_image_info *info, bool interpreter)
{
    uint64_t min = interpreter ? ELF_DYN_INTERP_MIN : ELF_DYN_MAIN_MIN;
    uint64_t max = interpreter ? ELF_DYN_INTERP_MAX : ELF_DYN_MAIN_MAX;
    uint64_t span;
    uint64_t base;
    if (!info || info->high_vaddr <= info->low_vaddr ||
        info->high_vaddr - info->low_vaddr > max - min) {
        return 0;
    }
    span = (max - min - (info->high_vaddr - info->low_vaddr)) & ~0x1fffffULL;
    base = min + (elf64_random_u64(NULL) % (span / 0x200000ULL + 1ULL)) * 0x200000ULL;
    if (base < align_down(info->low_vaddr)) {
        return 0;
    }
    return base - align_down(info->low_vaddr);
}

/**
 * @brief Coordinates the elf64 map one operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param node Input or output value used by this operation.
 * @param image Input or output value used by this operation.
 * @param info Input or output value used by this operation.
 * @param bias Input or output value used by this operation.
 * @param image_name Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static bool elf64_map_one(struct task *task, const struct storage_node *node,
                          const void *image, const struct elf_image_info *info,
                          uint64_t bias, const char *image_name)
{
    const struct elf64_ehdr *eh = image;
    uint32_t loads = 0;
    uint32_t free_vmas = 0;
    if (!elf64_segments_nonoverlapping(eh, image, node->size, bias, info->dynamic)) {
        console_printf("[ntclks] ELF %s segment layout rejected bias=0x%llx\n",
                       image_name, (unsigned long long)bias);
        return false;
    }
    for (uint32_t i = 0; i < SCHED_TASK_VMA_MAX; ++i) {
        if (!task->vmas[i].used) {
            ++free_vmas;
        }
    }
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph = elf64_phdr_at(eh, image, i);
        if (ph->p_type == PT_LOAD && ph->p_memsz) {
            ++loads;
        }
    }
    if (!loads || loads > free_vmas) {
        console_printf("[ntclks] ELF %s VMA capacity rejected loads=%u free=%u\n",
                       image_name, loads, free_vmas);
        return false;
    }
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const struct elf64_phdr *ph = elf64_phdr_at(eh, image, i);
        struct task_vma *vma;
        struct storage_node segment_node;
        uint64_t start;
        uint64_t end;
        uint64_t file_offset;
        uint64_t page_delta;
        struct task_vma *legacy_vma = NULL;
        uint32_t prot = TASK_VMA_PROT_READ;
        if (ph->p_type != PT_LOAD || !ph->p_memsz) {
            continue;
        }
        start = align_down(bias + ph->p_vaddr);
        end = align_up(bias + ph->p_vaddr + ph->p_memsz);
        if (!end) {
            console_printf("[ntclks] ELF %s LOAD[%u] address overflow\n", image_name, i);
            return false;
        }
        page_delta = ph->p_vaddr - align_down(ph->p_vaddr);
        if (ph->p_offset < page_delta) {
            console_printf("[ntclks] ELF %s LOAD[%u] file offset rejected\n", image_name, i);
            return false;
        }
        file_offset = ph->p_offset - page_delta;
        if (!info->dynamic) {
            legacy_vma = elf64_legacy_shared_page_vma(task, node, start, end, file_offset);
        }
        if (!legacy_vma && !elf64_task_range_available(task, start, end)) {
            console_printf("[ntclks] ELF %s LOAD[%u] range rejected 0x%llx-0x%llx\n",
                           image_name, i, (unsigned long long)start,
                           (unsigned long long)end);
            return false;
        }
        if (!address_space_prepare_user_range(&task->as, start, end)) {
            console_printf("[ntclks] ELF %s LOAD[%u] page table preparation failed "
                           "0x%llx-0x%llx\n",
                           image_name, i, (unsigned long long)start,
                           (unsigned long long)end);
            return false;
        }
        vma = legacy_vma ? legacy_vma : elf64_task_free_vma(task);
        if (!vma) {
            console_printf("[ntclks] ELF %s LOAD[%u] VMA capacity rejected\n", image_name, i);
            return false;
        }
        if (ph->p_flags & PF_W) {
            prot |= TASK_VMA_PROT_WRITE;
        }
        if (ph->p_flags & PF_X) {
            prot |= TASK_VMA_PROT_EXEC;
        }
        if (legacy_vma) {
            if ((legacy_vma->prot | prot) & TASK_VMA_PROT_WRITE &&
                (legacy_vma->prot | prot) & TASK_VMA_PROT_EXEC) {
                console_printf("[ntclks] ELF %s LOAD[%u] legacy W+X page rejected\n",
                               image_name, i);
                return false;
            }
            legacy_vma->prot |= prot;
            legacy_vma->max_prot |= prot;
            legacy_vma->flags = TASK_VMA_FLAG_FILE | TASK_VMA_FLAG_LAZY |
                                ((legacy_vma->prot & TASK_VMA_PROT_WRITE) ?
                                 TASK_VMA_FLAG_PRIVATE : TASK_VMA_FLAG_SHARED_FILE);
            if (end > legacy_vma->end) {
                legacy_vma->end = end;
            }
            if (ph->p_offset + ph->p_filesz > legacy_vma->file_limit) {
                legacy_vma->file_limit = ph->p_offset + ph->p_filesz;
                legacy_vma->file_node.size = legacy_vma->file_limit;
            }
            continue;
        }
        segment_node = *node;
        segment_node.size = ph->p_offset + ph->p_filesz;
        *vma = (struct task_vma){
            .used = 1,
            .prot = prot,
            .max_prot = prot,
            .flags = TASK_VMA_FLAG_FILE | TASK_VMA_FLAG_LAZY |
                     ((ph->p_flags & PF_W) ? TASK_VMA_FLAG_PRIVATE : TASK_VMA_FLAG_SHARED_FILE),
            .start = start,
            .end = end,
            .file_offset = file_offset,
            .file_limit = segment_node.size,
            .file_node = segment_node,
        };
    }
    (void)info;
    return true;
}

/**
 * @brief Coordinates the elf64 map task image operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param node Input or output value used by this operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
bool elf64_map_task_image(struct task *task, const struct storage_node *node,
                          struct elf_image_info *out)
{
    const void *image;
    const void *interp_image;
    size_t image_len;
    size_t interp_len;
    struct elf_image_info main_info;
    struct elf_image_info interp_info;
    struct storage_node interp_node;
    uint64_t main_bias = 0;
    uint64_t interp_bias;
    const struct elf64_ehdr *main_eh;
    const struct elf64_ehdr *interp_eh;
    bool entry_found = false;

    if (!task || !node || !out) {
        console_printf("[ntclks] ELF task image request is invalid\n");
        return false;
    }
    if (!elf64_read_headers(node, &image, &image_len)) {
        console_printf("[ntclks] ELF main header read failed\n");
        return false;
    }
    if (!elf64_probe_image(image, image_len, node->size, false, &main_info)) {
        console_printf("[ntclks] ELF main header validation failed\n");
        return false;
    }
    main_eh = image;
    if (main_info.dynamic) {
        main_bias = elf64_choose_bias(&main_info, false);
        if (!main_bias || main_info.entry > UINT64_MAX - main_bias ||
            main_info.phdr_vaddr > UINT64_MAX - main_bias) {
            console_printf("[ntclks] ELF main ASLR layout rejected span=0x%llx\n",
                           (unsigned long long)(main_info.high_vaddr - main_info.low_vaddr));
            return false;
        }
    }
    for (uint16_t i = 0; i < main_eh->e_phnum; ++i) {
        const struct elf64_phdr *ph = elf64_phdr_at(main_eh, image, i);
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X) &&
            main_info.entry >= ph->p_vaddr && main_info.entry - ph->p_vaddr < ph->p_memsz) {
            entry_found = true;
        }
    }
    if (!entry_found) {
        console_printf("[ntclks] ELF main entry is outside an executable LOAD segment\n");
        return false;
    }
    if (!elf64_map_one(task, node, image, &main_info, main_bias, "main")) {
        return false;
    }
    main_info.load_bias = main_bias;
    main_info.entry += main_bias;
    main_info.phdr_vaddr += main_bias;
    if (!main_info.dynamic) {
        *out = main_info;
        return true;
    }

    if (storage_lookup_path(LEONOS_ELF_INTERP_PATH, &interp_node) < 0) {
        console_printf("[ntclks] ELF interpreter lookup failed path=%s\n", LEONOS_ELF_INTERP_PATH);
        return false;
    }
    if (!elf64_read_headers(&interp_node, &interp_image, &interp_len)) {
        console_printf("[ntclks] ELF interpreter header read failed\n");
        return false;
    }
    if (!elf64_probe_image(interp_image, interp_len, interp_node.size, true, &interp_info)) {
        console_printf("[ntclks] ELF interpreter header validation failed\n");
        return false;
    }
    if (interp_info.abi_major != main_info.abi_major) {
        console_printf("[ntclks] ELF ABI mismatch main=%u interpreter=%u\n",
                       main_info.abi_major, interp_info.abi_major);
        return false;
    }
    interp_bias = elf64_choose_bias(&interp_info, true);
    interp_eh = interp_image;
    if (!interp_bias || interp_info.entry > UINT64_MAX - interp_bias) {
        console_printf("[ntclks] ELF interpreter ASLR layout rejected span=0x%llx\n",
                       (unsigned long long)(interp_info.high_vaddr - interp_info.low_vaddr));
        return false;
    }
    if (!elf64_map_one(task, &interp_node, interp_image, &interp_info, interp_bias,
                        "interpreter")) {
        return false;
    }
    (void)interp_eh;
    task->dynamic_launch.main_base = main_info.load_bias;
    task->dynamic_launch.main_entry = main_info.entry;
    task->dynamic_launch.main_phdr = main_info.phdr_vaddr;
    task->dynamic_launch.interp_base = interp_bias;
    task->dynamic_launch.interp_entry = interp_info.entry + interp_bias;
    task->dynamic_launch.abi_major = main_info.abi_major;
    elf64_fill_random(task->dynamic_launch.random);
    for (uint32_t i = 0; i + 1 < sizeof(task->dynamic_launch.main_path) && task->path[i]; ++i) {
        task->dynamic_launch.main_path[i] = task->path[i];
        task->dynamic_launch.main_path[i + 1] = 0;
    }
    main_info.interpreter_entry = interp_info.entry + interp_bias;
    *out = main_info;
    return true;
}
