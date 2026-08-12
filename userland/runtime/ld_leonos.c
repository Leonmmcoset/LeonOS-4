#include <dlfcn.h>
#include <leonos/elf_abi.h>
#include <leonos/syscall.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stddef.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#define EI_NIDENT 16
#define ET_DYN 3
#define EM_X86_64 62

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_NOTE 4
#define PT_GNU_RELRO 0x6474e552u

#define PF_X 1
#define PF_W 2

#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_PLTGOT 3
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_INIT 12
#define DT_FINI 13
#define DT_SONAME 14
#define DT_RPATH 15
#define DT_SYMBOLIC 16
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_DEBUG 21
#define DT_TEXTREL 22
#define DT_JMPREL 23
#define DT_BIND_NOW 24
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RUNPATH 29
#define DT_FLAGS 30
#define DT_PREINIT_ARRAY 32
#define DT_PREINIT_ARRAYSZ 33
#define DT_GNU_HASH 0x6ffffef5ULL
#define DT_RELACOUNT 0x6ffffff9ULL
#define DT_FLAGS_1 0x6ffffffbULL
#define DT_RELR 0x6fffe000ULL
#define DT_RELRSZ 0x6fffe001ULL
#define DT_RELRENT 0x6fffe003ULL

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_COPY 5
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8

#define STB_GLOBAL 1
#define STB_WEAK 2

#define O_RDONLY 0
#define LOADER_MAX_MODULES 32u
#define LOADER_MAX_PHDRS 32u
#define LOADER_MAX_DEPS 16u
#define LOADER_PATH_MAX 260u
#define LOADER_PATH_WORK_MAX 520u
#define LEONOS_DYNLINK_ERROR_APP_PATH "0:/system/apps/dynlinkerror/dynlinkerror.elf"

enum loader_module_state {
    LOADER_MODULE_EMPTY = 0,
    LOADER_MODULE_LOADING,
    LOADER_MODULE_READY,
};

struct elf64_ehdr {
    unsigned char ident[EI_NIDENT];
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

struct elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

struct elf64_nhdr {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
};

struct elf64_dyn {
    int64_t tag;
    uint64_t value;
};

struct elf64_sym {
    uint32_t name;
    unsigned char info;
    unsigned char other;
    uint16_t shndx;
    uint64_t value;
    uint64_t size;
};

struct elf64_rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
};

struct loader_module {
    uint32_t used;
    uint32_t state;
    uint32_t refs;
    uint32_t global;
    uint32_t permanent;
    uint32_t initialized;
    uint32_t phnum;
    uint32_t dep_count;
    uint64_t base;
    uint64_t low;
    uint64_t high;
    uint64_t map_start;
    uint64_t map_end;
    struct elf64_phdr phdrs[LOADER_MAX_PHDRS];
    struct elf64_dyn *dynamic;
    const struct elf64_sym *symtab;
    const char *strtab;
    uint64_t strsz;
    uint32_t symcount;
    struct loader_module *deps[LOADER_MAX_DEPS];
    char path[LOADER_PATH_MAX];
};

static struct loader_module modules[LOADER_MAX_MODULES];
static char last_error[160];
static char missing_shared_object[LOADER_PATH_MAX];
static unsigned char error_pending;
static unsigned char process_finalizers_ran;

static uint64_t page_down(uint64_t value)
{
    return value & ~4095ULL;
}

static uint64_t page_up(uint64_t value)
{
    if (value > UINT64_MAX - 4095ULL) {
        return 0;
    }
    return (value + 4095ULL) & ~4095ULL;
}

static uint64_t align4_up(uint64_t value)
{
    if (value > UINT64_MAX - 3ULL) {
        return 0;
    }
    return (value + 3ULL) & ~3ULL;
}

static int text_equal(const char *left, const char *right)
{
    uint32_t i = 0;
    if (!left || !right) {
        return 0;
    }
    while (left[i] && left[i] == right[i]) {
        ++i;
    }
    return left[i] == right[i];
}

static int text_has_slash(const char *text)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        if (text[i] == '/') {
            return 1;
        }
    }
    return 0;
}

static void text_copy(char *out, uint32_t cap, const char *text)
{
    uint32_t i = 0;
    if (!out || !cap) {
        return;
    }
    while (text && text[i] && i + 1 < cap) {
        out[i] = text[i];
        ++i;
    }
    out[i] = 0;
}

static void set_error(const char *text)
{
    text_copy(last_error, sizeof(last_error), text);
    error_pending = 1;
}

static void clear_missing_shared_object(void)
{
    missing_shared_object[0] = 0;
}

static void report_missing_shared_object(const char *program_path)
{
    char *const argv[] = {
        (char *)LEONOS_DYNLINK_ERROR_APP_PATH,
        (char *)(program_path ? program_path : ""),
        missing_shared_object,
        NULL,
    };
    if (!missing_shared_object[0]) {
        return;
    }
    (void)execve(LEONOS_DYNLINK_ERROR_APP_PATH, argv, NULL);
}

static int read_exact(int fd, void *buffer, uint64_t size)
{
    uint8_t *dst = (uint8_t *)buffer;
    uint64_t done = 0;
    while (done < size) {
        long ret = read(fd, dst + done, (size_t)(size - done));
        if (ret <= 0) {
            return -1;
        }
        done += (uint64_t)ret;
    }
    return 0;
}

static int read_at(int fd, uint64_t offset, void *buffer, uint64_t size)
{
    if (offset > 0x7fffffffffffffffULL || lseek(fd, (long)offset, SEEK_SET) < 0) {
        return -1;
    }
    return read_exact(fd, buffer, size);
}

