/*
 * LeonOS kernel-debug controller.
 * Owns the persistent flag, validates the staged ET_REL payload, and runs the
 * built-in diagnostic menu before normal Ring-3 startup.
 */
#include <leonos/kernel_debug.h>
#include <leonos/audio.h>
#include <leonos/auth.h>
#include <leonos/device.h>
#include <leonos/driver.h>
#include <leonos/fs.h>
#include <leonos/inputm.h>
#include <leonos/net.h>
#include <leonos/pty.h>
#include <leonos/startup.h>
#include <leonos/system.h>
#include <leonos/text.h>
#include <ntclks/console.h>
#include <ntclks/heap.h>
#include <ntclks/kernel_debug.h>
#include <ntclks/mm.h>
#include <ntclks/ostui.h>
#include <ntclks/power.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/time.h>
#include <ntclks/gui_ipc.h>

#define KERNEL_DEBUG_ENABLED_PATH "/system/state/kerneldebug.enabled"
#define KERNEL_DEBUG_MODULE_PATH "/system/kerneldebug.sys"
#define KERNEL_DEBUG_MARKER "LEONOS-KDBG-1\n"
#define KERNEL_DEBUG_BENCH_ITERATIONS 1000U
#define KERNEL_DEBUG_GUI_IOCTL_VERSION 0x4c475549ULL
#define KERNEL_DEBUG_GUI_IOCTL_EVENT 0x4c455654ULL
#define KERNEL_DEBUG_GUI_IOCTL_UPTIME_MS 0x4c555054ULL
#define KERNEL_DEBUG_GUI_IOCTL_FB_INFO 0x4c464249ULL
#define KERNEL_DEBUG_GUI_IOCTL_FB_FILL 0x4c464246ULL
#define KERNEL_DEBUG_GUI_IOCTL_FB_RECT 0x4c464252ULL
#define KERNEL_DEBUG_GUI_IOCTL_FB_TEXT 0x4c464254ULL
#define KERNEL_DEBUG_GUI_IOCTL_FB_PIXEL 0x4c464250ULL
#define KERNEL_DEBUG_GUI_IOCTL_FB_BLIT 0x4c46424cULL
#define KERNEL_DEBUG_GUI_IOCTL_FB_SET_MODE 0x4c46424dULL
#define KERNEL_DEBUG_GUI_IOCTL_FB_CAPS 0x4c464243ULL
#define KERNEL_DEBUG_GUI_IOCTL_CREATE_WINDOW 0x4c475743ULL
#define KERNEL_DEBUG_GUI_IOCTL_POLL_WINDOW 0x4c475750ULL
#define KERNEL_DEBUG_GUI_IOCTL_PRESENT_WINDOW 0x4c475046ULL
#define KERNEL_DEBUG_GUI_IOCTL_FETCH_WINDOW 0x4c475746ULL
#define KERNEL_DEBUG_GUI_IOCTL_WINDOW_EVENT 0x4c475745ULL
#define KERNEL_DEBUG_GUI_IOCTL_WAIT_WINDOW_EVENT 0x4c475457ULL
#define KERNEL_DEBUG_GUI_IOCTL_SEND_WINDOW_EVENT 0x4c475753ULL
#define KERNEL_DEBUG_GUI_IOCTL_DESTROY_WINDOW 0x4c475744ULL
#define KERNEL_DEBUG_GUI_IOCTL_TASKS 0x4c54534bULL
#define KERNEL_DEBUG_GUI_IOCTL_TASK_KILL 0x4c544b49ULL
#define KERNEL_DEBUG_GUI_IOCTL_REBOOT 0x4c524254ULL
#define KERNEL_DEBUG_GUI_IOCTL_SHUTDOWN 0x4c534844ULL
#define KERNEL_DEBUG_GUI_IOCTL_DISPLAY_STATE 0x4c445350ULL
#define KERNEL_DEBUG_GUI_IOCTL_DISPLAY_REQUEST 0x4c445351ULL
#define KERNEL_DEBUG_GUI_IOCTL_POLL_DISPLAY_REQUEST 0x4c445352ULL
#define KERNEL_DEBUG_GUI_IOCTL_PUBLISH_DISPLAY_STATE 0x4c445353ULL

#define EI_NIDENT 16U
#define ET_REL 1U
#define EM_X86_64 62U
#define SHT_NULL 0U
#define SHT_PROGBITS 1U
#define SHT_SYMTAB 2U
#define SHT_STRTAB 3U
#define SHT_RELA 4U
#define SHT_NOTE 7U
#define SHT_NOBITS 8U
#define SHF_WRITE 0x1ULL
#define SHF_ALLOC 0x2ULL
#define SHF_EXECINSTR 0x4ULL
#define SHF_TLS 0x400ULL
#define SHN_UNDEF 0U
#define SHN_ABS 0xfff1U
#define STT_NOTYPE 0U
#define STT_OBJECT 1U
#define STT_FUNC 2U
#define STT_SECTION 3U
#define STT_TLS 6U
#define STT_GNU_IFUNC 10U
#define R_X86_64_NONE 0U
#define R_X86_64_64 1U
#define R_X86_64_PC32 2U
#define R_X86_64_PLT32 4U
#define R_X86_64_32 10U
#define R_X86_64_32S 11U

struct debug_elf_header {
    uint8_t ident[EI_NIDENT];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct debug_elf_section {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t address;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t align;
    uint64_t entsize;
};

struct debug_elf_symbol {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
};

struct debug_elf_rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
};

struct debug_elf_note_header {
    uint32_t name_size;
    uint32_t desc_size;
    uint32_t type;
};

struct debug_kernel_note {
    uint32_t abi;
    uint32_t entry_name_hash;
};

static uint64_t debug_align_up(uint64_t value, uint64_t align)
{
    if (align < 16U) align = 16U;
    if (align & (align - 1U)) align = 16U;
    if (value > UINT64_MAX - (align - 1U)) return 0;
    return (value + align - 1U) & ~(align - 1U);
}

static int debug_range_ok(uint64_t offset, uint64_t size, uint64_t length)
{
    return offset <= length && size <= length - offset;
}