static uint64_t choose_base(uint64_t span)
{
    void *reservation = mmap(NULL, span, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t base;
    if (reservation == MAP_FAILED) {
        return 0;
    }
    base = (uint64_t)(uintptr_t)reservation;
    if (munmap(reservation, span) < 0) {
        return 0;
    }
    return base;
}

static int elf_header_valid(const struct elf64_ehdr *header)
{
    return header && header->ident[0] == 0x7f && header->ident[1] == 'E' &&
           header->ident[2] == 'L' && header->ident[3] == 'F' &&
           header->ident[4] == 2 && header->ident[5] == 1 && header->ident[6] == 1 &&
           header->type == ET_DYN && header->machine == EM_X86_64 && header->version == 1 &&
           header->ehsize == sizeof(*header) &&
           header->phentsize == sizeof(struct elf64_phdr) &&
           header->phnum && header->phnum <= LOADER_MAX_PHDRS;
}

static int range_in_module(const struct loader_module *module, uint64_t address, uint64_t size)
{
    return module && address >= module->map_start && size <= module->map_end - address;
}

static int range_has_writable_load(const struct loader_module *module, uint64_t address,
                                   uint64_t size)
{
    for (uint32_t i = 0; module && i < module->phnum; ++i) {
        const struct elf64_phdr *ph = &module->phdrs[i];
        uint64_t start;
        uint64_t end;
        if (ph->type != PT_LOAD || !(ph->flags & PF_W) || ph->vaddr > UINT64_MAX - module->base) {
            continue;
        }
        start = module->base + ph->vaddr;
        if (ph->memsz > UINT64_MAX - start) {
            continue;
        }
        end = start + ph->memsz;
        if (address >= start && size <= end - address) {
            return 1;
        }
    }
    return 0;
}

static int file_has_abi_note(int fd, const struct elf64_phdr *phdrs, uint16_t phnum)
{
    for (uint16_t i = 0; i < phnum; ++i) {
        const struct elf64_phdr *ph = &phdrs[i];
        uint64_t cursor;
        uint64_t end;
        if (ph->type != PT_NOTE || !ph->filesz || ph->offset > UINT64_MAX - ph->filesz) {
            continue;
        }
        cursor = ph->offset;
        end = ph->offset + ph->filesz;
        while (cursor < end) {
            struct elf64_nhdr note;
            char name[16];
            struct leonos_elf_abi_note abi;
            uint64_t names;
            uint64_t desc;
            uint64_t next;
            if (end - cursor < sizeof(note) || read_at(fd, cursor, &note, sizeof(note)) < 0) {
                return 0;
            }
            names = cursor + sizeof(note);
            desc = align4_up(names + note.namesz);
            next = desc ? align4_up(desc + note.descsz) : 0;
            if (!desc || !next || names > end || note.namesz > end - names ||
                desc > end || note.descsz > end - desc || next > end) {
                return 0;
            }
            if (note.namesz == sizeof(LEONOS_ELF_NOTE_NAME) && note.namesz <= sizeof(name) &&
                note.descsz >= sizeof(abi) && read_at(fd, names, name, note.namesz) == 0 &&
                read_at(fd, desc, &abi, sizeof(abi)) == 0 &&
                note.type == LEONOS_ELF_NOTE_TYPE &&
                text_equal(name, LEONOS_ELF_NOTE_NAME) &&
                abi.major == LEONOS_ELF_ABI_MAJOR) {
                return 1;
            }
            cursor = next;
        }
    }
    return 0;
}

static int memory_has_abi_note(const struct loader_module *module)
{
    for (uint32_t i = 0; module && i < module->phnum; ++i) {
        const struct elf64_phdr *ph = &module->phdrs[i];
        uint64_t cursor;
        uint64_t end;
        if (ph->type != PT_NOTE || !ph->filesz || ph->vaddr > UINT64_MAX - module->base ||
            ph->filesz > UINT64_MAX - module->base - ph->vaddr) {
            continue;
        }
        cursor = module->base + ph->vaddr;
        end = cursor + ph->filesz;
        while (cursor < end) {
            const struct elf64_nhdr *note;
            const char *name;
            const struct leonos_elf_abi_note *abi;
            uint64_t names;
            uint64_t desc;
            uint64_t next;
            if (end - cursor < sizeof(*note)) {
                return 0;
            }
            note = (const struct elf64_nhdr *)(uintptr_t)cursor;
            names = cursor + sizeof(*note);
            desc = align4_up(names + note->namesz);
            next = desc ? align4_up(desc + note->descsz) : 0;
            if (!desc || !next || names > end || note->namesz > end - names ||
                desc > end || note->descsz > end - desc || next > end) {
                return 0;
            }
            name = (const char *)(uintptr_t)names;
            abi = (const struct leonos_elf_abi_note *)(uintptr_t)desc;
            if (note->type == LEONOS_ELF_NOTE_TYPE &&
                note->namesz == sizeof(LEONOS_ELF_NOTE_NAME) &&
                note->descsz >= sizeof(*abi) && text_equal(name, LEONOS_ELF_NOTE_NAME) &&
                abi->major == LEONOS_ELF_ABI_MAJOR) {
                return 1;
            }
            cursor = next;
        }
    }
    return 0;
}

static int load_segments_valid(const struct elf64_phdr *phdrs, uint16_t phnum,
                               uint64_t file_size, uint64_t *out_low, uint64_t *out_high)
{
    uint64_t low = UINT64_MAX;
    uint64_t high = 0;
    uint32_t count = 0;
    for (uint16_t i = 0; i < phnum; ++i) {
        const struct elf64_phdr *ph = &phdrs[i];
        uint64_t start;
        uint64_t end;
        if (ph->type != PT_LOAD || !ph->memsz) {
            continue;
        }
        if (ph->filesz > ph->memsz || ph->offset > file_size || ph->filesz > file_size - ph->offset ||
            ph->vaddr > UINT64_MAX - ph->memsz ||
            ph->offset % 4096ULL != ph->vaddr % 4096ULL ||
            ((ph->flags & PF_W) && (ph->flags & PF_X)) ||
            (ph->memsz > ph->filesz && !(ph->flags & PF_W))) {
            return 0;
        }
        start = page_down(ph->vaddr);
        end = page_up(ph->vaddr + ph->memsz);
        if (!end || end <= start) {
            return 0;
        }
        for (uint16_t j = 0; j < i; ++j) {
            const struct elf64_phdr *other = &phdrs[j];
            uint64_t other_start;
            uint64_t other_end;
            if (other->type != PT_LOAD || !other->memsz) {
                continue;
            }
            other_start = page_down(other->vaddr);
            other_end = page_up(other->vaddr + other->memsz);
            if (start < other_end && end > other_start) {
                return 0;
            }
        }
        if (start < low) {
            low = start;
        }
        if (end > high) {
            high = end;
        }
        ++count;
    }
    if (!count || low == UINT64_MAX || high <= low) {
        return 0;
    }
    *out_low = low;
    *out_high = high;
    return 1;
}

static int dynamic_tag_allowed(int64_t tag)
{
    switch (tag) {
    case DT_NULL:
    case DT_NEEDED:
    case DT_PLTRELSZ:
    case DT_PLTGOT:
    case DT_HASH:
    case DT_STRTAB:
    case DT_SYMTAB:
    case DT_RELA:
    case DT_RELASZ:
    case DT_RELAENT:
    case DT_STRSZ:
    case DT_SYMENT:
    case DT_INIT:
    case DT_FINI:
    case DT_SONAME:
    case DT_SYMBOLIC:
    case DT_PLTREL:
    case DT_DEBUG:
    case DT_JMPREL:
    case DT_BIND_NOW:
    case DT_INIT_ARRAY:
    case DT_FINI_ARRAY:
    case DT_INIT_ARRAYSZ:
    case DT_FINI_ARRAYSZ:
    case DT_FLAGS:
    case DT_PREINIT_ARRAY:
    case DT_PREINIT_ARRAYSZ:
    case (int64_t)DT_GNU_HASH:
    case (int64_t)DT_RELACOUNT:
    case (int64_t)DT_FLAGS_1:
        return 1;
    default:
        return 0;
    }
}

static uint64_t dyn_value(const struct elf64_dyn *dynamic, uint64_t max_entries, int64_t tag)
{
    for (uint64_t i = 0; dynamic && i < max_entries; ++i) {
        if (dynamic[i].tag == DT_NULL) {
            break;
        }
        if (dynamic[i].tag == tag) {
            return dynamic[i].value;
        }
    }
    return 0;
}

static uint64_t module_dynamic_entries(const struct loader_module *module)
{
    for (uint32_t i = 0; module && i < module->phnum; ++i) {
        if (module->phdrs[i].type == PT_DYNAMIC) {
            return module->phdrs[i].memsz / sizeof(struct elf64_dyn);
        }
    }
    return 0;
}

static const char *module_string(const struct loader_module *module, uint64_t offset)
{
    const char *text;
    uint64_t i;
    if (!module || offset >= module->strsz) {
        return NULL;
    }
    text = module->strtab + offset;
    for (i = offset; i < module->strsz; ++i) {
        if (!text[i - offset]) {
            return text;
        }
    }
    return NULL;
}

static int module_parse_dynamic(struct loader_module *module)
{
    uint64_t entries = module_dynamic_entries(module);
    uint64_t strtab;
    uint64_t symtab;
    uint64_t hash;
    uint64_t strsz;
    uint64_t syment;
    uint64_t relaent;
    uint64_t terminated = 0;
    if (!module || !module->dynamic || !entries) {
        set_error("missing dynamic section");
        return -1;
    }
    for (uint64_t i = 0; i < entries; ++i) {
        int64_t tag = module->dynamic[i].tag;
        if (tag == DT_NULL) {
            terminated = 1;
            break;
        }
        if (tag == DT_RPATH || tag == DT_RUNPATH || tag == DT_TEXTREL || tag == DT_REL ||
            tag == DT_RELSZ || tag == DT_RELENT || tag == (int64_t)DT_RELR ||
            tag == (int64_t)DT_RELRSZ || tag == (int64_t)DT_RELRENT) {
            set_error("unsupported dynamic ELF feature");
            return -1;
        }
        if (!dynamic_tag_allowed(tag)) {
            set_error("unknown dynamic ELF tag");
            return -1;
        }
    }
    if (!terminated) {
        set_error("unterminated dynamic section");
        return -1;
    }
    strtab = dyn_value(module->dynamic, entries, DT_STRTAB);
    symtab = dyn_value(module->dynamic, entries, DT_SYMTAB);
    hash = dyn_value(module->dynamic, entries, DT_HASH);
    strsz = dyn_value(module->dynamic, entries, DT_STRSZ);
    syment = dyn_value(module->dynamic, entries, DT_SYMENT);
    relaent = dyn_value(module->dynamic, entries, DT_RELAENT);
    if (!strtab || !symtab || !hash || !strsz || syment != sizeof(struct elf64_sym) ||
        (dyn_value(module->dynamic, entries, DT_RELA) && relaent != sizeof(struct elf64_rela)) ||
        strtab > UINT64_MAX - module->base || symtab > UINT64_MAX - module->base ||
        hash > UINT64_MAX - module->base ||
        !range_in_module(module, module->base + strtab, strsz) ||
        !range_in_module(module, module->base + symtab, sizeof(struct elf64_sym)) ||
        !range_in_module(module, module->base + hash, 8)) {
        set_error("invalid dynamic symbol table");
        return -1;
    }
    module->strtab = (const char *)(uintptr_t)(module->base + strtab);
    module->strsz = strsz;
    module->symtab = (const struct elf64_sym *)(uintptr_t)(module->base + symtab);
    module->symcount = ((const uint32_t *)(uintptr_t)(module->base + hash))[1];
    if (!module->symcount || !range_in_module(module, (uint64_t)(uintptr_t)module->symtab,
                                                (uint64_t)module->symcount * sizeof(*module->symtab))) {
        set_error("invalid dynamic symbol count");
        return -1;
    }
    return 0;
}

static int module_init_from_memory(struct loader_module *module, const char *path,
                                   uint64_t base, const struct elf64_ehdr *header,
                                   const struct elf64_phdr *phdrs)
{
    uint64_t low = UINT64_MAX;
    uint64_t high = 0;
    uint32_t dynamic_seen = 0;
    if (!module || !elf_header_valid(header) || !phdrs) {
        set_error("invalid shared object header");
        return -1;
    }
    *module = (struct loader_module){0};
    for (uint16_t i = 0; i < header->phnum; ++i) {
        const struct elf64_phdr *ph = &phdrs[i];
        module->phdrs[i] = *ph;
        if (ph->type == PT_LOAD && ph->memsz) {
            uint64_t start = page_down(base + ph->vaddr);
            uint64_t end = page_up(base + ph->vaddr + ph->memsz);
            if (!end || end <= start) {
                set_error("invalid shared object segment");
                return -1;
            }
            if (start < low) {
                low = start;
            }
            if (end > high) {
                high = end;
            }
        }
        if (ph->type == PT_DYNAMIC) {
            if (dynamic_seen || ph->vaddr > UINT64_MAX - base ||
                !ph->memsz || ph->memsz % sizeof(struct elf64_dyn)) {
                set_error("invalid dynamic section");
                return -1;
            }
            module->dynamic = (struct elf64_dyn *)(uintptr_t)(base + ph->vaddr);
            dynamic_seen = 1;
        }
    }
    if (!dynamic_seen || low == UINT64_MAX || high <= low) {
        set_error("shared object has no dynamic load image");
        return -1;
    }
    module->used = 1;
    module->base = base;
    module->low = low;
    module->high = high;
    module->map_start = low;
    module->map_end = high;
    module->phnum = header->phnum;
    text_copy(module->path, sizeof(module->path), path);
    if (!memory_has_abi_note(module) || module_parse_dynamic(module) < 0) {
        *module = (struct loader_module){0};
        set_error("shared object ABI v1 validation failed");
        return -1;
    }
    return 0;
}

static void module_unmap(struct loader_module *module)
{
    if (module && module->map_end > module->map_start) {
        (void)munmap((void *)(uintptr_t)module->map_start, module->map_end - module->map_start);
    }
}

static int map_module_file(const char *path, struct loader_module *module)
{
    struct elf64_ehdr header;
    struct elf64_phdr phdrs[LOADER_MAX_PHDRS];
    struct leonos_stat stat_result;
    uint64_t low;
    uint64_t high;
    uint64_t base;
    uint64_t file_size = 0;
    int fd = -1;

    /* Each map attempt owns the missing-file result.  This is important for
     * the private-directory then system-directory fallback lookup. */
    clear_missing_shared_object();
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        if (stat(path, &stat_result) < 0) {
            text_copy(missing_shared_object, sizeof(missing_shared_object), path);
            set_error("shared object not found");
        } else {
            set_error("shared object cannot be opened");
        }
        return -1;
    }
    clear_missing_shared_object();
    if (read_at(fd, 0, &header, sizeof(header)) < 0 || !elf_header_valid(&header) ||
        read_at(fd, header.phoff, phdrs, (uint64_t)header.phnum * sizeof(phdrs[0])) < 0) {
        close(fd);
        set_error("invalid shared object ELF");
        return -1;
    }
    for (uint16_t i = 0; i < header.phnum; ++i) {
        const struct elf64_phdr *ph = &phdrs[i];
        if (ph->type == PT_LOAD && ph->offset <= UINT64_MAX - ph->filesz &&
            ph->offset + ph->filesz > file_size) {
            file_size = ph->offset + ph->filesz;
        }
    }
    if (!file_size || !load_segments_valid(phdrs, header.phnum, file_size, &low, &high) ||
        !file_has_abi_note(fd, phdrs, header.phnum)) {
        close(fd);
        set_error("shared object ABI v1 validation failed");
        return -1;
    }
    base = choose_base(high - low);
    if (!base || base < low) {
        close(fd);
        set_error("shared object address space unavailable");
        return -1;
    }
    base -= low;
    *module = (struct loader_module){0};
    for (uint16_t i = 0; i < header.phnum; ++i) {
        const struct elf64_phdr *ph = &phdrs[i];
        uint64_t start;
        uint64_t file_end;
        uint64_t file_map_end;
        uint64_t seg_end;
        uint64_t file_offset;
        uint64_t file_map_len;
        int prot = PROT_READ;
        if (ph->type != PT_LOAD || !ph->memsz) {
            continue;
        }
        if (ph->flags & PF_X) {
            prot |= PROT_EXEC;
        }
        if (ph->flags & PF_W) {
            prot |= PROT_WRITE;
        }
        start = page_down(base + ph->vaddr);
        file_end = base + ph->vaddr + ph->filesz;
        file_map_end = page_up(file_end);
        seg_end = page_up(base + ph->vaddr + ph->memsz);
        file_offset = page_down(ph->offset);
        file_map_len = ph->filesz ? file_map_end - start : 0;
        if (file_map_len && mmap((void *)(uintptr_t)start, file_map_len, prot,
                                 MAP_PRIVATE | MAP_FIXED, fd, (long)file_offset) == MAP_FAILED) {
            set_error("shared object file segment map failed");
            goto fail;
        }
        if (ph->memsz > ph->filesz && file_end < file_map_end) {
            uint8_t *zero = (uint8_t *)(uintptr_t)file_end;
            uint64_t count = file_map_end - file_end;
            while (count--) {
                *zero++ = 0;
            }
        }
        if (seg_end > file_map_end && mmap((void *)(uintptr_t)file_map_end,
                                           seg_end - file_map_end, prot,
                                           MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                                           -1, 0) == MAP_FAILED) {
            set_error("shared object BSS segment map failed");
            goto fail;
        }
    }
    close(fd);
    if (module_init_from_memory(module, path, base, &header, phdrs) < 0) {
        module_unmap(module);
        return -1;
    }
    return 0;

fail:
    close(fd);
    if (high > low) {
        (void)munmap((void *)(uintptr_t)(base + low), high - low);
    }
    return -1;
}

static struct loader_module *module_slot(void)
{
    for (uint32_t i = 0; i < LOADER_MAX_MODULES; ++i) {
        if (!modules[i].used) {
            return &modules[i];
        }
    }
    set_error("dynamic module limit reached");
    return NULL;
}

static struct loader_module *module_by_path(const char *path)
{
    for (uint32_t i = 0; i < LOADER_MAX_MODULES; ++i) {
        if (modules[i].used && text_equal(modules[i].path, path)) {
            return &modules[i];
        }
    }
    return NULL;
}

static int normalize_path(char out[LOADER_PATH_MAX], const char *base_path, const char *name)
{
    char work[LOADER_PATH_WORK_MAX];
    uint32_t marks[64];
    uint32_t write = 0;
    uint32_t mark_count = 0;
    uint32_t read = 0;
    uint32_t work_len = 0;
    int absolute;
    if (!out || !name || !name[0]) {
        return -1;
    }
    absolute = name[0] && name[1] == ':' && name[2] == '/';
    if (absolute) {
        text_copy(work, sizeof(work), name);
    } else {
        uint32_t slash = 0;
        if (!base_path || !(base_path[0] && base_path[1] == ':' && base_path[2] == '/')) {
            return -1;
        }
        while (base_path[work_len] && work_len + 1 < sizeof(work)) {
            work[work_len] = base_path[work_len];
            if (base_path[work_len] == '/') {
                slash = work_len + 1;
            }
            ++work_len;
        }
        if (!slash || slash + 1 >= sizeof(work)) {
            return -1;
        }
        work_len = slash;
        for (uint32_t i = 0; name[i] && work_len + 1 < sizeof(work); ++i) {
            work[work_len++] = name[i];
        }
        work[work_len] = 0;
    }
    if (!(work[0] && work[1] == ':' && work[2] == '/')) {
        return -1;
    }
    out[write++] = work[0];
    out[write++] = ':';
    out[write++] = '/';
    read = 3;
    while (work[read]) {
        uint32_t start;
        uint32_t length;
        while (work[read] == '/') {
            ++read;
        }
        if (!work[read]) {
            break;
        }
        start = read;
        while (work[read] && work[read] != '/') {
            ++read;
        }
        length = read - start;
        if (length == 1 && work[start] == '.') {
            continue;
        }
        if (length == 2 && work[start] == '.' && work[start + 1] == '.') {
            if (!mark_count) {
                return -1;
            }
            write = marks[--mark_count];
            continue;
        }
        if (mark_count == sizeof(marks) / sizeof(marks[0]) || write + length + 2 > LOADER_PATH_MAX) {
            return -1;
        }
        marks[mark_count++] = write;
        if (write > 3) {
            out[write++] = '/';
        }
        for (uint32_t i = 0; i < length; ++i) {
            out[write++] = work[start + i];
        }
    }
    out[write] = 0;
    return 0;
}