static int debug_ranges_overlap(uint64_t left, uint64_t left_size,
                                uint64_t right, uint64_t right_size)
{
    if (!left_size || !right_size) return 0;
    return left <= right ? right - left < left_size : left - right < right_size;
}

static void debug_copy(void *dst, const void *src, size_t len)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    for (size_t i = 0; i < len; ++i) out[i] = in[i];
}

static void debug_zero(void *dst, size_t len)
{
    uint8_t *out = (uint8_t *)dst;
    for (size_t i = 0; i < len; ++i) out[i] = 0;
}

static int debug_text_eq(const char *left, const char *right)
{
    if (!left || !right) return 0;
    while (*left && *right && *left == *right) ++left, ++right;
    return *left == 0 && *right == 0;
}

static int debug_text_starts(const char *text, const char *prefix)
{
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

static int debug_string_terminated(const uint8_t *table, uint64_t length, uint32_t offset)
{
    if (!table || offset >= length) return 0;
    for (uint64_t index = offset; index < length; ++index) {
        if (table[index] == 0) return 1;
    }
    return 0;
}

static int debug_allowed_section_name(const char *name, uint32_t type, uint64_t flags)
{
    if (!name) return 0;
    if (type == SHT_NULL) return name[0] == 0;
    if (type == SHT_NOTE) return debug_text_eq(name, ".note.leonos.kerneldebug");
    if (type == SHT_SYMTAB) return debug_text_eq(name, ".symtab");
    if (type == SHT_STRTAB) return debug_text_eq(name, ".strtab") || debug_text_eq(name, ".shstrtab");
    if (type == SHT_RELA) return debug_text_starts(name, ".rela.");
    if (type != SHT_PROGBITS && type != SHT_NOBITS) return 0;
    if (!(flags & SHF_ALLOC)) return 0;
    return debug_text_eq(name, ".text") || debug_text_eq(name, ".rodata") ||
           debug_text_eq(name, ".data") || debug_text_eq(name, ".bss") ||
           debug_text_starts(name, ".text.") || debug_text_starts(name, ".rodata.") ||
           debug_text_starts(name, ".data.") || debug_text_starts(name, ".bss.");
}

static uint32_t debug_name_hash(const char *text)
{
    uint32_t hash = 2166136261U;
    while (text && *text) {
        hash ^= (uint8_t)*text++;
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t debug_align4_u32(uint32_t value)
{
    return (value + 3U) & ~3U;
}

static int debug_validate_note(const uint8_t *image, size_t len,
                               const struct debug_elf_section *section)
{
    uint64_t offset = 0;
    int found = 0;
    if (!section || section->type != SHT_NOTE || !(section->flags & SHF_ALLOC) ||
        section->size > UINT32_MAX || !debug_range_ok(section->offset, section->size, len)) {
        return -22;
    }
    while (offset < section->size) {
        const struct debug_elf_note_header *header;
        uint32_t name_size;
        uint32_t desc_size;
        uint32_t name_padded;
        uint32_t desc_padded;
        const uint8_t *name;
        const uint8_t *desc;
        if (section->size - offset < sizeof(*header)) return -22;
        header = (const struct debug_elf_note_header *)(image + section->offset + offset);
        name_size = header->name_size;
        desc_size = header->desc_size;
        name_padded = debug_align4_u32(name_size);
        desc_padded = debug_align4_u32(desc_size);
        if (name_padded < name_size || desc_padded < desc_size ||
            name_padded > section->size - offset - sizeof(*header) ||
            desc_padded > section->size - offset - sizeof(*header) - name_padded) {
            return -22;
        }
        name = image + section->offset + offset + sizeof(*header);
        desc = name + name_padded;
        if (header->type == LEONOS_KERNEL_DEBUG_NOTE_TYPE && name_size == 9U &&
            name[0] == 'L' && name[1] == 'E' && name[2] == 'O' && name[3] == 'N' &&
            name[4] == 'K' && name[5] == 'D' && name[6] == 'B' && name[7] == 'G' &&
            name[8] == 0 && desc_size == sizeof(struct debug_kernel_note)) {
            const struct debug_kernel_note *note = (const struct debug_kernel_note *)desc;
            if (note->abi != LEONOS_KERNEL_DEBUG_MODULE_ABI ||
                note->entry_name_hash != debug_name_hash("leonos_kernel_debug_module_entry")) {
                return -22;
            }
            found = 1;
        }
        offset += sizeof(*header) + name_padded + desc_padded;
    }
    return found ? 0 : -22;
}

static const struct debug_elf_section *debug_section_at(const struct debug_elf_header *eh,
                                                        const void *image, uint16_t index)
{
    return (const struct debug_elf_section *)((const uint8_t *)image + eh->shoff +
                                              (uint64_t)index * eh->shentsize);
}

static int debug_file_exists(const char *path)
{
    struct leonos_stat st;
    return path && storage_stat_path(path, &st) == 0 && st.type == LEONOS_FS_TYPE_FILE;
}

static int debug_module_validate(const void *image, size_t len)
{
    const struct debug_elf_header *eh = (const struct debug_elf_header *)image;
    if (!eh || len < sizeof(*eh) || eh->ident[0] != 0x7f || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L' || eh->ident[3] != 'F' || eh->ident[4] != 2U ||
        eh->ident[5] != 1U || eh->type != ET_REL || eh->machine != EM_X86_64 ||
        eh->version != 1U || eh->shentsize != sizeof(struct debug_elf_section) ||
        eh->shnum == 0U || eh->shstrndx >= eh->shnum || eh->shoff > len ||
        (uint64_t)eh->shnum * eh->shentsize > len - eh->shoff) {
        return -22;
    }
    {
        const struct debug_elf_section *names = debug_section_at(eh, image, eh->shstrndx);
        int found_note = 0;
        if (names->type != SHT_STRTAB || !debug_range_ok(names->offset, names->size, len)) return -22;
        for (uint16_t index = 0; index < eh->shnum; ++index) {
            const struct debug_elf_section *section = debug_section_at(eh, image, index);
            const uint8_t *name_table = (const uint8_t *)image + names->offset;
            if (section->name >= names->size || !debug_string_terminated(name_table, names->size,
                                                                          section->name)) return -22;
            if (section->align && (section->align & (section->align - 1U))) return -22;
            if (section->align > 4096U) return -22;
            if (section->align > 1U && section->type != SHT_NOBITS &&
                section->offset % section->align != 0U) return -22;
            if (section->type != SHT_NOBITS && !debug_range_ok(section->offset, section->size, len)) return -22;
            if (section->type != SHT_NULL && section->type != SHT_PROGBITS &&
                section->type != SHT_SYMTAB && section->type != SHT_STRTAB &&
                section->type != SHT_RELA && section->type != SHT_NOTE && section->type != SHT_NOBITS) return -95;
            if (section->flags & SHF_TLS) return -95;
            if ((section->flags & SHF_ALLOC) && (section->flags & SHF_WRITE) &&
                (section->flags & SHF_EXECINSTR)) return -22;
            {
                const char *section_name = (const char *)name_table + section->name;
                if (!debug_allowed_section_name(section_name, section->type, section->flags)) return -95;
                if (section->flags & SHF_ALLOC) {
                    if (section->type == SHT_NOTE) {
                        if (debug_validate_note((const uint8_t *)image, len, section) < 0) return -22;
                        found_note = 1;
                    } else if (section->type != SHT_PROGBITS && section->type != SHT_NOBITS) {
                        return -95;
                    }
                }
            }
            if (section->type == SHT_SYMTAB || section->type == SHT_RELA) {
                if (section->entsize == 0U || section->size % section->entsize != 0U) return -22;
            }
            if (section->type != SHT_NOBITS && section->size) {
                for (uint16_t previous = 0; previous < index; ++previous) {
                    const struct debug_elf_section *other = debug_section_at(eh, image, previous);
                    if (other->type != SHT_NOBITS &&
                        debug_ranges_overlap(section->offset, section->size,
                                             other->offset, other->size)) return -22;
                }
            }
        }
        if (!found_note) return -22;
    }
    return 0;
}

static int debug_apply_relocation(uint8_t *place, uint32_t type, uint64_t symbol,
                                  int64_t addend, uint64_t place_addr)
{
    uint64_t value = symbol + (uint64_t)addend;
    int64_t relative = (int64_t)value - (int64_t)place_addr;
    if (type == R_X86_64_NONE) return 0;
    if (type == R_X86_64_64) {
        *(uint64_t *)(void *)place = value;
        return 0;
    }
    if (type == R_X86_64_PC32 || type == R_X86_64_PLT32) {
        if (relative < INT32_MIN || relative > INT32_MAX) return -22;
        *(int32_t *)(void *)place = (int32_t)relative;
        return 0;
    }
    if (type == R_X86_64_32) {
        if (value > UINT32_MAX) return -22;
        *(uint32_t *)(void *)place = (uint32_t)value;
        return 0;
    }
    if (type == R_X86_64_32S) {
        if ((int64_t)value < INT32_MIN || (int64_t)value > INT32_MAX) return -22;
        *(int32_t *)(void *)place = (int32_t)value;
        return 0;
    }
    return -95;
}

static int debug_load_module(const void *image, size_t len,
                             const struct leonos_kernel_debug_api *api)
{
    const struct debug_elf_header *eh;
    uint64_t section_addr[64] = {0};
    /* Keep zero as the unallocated-section sentinel. */
    uint64_t total = 16U;
    uint64_t allocation;
    uint32_t pages;
    leonos_kernel_debug_entry_fn entry = 0;
    if (!image || len < sizeof(struct debug_elf_header)) return -22;
    eh = (const struct debug_elf_header *)image;
    if (eh->shnum > 64U) return -22;
    int ret = debug_module_validate(image, len);
    if (ret < 0) return ret;

    for (uint16_t index = 0; index < eh->shnum; ++index) {
        const struct debug_elf_section *section = debug_section_at(eh, image, index);
        if ((section->flags & SHF_ALLOC) == 0U) continue;
        if ((section->flags & SHF_WRITE) && (section->flags & SHF_EXECINSTR)) return -22;
        total = debug_align_up(total, section->align);
        if (!total) return -12;
        if (section->size > UINT64_MAX - total) return -12;
        section_addr[index] = total;
        total += section->size;
    }
    if (total == 0U || total > 16U * 1024U * 1024U) return -12;
    pages = (uint32_t)((total + 4095U) / 4096U);
    allocation = mm_alloc_pages(pages);
    if (!allocation) return -12;
    for (uint16_t index = 0; index < eh->shnum; ++index) {
        const struct debug_elf_section *section = debug_section_at(eh, image, index);
        if (!section_addr[index]) continue;
        section_addr[index] += allocation;
        if (section->type == SHT_NOBITS) debug_zero((void *)(uintptr_t)section_addr[index], section->size);
        else debug_copy((void *)(uintptr_t)section_addr[index], (const uint8_t *)image + section->offset, section->size);
    }
    for (uint16_t index = 0; index < eh->shnum; ++index) {
        const struct debug_elf_section *rela_section = debug_section_at(eh, image, index);
        const struct debug_elf_section *symbols;
        const struct debug_elf_section *strings;
        if (rela_section->type != SHT_RELA) continue;
        if (rela_section->info >= eh->shnum || rela_section->link >= eh->shnum ||
            !section_addr[rela_section->info] || rela_section->entsize != sizeof(struct debug_elf_rela)) {
            ret = -22;
            goto out;
        }
        symbols = debug_section_at(eh, image, (uint16_t)rela_section->link);
        if (symbols->type != SHT_SYMTAB || symbols->link >= eh->shnum ||
            symbols->entsize != sizeof(struct debug_elf_symbol)) { ret = -22; goto out; }
        strings = debug_section_at(eh, image, (uint16_t)symbols->link);
        if (strings->type != SHT_STRTAB) { ret = -22; goto out; }
        for (uint64_t offset = 0; offset < rela_section->size; offset += sizeof(struct debug_elf_rela)) {
            const struct debug_elf_rela *rela = (const struct debug_elf_rela *)((const uint8_t *)image + rela_section->offset + offset);
            uint32_t symbol_index = (uint32_t)(rela->info >> 32);
            uint32_t type = (uint32_t)rela->info;
            const struct debug_elf_symbol *symbol;
            uint64_t symbol_addr;
            uint8_t *place;
            uint32_t width = (type == R_X86_64_64) ? 8U : (type == R_X86_64_NONE ? 0U : 4U);
            if ((uint64_t)symbol_index * sizeof(*symbol) + sizeof(*symbol) > symbols->size ||
                width > debug_section_at(eh, image, (uint16_t)rela_section->info)->size ||
                rela->offset > debug_section_at(eh, image, (uint16_t)rela_section->info)->size - width) { ret = -22; goto out; }
            symbol = (const struct debug_elf_symbol *)((const uint8_t *)image + symbols->offset +
                                                       (uint64_t)symbol_index * sizeof(*symbol));
            {
                uint8_t symbol_type = symbol->info & 0x0fU;
                if (symbol_type != STT_NOTYPE && symbol_type != STT_OBJECT &&
                    symbol_type != STT_FUNC && symbol_type != STT_SECTION) {
                    ret = (symbol_type == STT_TLS || symbol_type == STT_GNU_IFUNC) ? -95 : -22;
                    goto out;
                }
            }
            if (symbol->section_index == SHN_UNDEF) { ret = -95; goto out; }
            if (symbol->section_index == SHN_ABS) symbol_addr = symbol->value;
            else if (symbol->section_index >= eh->shnum || !section_addr[symbol->section_index]) { ret = -95; goto out; }
            else {
                const struct debug_elf_section *symbol_section = debug_section_at(eh, image, symbol->section_index);
                if (symbol->value > symbol_section->size) { ret = -22; goto out; }
                symbol_addr = section_addr[symbol->section_index] + symbol->value;
            }
            place = (uint8_t *)(uintptr_t)(section_addr[rela_section->info] + rela->offset);
            ret = debug_apply_relocation(place, type, symbol_addr, rela->addend, (uint64_t)(uintptr_t)place);
            if (ret < 0) goto out;
        }
    }
    for (uint16_t index = 0; index < eh->shnum && !entry; ++index) {
        const struct debug_elf_section *symbols = debug_section_at(eh, image, index);
        const struct debug_elf_section *strings;
        if (symbols->type != SHT_SYMTAB || symbols->link >= eh->shnum || symbols->entsize != sizeof(struct debug_elf_symbol)) continue;
        strings = debug_section_at(eh, image, (uint16_t)symbols->link);
        if (strings->type != SHT_STRTAB) continue;
        for (uint64_t offset = 0; offset < symbols->size; offset += sizeof(struct debug_elf_symbol)) {
            const struct debug_elf_symbol *symbol = (const struct debug_elf_symbol *)((const uint8_t *)image + symbols->offset + offset);
            if (symbol->name >= strings->size || symbol->section_index >= eh->shnum || !section_addr[symbol->section_index]) continue;
            if (debug_text_eq((const char *)image + strings->offset + symbol->name,
                              "leonos_kernel_debug_module_entry")) {
                entry = (leonos_kernel_debug_entry_fn)(uintptr_t)(section_addr[symbol->section_index] + symbol->value);
                break;
            }
        }
    }
    ret = entry ? entry(api) : -2;
out:
    mm_free_pages(allocation, pages);
    return ret;
}

int kernel_debug_control(struct leonos_kernel_debug_control *control)
{
    uint32_t enabled = debug_file_exists(KERNEL_DEBUG_ENABLED_PATH)
                           ? LEONOS_KERNEL_DEBUG_STATE_ENABLED : 0U;
    int ret = 0;
    if (!control || control->version != LEONOS_KERNEL_DEBUG_VERSION) {
        return -22;
    }
    control->result_flags = enabled;
    switch (control->command) {
    case LEONOS_KERNEL_DEBUG_CONTROL_GET_STATE:
        return 0;
    case LEONOS_KERNEL_DEBUG_CONTROL_SET_ENABLED:
        (void)storage_mkdir("/system");
        (void)storage_mkdir("/system/state");
        if (control->flags & LEONOS_KERNEL_DEBUG_STATE_ENABLED) {
            ret = storage_write_file(KERNEL_DEBUG_ENABLED_PATH, "1\n", 2U);
        } else {
            ret = storage_unlink(KERNEL_DEBUG_ENABLED_PATH);
            if (ret == -2) ret = 0;
        }
        break;
    case LEONOS_KERNEL_DEBUG_CONTROL_ARM_NEXT_BOOT:
        if (!enabled) return -1;
        ret = storage_write_boot_esp_file("/boot/system/state/kerneldebug.next",
                                          KERNEL_DEBUG_MARKER,
                                          (uint32_t)(sizeof(KERNEL_DEBUG_MARKER) - 1U));
        break;
    case LEONOS_KERNEL_DEBUG_CONTROL_CLEAR:
        ret = storage_unlink(KERNEL_DEBUG_ENABLED_PATH);
        if (ret == -2) ret = 0;
        if (ret == 0) {
            int marker_ret = storage_unlink_boot_esp_file("/boot/system/state/kerneldebug.next");
            if (marker_ret < 0 && marker_ret != -2) ret = marker_ret;
        }
        break;
    default:
        return -22;
    }
    control->result_flags = (ret == 0 && control->command == LEONOS_KERNEL_DEBUG_CONTROL_SET_ENABLED &&
                             (control->flags & LEONOS_KERNEL_DEBUG_STATE_ENABLED))
                                ? LEONOS_KERNEL_DEBUG_STATE_ENABLED : 0U;
    return ret;
}

int kernel_debug_clear_state(void)
{
    struct leonos_kernel_debug_control control = {
        .version = LEONOS_KERNEL_DEBUG_VERSION,
        .command = LEONOS_KERNEL_DEBUG_CONTROL_CLEAR,
    };
    return kernel_debug_control(&control);
}

bool kernel_debug_boot_requested(const struct leonos_boot_handoff *handoff)
{
    return handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC &&
           handoff->version == LEONOS_BOOT_HANDOFF_VERSION &&
           handoff->kernel_debug_mode != 0U && debug_file_exists(KERNEL_DEBUG_ENABLED_PATH);
}

static uint64_t debug_rdtsc(void)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static void debug_write(const char *text)
{
    ostui_write(text ? text : "");
}

static void debug_yield(void)
{
    sched_yield_current();
}

/*
 * The debugger sees every exported syscall and ioctl.  Each entry records
 * whether its contract permits an early-boot probe.  Read-only calls are
 * invoked with bounded zero arguments; pointer-taking interfaces normally
 * return their documented EFAULT/EINVAL path.  State-changing calls are
 * deliberately listed but never invoked from Ring-0 diagnostics.
 */
#define DEBUG_PROBE_SKIP 0x01U
#define DEBUG_ARRAY_COUNT(items) ((uint32_t)(sizeof(items) / sizeof((items)[0])))

struct debug_probe {
    const char *name;
    uint64_t value;
    uint8_t kind;
    uint8_t flags;
};

static const struct debug_probe debug_syscall_probes[] = {
    {"read (0)", LINUX_SYS_READ, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"write (1)", LINUX_SYS_WRITE, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"open (2)", LINUX_SYS_OPEN, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"close (3)", LINUX_SYS_CLOSE, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"stat (4)", LINUX_SYS_STAT, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"fstat (5)", LINUX_SYS_FSTAT, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"lseek (8)", LINUX_SYS_LSEEK, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"mmap (9)", LINUX_SYS_MMAP, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"mprotect (10)", LINUX_SYS_MPROTECT, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"munmap (11)", LINUX_SYS_MUNMAP, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"ioctl (16)", LINUX_SYS_IOCTL, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"pipe (22)", LINUX_SYS_PIPE, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"sched_yield (24)", LINUX_SYS_SCHED_YIELD, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"dup (32)", LINUX_SYS_DUP, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"dup2 (33)", LINUX_SYS_DUP2, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"nice (34)", LINUX_SYS_NICE, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"nanosleep (35)", LINUX_SYS_NANOSLEEP, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"getpid (39)", LINUX_SYS_GETPID, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"fork (57)", LINUX_SYS_FORK, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"vfork (58)", LINUX_SYS_VFORK, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"execve (59)", LINUX_SYS_EXECVE, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"exit (60)", LINUX_SYS_EXIT, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"wait4 (61)", LINUX_SYS_WAIT4, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"kill (62)", LINUX_SYS_KILL, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"fcntl (72)", LINUX_SYS_FCNTL, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"ftruncate (77)", LINUX_SYS_FTRUNCATE, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"getcwd (79)", LINUX_SYS_GETCWD, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"chdir (80)", LINUX_SYS_CHDIR, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"rename (82)", LINUX_SYS_RENAME, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"mkdir (83)", LINUX_SYS_MKDIR, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"rmdir (84)", LINUX_SYS_RMDIR, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"unlink (87)", LINUX_SYS_UNLINK, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"getrlimit (97)", LINUX_SYS_GETRLIMIT, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"setpgid (109)", LINUX_SYS_SETPGID, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"getppid (110)", LINUX_SYS_GETPPID, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"getpgrp (111)", LINUX_SYS_GETPGRP, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"setsid (112)", LINUX_SYS_SETSID, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"getpgid (121)", LINUX_SYS_GETPGID, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"getpriority (140)", LINUX_SYS_GETPRIORITY, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, 0},
    {"setpriority (141)", LINUX_SYS_SETPRIORITY, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
    {"setrlimit (160)", LINUX_SYS_SETRLIMIT, LEONOS_KERNEL_DEBUG_BENCH_SYSCALL, DEBUG_PROBE_SKIP},
};

#define DEBUG_IOCTL(name, request, skip) {name, request, LEONOS_KERNEL_DEBUG_BENCH_IOCTL, skip}
static const struct debug_probe debug_ioctl_probes[] = {
    DEBUG_IOCTL("system information", LEONOS_IOCTL_SYSTEM_INFO, 0),
    DEBUG_IOCTL("performance information", LEONOS_IOCTL_PERF_INFO, 0),
    DEBUG_IOCTL("clock information", LEONOS_IOCTL_TIME_INFO, 0),
    DEBUG_IOCTL("NTP clock sync", LEONOS_IOCTL_TIME_NTP_SYNC, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("machine identity", LEONOS_IOCTL_MACHINE_IDENTITY, 0),
    DEBUG_IOCTL("directory listing", LEONOS_IOCTL_LIST_DIR, 0),
    DEBUG_IOCTL("installer disk list", LEONOS_INSTALL_IOCTL_LIST_DISKS, 0),
    DEBUG_IOCTL("installer format target", LEONOS_INSTALL_IOCTL_FORMAT_TARGET, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("installer mount target", LEONOS_INSTALL_IOCTL_MOUNT_TARGET, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("partition list", LEONOS_DISK_IOCTL_LIST_PARTITIONS, 0),
    DEBUG_IOCTL("partition format", LEONOS_DISK_IOCTL_FORMAT_PARTITION, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("partition delete", LEONOS_DISK_IOCTL_DELETE_PARTITION, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("partition create", LEONOS_DISK_IOCTL_CREATE_PARTITION, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("partition mount", LEONOS_DISK_IOCTL_MOUNT_PARTITION, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("partition unmount", LEONOS_DISK_IOCTL_UNMOUNT_PARTITION, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("filesystem ACL get", LEONOS_FS_IOCTL_ACL_GET, 0),
    DEBUG_IOCTL("filesystem ACL set", LEONOS_FS_IOCTL_ACL_SET, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("filesystem take ownership", LEONOS_FS_IOCTL_ACL_TAKE_OWNERSHIP, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("filesystem ACL repair", LEONOS_FS_IOCTL_ACL_REPAIR, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("authentication status", LEONOS_AUTH_IOCTL_STATUS, 0),
    DEBUG_IOCTL("current account", LEONOS_AUTH_IOCTL_CURRENT, 0),
    DEBUG_IOCTL("account list", LEONOS_AUTH_IOCTL_LIST_USERS, 0),
    DEBUG_IOCTL("account login", LEONOS_AUTH_IOCTL_LOGIN, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("administrator elevation", LEONOS_AUTH_IOCTL_ELEVATE_ADMIN, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("elevation delegation", LEONOS_AUTH_IOCTL_DELEGATE_ELEVATION, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("account logout", LEONOS_AUTH_IOCTL_LOGOUT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("account create", LEONOS_AUTH_IOCTL_CREATE_USER, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("account update", LEONOS_AUTH_IOCTL_UPDATE_USER, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("password change", LEONOS_AUTH_IOCTL_CHANGE_PASSWORD, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("input provider register", LEONOS_INPUTM_IOCTL_REGISTER, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("input provider unregister", LEONOS_INPUTM_IOCTL_UNREGISTER, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("input provider next", LEONOS_INPUTM_IOCTL_PROVIDER_NEXT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("input provider result", LEONOS_INPUTM_IOCTL_PROVIDER_RESULT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("input submit key", LEONOS_INPUTM_IOCTL_SUBMIT_KEY, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("input poll result", LEONOS_INPUTM_IOCTL_POLL_RESULT, 0),
    DEBUG_IOCTL("input active provider", LEONOS_INPUTM_IOCTL_SET_ACTIVE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("input provider list", LEONOS_INPUTM_IOCTL_LIST, 0),
    DEBUG_IOCTL("input context", LEONOS_INPUTM_IOCTL_CONTEXT, 0),
    DEBUG_IOCTL("input state", LEONOS_INPUTM_IOCTL_GET_STATE, 0),
    DEBUG_IOCTL("input configuration notice", LEONOS_INPUTM_IOCTL_NOTIFY_CONFIG, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network configuration", LEONOS_IOCTL_NET_CONFIG, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network DHCP", LEONOS_IOCTL_NET_DHCP, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network DNS policy", LEONOS_IOCTL_NET_DNS_POLICY, 0),
    DEBUG_IOCTL("network ping", LEONOS_IOCTL_NET_PING, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network DNS lookup", LEONOS_IOCTL_NET_DNS, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network HTTP request", LEONOS_IOCTL_NET_HTTP_GET, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network socket open", LEONOS_IOCTL_NET_SOCKET_OPEN, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network socket connect", LEONOS_IOCTL_NET_SOCKET_CONNECT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network socket send", LEONOS_IOCTL_NET_SOCKET_SEND, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network socket receive", LEONOS_IOCTL_NET_SOCKET_RECV, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network socket close", LEONOS_IOCTL_NET_SOCKET_CLOSE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("network connections", LEONOS_IOCTL_NET_CONNECTIONS, 0),
    DEBUG_IOCTL("audio configuration", LEONOS_IOCTL_AUDIO_CONFIGURE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("audio playback", LEONOS_IOCTL_AUDIO_WRITE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("audio state", LEONOS_IOCTL_AUDIO_GET_STATE, 0),
    DEBUG_IOCTL("device list", LEONOS_IOCTL_DEVICE_LIST, 0),
    DEBUG_IOCTL("driver list", LEONOS_IOCTL_DRIVER_LIST, 0),
    DEBUG_IOCTL("driver control", LEONOS_IOCTL_DRIVER_CONTROL, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("startup request", LEONOS_STARTUP_IOCTL_REQUEST, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("startup request status", LEONOS_STARTUP_IOCTL_REQUEST_STATUS, 0),
    DEBUG_IOCTL("startup dialog get", LEONOS_STARTUP_IOCTL_DIALOG_GET, 0),
    DEBUG_IOCTL("startup dialog resolve", LEONOS_STARTUP_IOCTL_DIALOG_RESOLVE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("startup item list", LEONOS_STARTUP_IOCTL_LIST, 0),
    DEBUG_IOCTL("startup item enable", LEONOS_STARTUP_IOCTL_SET_ENABLED, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("startup item remove", LEONOS_STARTUP_IOCTL_REMOVE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("startup launch current", LEONOS_STARTUP_IOCTL_LAUNCH_CURRENT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("UTF-8 layout", LEONOS_TEXT_IOCTL_LAYOUT_UTF8, 0),
    DEBUG_IOCTL("kernel debug control", LEONOS_KERNEL_DEBUG_IOCTL_CONTROL, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("PTY create", LEONOS_PTY_IOCTL_CREATE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("PTY destroy", LEONOS_PTY_IOCTL_DESTROY, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("PTY read output", LEONOS_PTY_IOCTL_READ_OUTPUT, 0),
    DEBUG_IOCTL("PTY write input", LEONOS_PTY_IOCTL_WRITE_INPUT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("PTY spawn", LEONOS_PTY_IOCTL_SPAWN, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("PTY self", LEONOS_PTY_IOCTL_SELF, 0),
    DEBUG_IOCTL("PTY input available", LEONOS_PTY_IOCTL_INPUT_AVAILABLE, 0),
    DEBUG_IOCTL("PTY get attributes", LEONOS_PTY_IOCTL_GET_ATTR, 0),
    DEBUG_IOCTL("PTY set attributes", LEONOS_PTY_IOCTL_SET_ATTR, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("PTY owner get attributes", LEONOS_PTY_IOCTL_OWNER_GET_ATTR, 0),
    DEBUG_IOCTL("PTY owner set attributes", LEONOS_PTY_IOCTL_OWNER_SET_ATTR, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("PTY owner get window size", LEONOS_PTY_IOCTL_OWNER_GET_WINSIZE, 0),
    DEBUG_IOCTL("PTY owner set window size", LEONOS_PTY_IOCTL_OWNER_SET_WINSIZE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("PTY get window size", LEONOS_PTY_IOCTL_GET_WINSIZE, 0),
    DEBUG_IOCTL("PTY set window size", LEONOS_PTY_IOCTL_SET_WINSIZE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("PTY get process group", LEONOS_PTY_IOCTL_GET_PGRP, 0),
    DEBUG_IOCTL("PTY set process group", LEONOS_PTY_IOCTL_SET_PGRP, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI protocol version", KERNEL_DEBUG_GUI_IOCTL_VERSION, 0),
    DEBUG_IOCTL("GUI input event", KERNEL_DEBUG_GUI_IOCTL_EVENT, 0),
    DEBUG_IOCTL("GUI uptime", KERNEL_DEBUG_GUI_IOCTL_UPTIME_MS, 0),
    DEBUG_IOCTL("GUI framebuffer info", KERNEL_DEBUG_GUI_IOCTL_FB_INFO, 0),
    DEBUG_IOCTL("GUI framebuffer capabilities", KERNEL_DEBUG_GUI_IOCTL_FB_CAPS, 0),
    DEBUG_IOCTL("GUI framebuffer mode", KERNEL_DEBUG_GUI_IOCTL_FB_SET_MODE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI framebuffer fill", KERNEL_DEBUG_GUI_IOCTL_FB_FILL, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI framebuffer rectangle", KERNEL_DEBUG_GUI_IOCTL_FB_RECT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI framebuffer text", KERNEL_DEBUG_GUI_IOCTL_FB_TEXT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI framebuffer pixel", KERNEL_DEBUG_GUI_IOCTL_FB_PIXEL, 0),
    DEBUG_IOCTL("GUI framebuffer blit", KERNEL_DEBUG_GUI_IOCTL_FB_BLIT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI window create", KERNEL_DEBUG_GUI_IOCTL_CREATE_WINDOW, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI window destroy", KERNEL_DEBUG_GUI_IOCTL_DESTROY_WINDOW, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI window update", LEONOS_GUI_IOCTL_UPDATE_WINDOW, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI taskbar visibility", LEONOS_GUI_IOCTL_SET_TASKBAR_VISIBLE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI cursor request", LEONOS_GUI_IOCTL_CURSOR_REQUEST, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI mouse visibility", LEONOS_GUI_IOCTL_SET_MOUSE_VISIBLE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI poll window", KERNEL_DEBUG_GUI_IOCTL_POLL_WINDOW, 0),
    DEBUG_IOCTL("GUI present window", KERNEL_DEBUG_GUI_IOCTL_PRESENT_WINDOW, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI fetch window", KERNEL_DEBUG_GUI_IOCTL_FETCH_WINDOW, 0),
    DEBUG_IOCTL("GUI window event", KERNEL_DEBUG_GUI_IOCTL_WINDOW_EVENT, 0),
    DEBUG_IOCTL("GUI wait window event", KERNEL_DEBUG_GUI_IOCTL_WAIT_WINDOW_EVENT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI send window event", KERNEL_DEBUG_GUI_IOCTL_SEND_WINDOW_EVENT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI task list", KERNEL_DEBUG_GUI_IOCTL_TASKS, 0),
    DEBUG_IOCTL("GUI task kill", KERNEL_DEBUG_GUI_IOCTL_TASK_KILL, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI display state", KERNEL_DEBUG_GUI_IOCTL_DISPLAY_STATE, 0),
    DEBUG_IOCTL("GUI display request", KERNEL_DEBUG_GUI_IOCTL_DISPLAY_REQUEST, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI display request poll", KERNEL_DEBUG_GUI_IOCTL_POLL_DISPLAY_REQUEST, 0),
    DEBUG_IOCTL("GUI display publish", KERNEL_DEBUG_GUI_IOCTL_PUBLISH_DISPLAY_STATE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI appearance state", LEONOS_GUI_IOCTL_APPEARANCE_STATE, 0),
    DEBUG_IOCTL("GUI appearance request", LEONOS_GUI_IOCTL_APPEARANCE_REQUEST, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("GUI appearance request poll", LEONOS_GUI_IOCTL_POLL_APPEARANCE_REQUEST, 0),
    DEBUG_IOCTL("GUI appearance publish", LEONOS_GUI_IOCTL_PUBLISH_APPEARANCE_STATE, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("system reboot", KERNEL_DEBUG_GUI_IOCTL_REBOOT, DEBUG_PROBE_SKIP),
    DEBUG_IOCTL("system shutdown", KERNEL_DEBUG_GUI_IOCTL_SHUTDOWN, DEBUG_PROBE_SKIP),
};
#undef DEBUG_IOCTL

static uint64_t debug_probe_number;
static uint64_t debug_probe_ioctl;
static uint8_t debug_probe_is_ioctl;

static void debug_copy_name(char destination[48], const char *source)
{
    uint32_t index = 0;
    while (source && source[index] && index + 1U < 48U) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = 0;
}

static int64_t debug_call_probe(void)
{
    struct syscall_frame frame = {.number = debug_probe_number};
    if (debug_probe_is_ioctl) {
        frame.args[0] = 3U;
        frame.args[1] = debug_probe_ioctl;
    }
    return syscall_dispatch(&frame);
}

static void debug_measure_probe(const struct debug_probe *probe,
                                struct leonos_kernel_debug_benchmark *report)
{
    uint64_t total = 0;
    uint64_t minimum = UINT64_MAX;
    uint64_t maximum = 0;
    int64_t result = 0;

    *report = (struct leonos_kernel_debug_benchmark){0};
    debug_copy_name(report->name, probe->name);
    report->kind = probe->kind;
    if (probe->flags & DEBUG_PROBE_SKIP) {
        report->status = LEONOS_KERNEL_DEBUG_BENCH_SKIPPED;
        return;
    }
    debug_probe_number = probe->kind == LEONOS_KERNEL_DEBUG_BENCH_SYSCALL
                             ? probe->value : LINUX_SYS_IOCTL;
    debug_probe_ioctl = probe->value;
    debug_probe_is_ioctl = probe->kind == LEONOS_KERNEL_DEBUG_BENCH_IOCTL;
    for (uint32_t iteration = 0; iteration < KERNEL_DEBUG_BENCH_ITERATIONS; ++iteration) {
        uint64_t start = debug_rdtsc();
        result = debug_call_probe();
        uint64_t elapsed = debug_rdtsc() - start;
        if (elapsed < minimum) minimum = elapsed;
        if (elapsed > maximum) maximum = elapsed;
        total += elapsed;
        if ((iteration & 63U) == 63U) debug_yield();
    }
    report->minimum_cycles = minimum == UINT64_MAX ? 0 : minimum;
    report->average_cycles = total / KERNEL_DEBUG_BENCH_ITERATIONS;
    report->maximum_cycles = maximum;
    report->result = (int32_t)result;
    report->iterations = KERNEL_DEBUG_BENCH_ITERATIONS;
    report->status = result < 0 ? LEONOS_KERNEL_DEBUG_BENCH_EXPECTED_ERROR
                                 : LEONOS_KERNEL_DEBUG_BENCH_OK;
}

static int debug_run_safe_benchmarks(void *reports, uint32_t capacity, uint32_t *count)
{
    struct leonos_kernel_debug_benchmark *out = reports;
    uint32_t total = DEBUG_ARRAY_COUNT(debug_syscall_probes) +
                     DEBUG_ARRAY_COUNT(debug_ioctl_probes);
    if (!count) return -22;
    *count = total;
    if (!out || capacity < total) return -12;
    for (uint32_t index = 0; index < DEBUG_ARRAY_COUNT(debug_syscall_probes); ++index) {
        debug_measure_probe(&debug_syscall_probes[index], &out[index]);
    }
    for (uint32_t index = 0; index < DEBUG_ARRAY_COUNT(debug_ioctl_probes); ++index) {
        debug_measure_probe(&debug_ioctl_probes[index],
                            &out[DEBUG_ARRAY_COUNT(debug_syscall_probes) + index]);
    }
    return 0;
}

static int debug_run_dangerous_benchmarks(void *reports, uint32_t capacity, uint32_t *count)
{
    (void)reports;
    (void)capacity;
    if (count) *count = 0;
    return 0;
}

static void debug_tui_u64(uint64_t value)
{
    ostui_write_u64(value);
}

/**
 * @brief The staged module runs before the first schedulable user task exists. sched_yield_current() is deliberately only a pause in that phase, so a polling menu would leave interrupts disabled forever. Sleep until the keyboard IRQ has delivered an event, then return its normalized keycode.
 */
static int debug_wait_key(void)
{
    for (;;) {
        int key = ostui_poll_key();
        if (key) return key;
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
}

static void debug_menu(void)
{
    uint32_t selected = 0;
    static const char *items[] = {
        "[1] Run all syscall/ioctl diagnostics\n",
        "[2] Dangerous tests (disabled by default)\n",
        "[3] Reboot\n",
        "[4] Shutdown\n",
        "[5] Continue normal startup\n",
    };
    ostui_init();
    for (;;) {
        ostui_clear();
        ostui_write("\033[1;36mLeonOS 4 Kernel Debugger\033[0m\n\n");
        ostui_write("Use number keys. Diagnostics run in Ring-0.\n\n");
        for (uint32_t i = 0; i < 5U; ++i) {
            if (i == selected) ostui_write("\033[7m");
            ostui_write(items[i]);
            if (i == selected) ostui_write("\033[0m");
        }
        ostui_write("\nTSC: ");
        debug_tui_u64(debug_rdtsc());
        ostui_write("\n");
        for (;;) {
            int key = ostui_poll_key();
            if (key == 0) {
                __asm__ volatile("sti; hlt; cli");
                continue;
            }
            if (key == 72U && selected) { --selected; break; }
            if (key == 80U && selected < 4U) { ++selected; break; }
            if (key == 28 || (key >= 2 && key <= 6)) {
                uint32_t choice = key == 28U ? selected : (uint32_t)(key - 2U);
                if (choice == 0U) {
                    uint64_t start = debug_rdtsc();
                    for (uint32_t i = 0; i < 1000U; ++i) debug_yield();
                    uint64_t end = debug_rdtsc();
                    ostui_write("\nSafe benchmark cycles: ");
                    debug_tui_u64(end - start);
                    ostui_write("\nPress Enter to return.");
                } else if (choice == 1U) {
                    ostui_write("\nDangerous tests require a future explicit per-test confirmation.\nPress Enter to return.");
                } else if (choice == 2U) {
                    power_reboot();
                } else if (choice == 3U) {
                    power_shutdown();
                } else {
                    (void)kernel_debug_clear_state();
                    return;
                }
                while (ostui_poll_key() != 28U) __asm__ volatile("sti; hlt; cli");
                break;
            }
        }
    }
}

int kernel_debug_run_module(void)
{
    const void *image = 0;
    size_t len = 0;
    const struct leonos_kernel_debug_api api = {
        .version = LEONOS_KERNEL_DEBUG_MODULE_ABI,
        .write = debug_write,
        .rdtsc = debug_rdtsc,
        .yield = debug_yield,
        .tui_init = ostui_init,
        .tui_write = ostui_write,
        .tui_write_u64 = ostui_write_u64,
        .tui_key = debug_wait_key,
        .tui_clear = ostui_clear,
        .run_safe_benchmarks = debug_run_safe_benchmarks,
        .run_dangerous_benchmarks = debug_run_dangerous_benchmarks,
        .clear_state = kernel_debug_clear_state,
        .reboot = power_reboot,
        .shutdown = power_shutdown,
    };
    int ret = storage_read_file(KERNEL_DEBUG_MODULE_PATH, &image, &len);
    if (ret < 0) {
        console_printf("[ntclks] kernel debug module missing ret=%d\n", ret);
        debug_write("\033[31mKernel debug module missing.\033[0m\n");
        debug_menu();
        return 0;
    }
    ret = debug_load_module(image, len, &api);
    if (ret < 0) {
        console_printf("[ntclks] kernel debug module rejected or failed ret=%d\n", ret);
        debug_write("\033[31mKernel debug module rejected or failed.\033[0m\n");
    }
    mm_free_pages((uint64_t)(uintptr_t)image, (uint32_t)((len + 4095U) / 4096U));
    if (ret < 0) {
        debug_menu();
        return 0;
    }
    /* A valid module owns the diagnostic session.  The built-in menu above
     * is reserved for missing, malformed, or failed modules. */
    return 0;
}