static int apply_relro(const struct loader_module *module)
{
    for (uint32_t i = 0; module && i < module->phnum; ++i) {
        const struct elf64_phdr *ph = &module->phdrs[i];
        uint64_t start;
        uint64_t end;
        if (ph->type != PT_GNU_RELRO || !ph->memsz) {
            continue;
        }
        start = page_down(module->base + ph->vaddr);
        end = page_up(module->base + ph->vaddr + ph->memsz);
        if (!end || end <= start || mprotect((void *)(uintptr_t)start, end - start, PROT_READ) < 0) {
            set_error("RELRO protection failed");
            return -1;
        }
    }
    return 0;
}

static const struct elf64_sym *module_find_symbol(const struct loader_module *module,
                                                   const char *name, int *weak)
{
    const struct elf64_sym *weak_result = NULL;
    if (weak) {
        *weak = 0;
    }
    for (uint32_t i = 0; module && i < module->symcount; ++i) {
        const struct elf64_sym *symbol = &module->symtab[i];
        const char *candidate;
        unsigned char binding;
        if (!symbol->shndx || symbol->name >= module->strsz ||
            !(candidate = module_string(module, symbol->name))) {
            continue;
        }
        binding = symbol->info >> 4;
        if ((binding != STB_GLOBAL && binding != STB_WEAK) || !text_equal(candidate, name)) {
            continue;
        }
        if (binding == STB_GLOBAL) {
            return symbol;
        }
        weak_result = symbol;
    }
    if (weak_result && weak) {
        *weak = 1;
    }
    return weak_result;
}

static const struct elf64_sym *module_find_symbol_tree(const struct loader_module *module,
                                                        const char *name,
                                                        uint32_t seen[LOADER_MAX_MODULES],
                                                        const struct loader_module **owner)
{
    int weak = 0;
    const struct elf64_sym *result;
    uint32_t index;
    if (!module) {
        return NULL;
    }
    index = (uint32_t)(module - modules);
    if (index >= LOADER_MAX_MODULES || seen[index]) {
        return NULL;
    }
    seen[index] = 1;
    result = module_find_symbol(module, name, &weak);
    if (result && !weak) {
        *owner = module;
        return result;
    }
    for (uint32_t i = 0; i < module->dep_count; ++i) {
        result = module_find_symbol_tree(module->deps[i], name, seen, owner);
        if (result) {
            return result;
        }
    }
    if (result) {
        *owner = module;
    }
    return result;
}

static const struct elf64_sym *resolve_symbol(const struct loader_module *requester,
                                               const char *name,
                                               const struct loader_module **owner)
{
    uint32_t seen[LOADER_MAX_MODULES] = {0};
    const struct elf64_sym *result;
    if (!name || !name[0] || !owner) {
        return NULL;
    }
    result = module_find_symbol_tree(requester, name, seen, owner);
    if (result) {
        return result;
    }
    for (uint32_t i = 0; i < LOADER_MAX_MODULES; ++i) {
        if (!modules[i].used || !modules[i].global) {
            continue;
        }
        result = module_find_symbol_tree(&modules[i], name, seen, owner);
        if (result) {
            return result;
        }
    }
    return NULL;
}

static int relocate_table(struct loader_module *module, uint64_t rela, uint64_t relasz)
{
    if (!relasz) {
        return 0;
    }
    if (!rela || relasz % sizeof(struct elf64_rela) ||
        !range_in_module(module, module->base + rela, relasz)) {
        set_error("invalid dynamic relocation table");
        return -1;
    }
    for (uint64_t pos = 0; pos < relasz; pos += sizeof(struct elf64_rela)) {
        const struct elf64_rela *entry = (const struct elf64_rela *)(uintptr_t)(module->base + rela + pos);
        uint32_t type = (uint32_t)entry->info;
        uint32_t index = (uint32_t)(entry->info >> 32);
        uint64_t target;
        uint64_t *where;
        if (entry->offset > UINT64_MAX - module->base) {
            set_error("invalid relocation target");
            return -1;
        }
        target = module->base + entry->offset;
        if (!range_has_writable_load(module, target, sizeof(*where))) {
            set_error("text relocation rejected");
            return -1;
        }
        where = (uint64_t *)(uintptr_t)target;
        if (type == R_X86_64_NONE) {
            continue;
        }
        if (type == R_X86_64_RELATIVE) {
            *where = module->base + (uint64_t)entry->addend;
            continue;
        }
        if (type == R_X86_64_COPY) {
            set_error("COPY relocation rejected");
            return -1;
        }
        if ((type != R_X86_64_64 && type != R_X86_64_GLOB_DAT && type != R_X86_64_JUMP_SLOT) ||
            index >= module->symcount) {
            set_error("unsupported dynamic relocation");
            return -1;
        }
        {
            const struct elf64_sym *symbol = &module->symtab[index];
            const struct elf64_sym *resolved;
            const struct loader_module *owner = NULL;
            const char *name = module_string(module, symbol->name);
            if (!name) {
                set_error("invalid dynamic relocation symbol");
                return -1;
            }
            resolved = resolve_symbol(module, name, &owner);
            if (!resolved) {
                if ((symbol->info >> 4) == STB_WEAK) {
                    *where = (uint64_t)entry->addend;
                    continue;
                }
                set_error("unresolved dynamic symbol");
                return -1;
            }
            *where = owner->base + resolved->value + (uint64_t)entry->addend;
        }
    }
    return 0;
}

static int relocate_module(struct loader_module *module)
{
    uint64_t entries = module_dynamic_entries(module);
    uint64_t rela = dyn_value(module->dynamic, entries, DT_RELA);
    uint64_t relasz = dyn_value(module->dynamic, entries, DT_RELASZ);
    uint64_t jmprel = dyn_value(module->dynamic, entries, DT_JMPREL);
    uint64_t pltrelsz = dyn_value(module->dynamic, entries, DT_PLTRELSZ);
    uint64_t pltrel = dyn_value(module->dynamic, entries, DT_PLTREL);
    if (relocate_table(module, rela, relasz) < 0) {
        return -1;
    }
    if (pltrelsz) {
        if (!jmprel || pltrel != DT_RELA || relocate_table(module, jmprel, pltrelsz) < 0) {
            if (!last_error[0]) {
                set_error("unsupported PLT relocation format");
            }
            return -1;
        }
    } else if (jmprel || pltrel) {
        set_error("invalid PLT relocation table");
        return -1;
    }
    return 0;
}

static int run_array(const struct loader_module *module, int64_t tag, int64_t size_tag,
                     int reverse)
{
    uint64_t entries = module_dynamic_entries(module);
    uint64_t address = dyn_value(module->dynamic, entries, tag);
    uint64_t size = dyn_value(module->dynamic, entries, size_tag);
    if (!size) {
        return 0;
    }
    if (!address || size % sizeof(uint64_t) || !range_in_module(module, module->base + address, size)) {
        set_error("invalid dynamic initializer array");
        return -1;
    }
    if (reverse) {
        for (uint64_t i = size; i; i -= sizeof(uint64_t)) {
            void (*function)(void) = *(void (**)(void))(uintptr_t)(module->base + address + i - sizeof(uint64_t));
            if (function) {
                function();
            }
        }
    } else {
        for (uint64_t i = 0; i < size; i += sizeof(uint64_t)) {
            void (*function)(void) = *(void (**)(void))(uintptr_t)(module->base + address + i);
            if (function) {
                function();
            }
        }
    }
    return 0;
}

static int run_initializers(struct loader_module *module, int include_preinit)
{
    uint64_t entries = module_dynamic_entries(module);
    uint64_t init = dyn_value(module->dynamic, entries, DT_INIT);
    if (include_preinit && run_array(module, DT_PREINIT_ARRAY, DT_PREINIT_ARRAYSZ, 0) < 0) {
        return -1;
    }
    if (init) {
        ((void (*)(void))(uintptr_t)(module->base + init))();
    }
    if (run_array(module, DT_INIT_ARRAY, DT_INIT_ARRAYSZ, 0) < 0) {
        return -1;
    }
    module->initialized = 1;
    return 0;
}

static void run_finalizers(struct loader_module *module)
{
    uint64_t entries = module_dynamic_entries(module);
    uint64_t fini = dyn_value(module->dynamic, entries, DT_FINI);
    if (!module || !module->initialized) {
        return;
    }
    (void)run_array(module, DT_FINI_ARRAY, DT_FINI_ARRAYSZ, 1);
    if (fini) {
        ((void (*)(void))(uintptr_t)(module->base + fini))();
    }
    module->initialized = 0;
}

/* Dynamic applications bind exit() here.  Picolibc's implementation knows
 * only libleonos' own fini array, while the loader owns the executable's
 * array.  Keep both paths in one place for an explicit exit and for a main()
 * return through crt0_dynamic. */
void exit(int code)
{
    extern void __libc_fini_array(void);
    extern void _exit(int) __attribute__((noreturn));
    if (!process_finalizers_ran) {
        process_finalizers_ran = 1;
        if (modules[0].used) {
            run_finalizers(&modules[0]);
        }
        __libc_fini_array();
    }
    _exit(code);
    __builtin_unreachable();
}

void leonos_dynamic_exit(int code)
{
    exit(code);
}

static int module_release(struct loader_module *module);

static int module_add_dependency(struct loader_module *module, struct loader_module *dependency)
{
    if (!module || !dependency || module->dep_count >= LOADER_MAX_DEPS) {
        set_error("dynamic dependency limit reached");
        return -1;
    }
    module->deps[module->dep_count++] = dependency;
    return 0;
}

static int load_module_path(const char *path, struct loader_module *requester,
                            int mode, struct loader_module **out);

static int load_module_name(const char *name, struct loader_module *requester,
                            int mode, struct loader_module **out)
{
    char candidate[LOADER_PATH_MAX];
    if (text_has_slash(name)) {
        if (normalize_path(candidate, requester ? requester->path : LEONOS_ELF_RUNTIME_PATH, name) < 0) {
            set_error("invalid shared object path");
            return -1;
        }
        return load_module_path(candidate, requester, mode, out);
    }
    if (requester && normalize_path(candidate, requester->path, name) == 0 &&
        load_module_path(candidate, requester, mode, out) == 0) {
        return 0;
    }
    /* A private candidate that exists but is malformed or ABI-incompatible
     * is the actual load error.  Only try the system directory after a true
     * private-file miss, so the recovery UI does not misreport it as absent. */
    if (requester && !(mode & RTLD_NOLOAD) && !missing_shared_object[0]) {
        return -1;
    }
    if (normalize_path(candidate, LEONOS_ELF_RUNTIME_PATH, name) < 0) {
        set_error("invalid system shared object path");
        return -1;
    }
    return load_module_path(candidate, requester, mode, out);
}

static int load_module_path(const char *path, struct loader_module *requester,
                            int mode, struct loader_module **out)
{
    struct loader_module *module = module_by_path(path);
    uint64_t entries;
    if (module) {
        if (module->state == LOADER_MODULE_LOADING) {
            set_error("circular dynamic dependency rejected");
            return -1;
        }
        ++module->refs;
        if (mode & RTLD_GLOBAL) {
            module->global = 1;
        }
        *out = module;
        return 0;
    }
    if (mode & RTLD_NOLOAD) {
        set_error("shared object is not loaded");
        return -1;
    }
    module = module_slot();
    if (!module || map_module_file(path, module) < 0) {
        return -1;
    }
    module->state = LOADER_MODULE_LOADING;
    module->refs = 1;
    module->global = (mode & RTLD_GLOBAL) != 0;
    entries = module_dynamic_entries(module);
    for (uint64_t i = 0; i < entries && module->dynamic[i].tag != DT_NULL; ++i) {
        struct loader_module *dependency;
        const char *needed;
        if (module->dynamic[i].tag != DT_NEEDED) {
            continue;
        }
        needed = module_string(module, module->dynamic[i].value);
        if (!needed || load_module_name(needed, module, mode & RTLD_GLOBAL, &dependency) < 0 ||
            module_add_dependency(module, dependency) < 0) {
            goto fail;
        }
    }
    if (relocate_module(module) < 0 || apply_relro(module) < 0 || run_initializers(module, 0) < 0) {
        goto fail;
    }
    module->state = LOADER_MODULE_READY;
    *out = module;
    (void)requester;
    return 0;

fail:
    for (uint32_t i = module->dep_count; i; --i) {
        (void)module_release(module->deps[i - 1]);
    }
    module_unmap(module);
    *module = (struct loader_module){0};
    return -1;
}

static int module_release(struct loader_module *module)
{
    if (!module || !module->used) {
        set_error("invalid shared object handle");
        return -1;
    }
    if (module->permanent) {
        return 0;
    }
    if (!module->refs) {
        set_error("shared object reference underflow");
        return -1;
    }
    if (--module->refs) {
        return 0;
    }
    run_finalizers(module);
    for (uint32_t i = module->dep_count; i; --i) {
        (void)module_release(module->deps[i - 1]);
    }
    module_unmap(module);
    *module = (struct loader_module){0};
    return 0;
}

static int initialize_startup_modules(struct leonos_dynamic_launch *launch,
                                      uint64_t runtime_base)
{
    struct elf64_ehdr *main_header;
    struct elf64_ehdr *runtime_header;
    struct loader_module *main_module = &modules[0];
    struct loader_module *runtime_module = &modules[1];
    if (!launch || !runtime_base || modules[0].used || modules[1].used) {
        set_error("invalid dynamic loader startup state");
        return -1;
    }
    main_header = (struct elf64_ehdr *)(uintptr_t)launch->main_base;
    runtime_header = (struct elf64_ehdr *)(uintptr_t)runtime_base;
    if (module_init_from_memory(main_module, launch->main_path, launch->main_base, main_header,
                                (const struct elf64_phdr *)(uintptr_t)launch->main_phdr) < 0 ||
        module_init_from_memory(runtime_module, LEONOS_ELF_RUNTIME_PATH, runtime_base, runtime_header,
                                (const struct elf64_phdr *)(uintptr_t)(runtime_base + runtime_header->phoff)) < 0) {
        return -1;
    }
    main_module->permanent = 1;
    main_module->global = 1;
    main_module->refs = 1;
    main_module->state = LOADER_MODULE_READY;
    runtime_module->permanent = 1;
    runtime_module->global = 1;
    runtime_module->refs = 1;
    runtime_module->state = LOADER_MODULE_READY;
    if (module_add_dependency(main_module, runtime_module) < 0) {
        return -1;
    }
    return 0;
}

static int main_needs_runtime(const struct loader_module *main_module)
{
    uint64_t entries = module_dynamic_entries(main_module);
    uint32_t count = 0;
    for (uint64_t i = 0; i < entries && main_module->dynamic[i].tag != DT_NULL; ++i) {
        const char *needed;
        if (main_module->dynamic[i].tag != DT_NEEDED) {
            continue;
        }
        needed = module_string(main_module, main_module->dynamic[i].value);
        if (!needed || !text_equal(needed, LEONOS_ELF_RUNTIME_SONAME)) {
            set_error("unsupported initial runtime dependency");
            return -1;
        }
        ++count;
    }
    if (count != 1) {
        set_error("dynamic application must depend on libleonos.so.1");
        return -1;
    }
    return 0;
}

/* This entry point runs from libleonos rather than from the static bootstrap
 * interpreter.  Its module list is therefore also used by later dlopen(). */
int leonos_runtime_start(int argc, char **argv, char **envp,
                         struct leonos_dynamic_launch *launch, uint64_t runtime_base)
{
    struct loader_module *main_module;
    struct loader_module *runtime_module;
    if (!launch || launch->abi_major != LEONOS_ELF_ABI_MAJOR ||
        initialize_startup_modules(launch, runtime_base) < 0) {
        return 127;
    }
    main_module = &modules[0];
    runtime_module = &modules[1];
    if (main_needs_runtime(main_module) < 0 || relocate_module(main_module) < 0 || apply_relro(main_module) < 0 ||
        apply_relro(runtime_module) < 0 || run_initializers(runtime_module, 0) < 0 ||
        run_initializers(main_module, 1) < 0) {
        return 127;
    }
    return ((int (*)(int, char **, char **))(uintptr_t)launch->main_entry)(argc, argv, envp);
}

/* The static interpreter enters here after applying its own RELATIVE table.
 * It maps and relocates libleonos, then transfers to the runtime-resident
 * loader state used by the application. */
int ld_leonos_start(int argc, char **argv, char **envp, struct leonos_dynamic_launch *launch)
{
    struct loader_module runtime;
    const struct elf64_sym *entry;
    int weak;
    typedef int (*runtime_start_fn)(int, char **, char **,
                                    struct leonos_dynamic_launch *, uint64_t);
    if (!launch || launch->abi_major != LEONOS_ELF_ABI_MAJOR) {
        return 127;
    }
    clear_missing_shared_object();
    if (map_module_file(LEONOS_ELF_RUNTIME_PATH, &runtime) < 0) {
        report_missing_shared_object(launch->main_path);
        return 127;
    }
    /* Before the runtime's own module table exists, resolve its relocations
     * against itself only.  libleonos is intentionally self-contained. */
    for (uint32_t i = 0; i < LOADER_MAX_MODULES; ++i) {
        modules[i] = (struct loader_module){0};
    }
    modules[0] = runtime;
    modules[0].global = 1;
    modules[0].state = LOADER_MODULE_READY;
    if (relocate_module(&modules[0]) < 0) {
        module_unmap(&modules[0]);
        modules[0] = (struct loader_module){0};
        return 127;
    }
    entry = module_find_symbol(&modules[0], "leonos_runtime_start", &weak);
    if (!entry) {
        set_error("runtime bootstrap entry missing");
        module_unmap(&modules[0]);
        modules[0] = (struct loader_module){0};
        return 127;
    }
    runtime = modules[0];
    modules[0] = (struct loader_module){0};
    return ((runtime_start_fn)(uintptr_t)(runtime.base + entry->value))(
        argc, argv, envp, launch, runtime.base);
}

void *dlopen(const char *path, int mode)
{
    struct loader_module *module;
    if ((mode & ~(RTLD_LAZY | RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD)) ||
        !(mode & (RTLD_LAZY | RTLD_NOW))) {
        set_error("invalid dlopen mode");
        return NULL;
    }
    if (!path) {
        ++modules[0].refs;
        return &modules[0];
    }
    clear_missing_shared_object();
    if (load_module_name(path, &modules[0], mode, &module) < 0) {
        report_missing_shared_object(modules[0].path);
        return NULL;
    }
    return module;
}

void *dlsym(void *handle, const char *symbol)
{
    const struct elf64_sym *found;
    const struct loader_module *owner = NULL;
    struct loader_module *module = (struct loader_module *)handle;
    if (!symbol || !symbol[0]) {
        set_error("invalid dynamic symbol name");
        return NULL;
    }
    if (handle == RTLD_DEFAULT) {
        for (uint32_t i = 0; i < LOADER_MAX_MODULES; ++i) {
            int weak = 0;
            if (!modules[i].used || !modules[i].global) {
                continue;
            }
            found = module_find_symbol(&modules[i], symbol, &weak);
            if (found) {
                return (void *)(uintptr_t)(modules[i].base + found->value);
            }
        }
        set_error("dynamic symbol not found");
        return NULL;
    }
    if (handle == RTLD_NEXT) {
        uint64_t caller = (uint64_t)(uintptr_t)__builtin_return_address(0);
        uint32_t start = 0;
        for (uint32_t i = 0; i < LOADER_MAX_MODULES; ++i) {
            if (modules[i].used && caller >= modules[i].map_start && caller < modules[i].map_end) {
                start = i + 1;
                break;
            }
        }
        for (uint32_t i = start; i < LOADER_MAX_MODULES; ++i) {
            int weak = 0;
            if (!modules[i].used || !modules[i].global) {
                continue;
            }
            found = module_find_symbol(&modules[i], symbol, &weak);
            if (found) {
                return (void *)(uintptr_t)(modules[i].base + found->value);
            }
        }
        set_error("next dynamic symbol not found");
        return NULL;
    }
    if (!module || module < modules || module >= modules + LOADER_MAX_MODULES || !module->used) {
        set_error("invalid shared object handle");
        return NULL;
    }
    found = module_find_symbol_tree(module, symbol, (uint32_t[LOADER_MAX_MODULES]){0}, &owner);
    if (!found || !owner) {
        set_error("dynamic symbol not found");
        return NULL;
    }
    return (void *)(uintptr_t)(owner->base + found->value);
}

int dlclose(void *handle)
{
    struct loader_module *module = (struct loader_module *)handle;
    if (!module || module < modules || module >= modules + LOADER_MAX_MODULES) {
        set_error("invalid shared object handle");
        return -1;
    }
    return module_release(module);
}

const char *dlerror(void)
{
    if (!error_pending) {
        return NULL;
    }
    error_pending = 0;
    return last_error;
}

int dladdr(const void *address, Dl_info *info)
{
    uint64_t query = (uint64_t)(uintptr_t)address;
    if (!address || !info) {
        return 0;
    }
    for (uint32_t i = 0; i < LOADER_MAX_MODULES; ++i) {
        const struct loader_module *module = &modules[i];
        const struct elf64_sym *closest = NULL;
        if (!module->used || query < module->map_start || query >= module->map_end) {
            continue;
        }
        for (uint32_t s = 0; s < module->symcount; ++s) {
            const struct elf64_sym *candidate = &module->symtab[s];
            uint64_t symbol_address;
            if (!candidate->shndx || !candidate->name || candidate->value > UINT64_MAX - module->base) {
                continue;
            }
            symbol_address = module->base + candidate->value;
            if (symbol_address <= query && (!closest || symbol_address > module->base + closest->value)) {
                closest = candidate;
            }
        }
        info->dli_fname = module->path;
        info->dli_fbase = (void *)(uintptr_t)module->base;
        info->dli_sname = closest ? module_string(module, closest->name) : NULL;
        info->dli_saddr = closest ? (void *)(uintptr_t)(module->base + closest->value) : NULL;
        return 1;
    }
    return 0;
}
