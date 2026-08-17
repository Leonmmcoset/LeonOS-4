/*
 * LeonOS Multiboot2 support: decodes bootloader information structures.
 * Extracts memory maps, framebuffer data, modules, and command-line metadata.
 */
#include <ntclks/console.h>
#include <ntclks/multiboot2.h>

/**
 * @brief Coordinates the align8 operation.
 * @param value Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uintptr_t align8(uintptr_t value)
{
    return (value + 7u) & ~((uintptr_t)7u);
}

/**
 * @brief Coordinates the multiboot2 parse operation.
 * @param magic Input or output value used by this operation.
 * @param info_addr Address used by this operation; its address-space interpretation follows the API.
 * @param out Caller-provided storage that receives output from this operation.
 */
void multiboot2_parse(uint32_t magic, uintptr_t info_addr, struct boot_info *out)
{
    for (size_t i = 0; i < sizeof(*out); ++i) {
        ((uint8_t *)out)[i] = 0;
    }
    out->magic = magic;
    out->multiboot_info = (uint64_t)info_addr;

    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC || info_addr == 0) {
        console_printf("[ntclks] invalid multiboot2 handoff magic=0x%x info=%p\n",
                       magic, (void *)info_addr);
        return;
    }

    const struct multiboot2_info *info = (const struct multiboot2_info *)info_addr;
    uintptr_t cursor = info_addr + sizeof(*info);
    uintptr_t end = info_addr + info->total_size;

    while (cursor < end) {
        const struct multiboot2_tag *tag = (const struct multiboot2_tag *)cursor;
        if (tag->type == MULTIBOOT2_TAG_TYPE_END || tag->size == 0) {
            break;
        }

        switch (tag->type) {
        case MULTIBOOT2_TAG_TYPE_CMDLINE:
            out->cmdline = ((const struct multiboot2_tag_string *)tag)->string;
            break;
        case MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME:
            out->bootloader = ((const struct multiboot2_tag_string *)tag)->string;
            break;
        case MULTIBOOT2_TAG_TYPE_MODULE: {
            const struct multiboot2_tag_module *mod = (const struct multiboot2_tag_module *)tag;
            if (out->module_count < 16) {
                struct boot_module *dst = &out->modules[out->module_count++];
                dst->start = mod->mod_start;
                dst->end = mod->mod_end;
                dst->name = mod->string;
            }
            break;
        }
        case MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO: {
            const uint32_t *mem = (const uint32_t *)((const uint8_t *)tag + 8);
            out->memory_lower_kib = mem[0];
            out->memory_upper_kib = mem[1];
            break;
        }
        case MULTIBOOT2_TAG_TYPE_MMAP: {
            const struct multiboot2_tag_mmap *mmap = (const struct multiboot2_tag_mmap *)tag;
            out->mmap_addr = (uint64_t)(uintptr_t)mmap->entries;
            out->mmap_entry_size = mmap->entry_size;
            out->mmap_entry_count = (tag->size - sizeof(*mmap)) / mmap->entry_size;
            break;
        }
        case MULTIBOOT2_TAG_TYPE_FRAMEBUFFER: {
            const struct multiboot2_tag_framebuffer *fb = (const struct multiboot2_tag_framebuffer *)tag;
            out->framebuffer_addr = fb->framebuffer_addr;
            out->framebuffer_width = fb->framebuffer_width;
            out->framebuffer_height = fb->framebuffer_height;
            out->framebuffer_pitch = fb->framebuffer_pitch;
            out->framebuffer_bpp = fb->framebuffer_bpp;
            out->framebuffer_type = fb->framebuffer_type;
            if (fb->framebuffer_type == MULTIBOOT2_FRAMEBUFFER_TYPE_RGB &&
                tag->size >= sizeof(*fb) + MULTIBOOT2_FRAMEBUFFER_RGB_INFO_SIZE) {
                const uint8_t *rgb = (const uint8_t *)fb + sizeof(*fb);
                out->framebuffer_red_field_position = rgb[0];
                out->framebuffer_red_mask_size = rgb[1];
                out->framebuffer_green_field_position = rgb[2];
                out->framebuffer_green_mask_size = rgb[3];
                out->framebuffer_blue_field_position = rgb[4];
                out->framebuffer_blue_mask_size = rgb[5];
            }
            break;
        }
        case MULTIBOOT2_TAG_TYPE_EFI64: {
            const uint64_t *efi = (const uint64_t *)((const uint8_t *)tag + 8);
            out->efi_system_table = *efi;
            break;
        }
        case MULTIBOOT2_TAG_TYPE_EFI_MMAP: {
            const struct multiboot2_tag_efi_mmap *mmap =
                (const struct multiboot2_tag_efi_mmap *)tag;
            out->efi_mmap_addr = (uint64_t)(uintptr_t)mmap->efi_mmap;
            out->efi_mmap_entry_size = mmap->descr_size;
            out->efi_mmap_entry_count = mmap->descr_size ?
                (tag->size - sizeof(*mmap)) / mmap->descr_size : 0;
            break;
        }
        case MULTIBOOT2_TAG_TYPE_ACPI_OLD:
        case MULTIBOOT2_TAG_TYPE_ACPI_NEW:
            out->rsdp_addr = (uint64_t)(uintptr_t)((const uint8_t *)tag + 8);
            break;
        default:
            break;
        }

        cursor = align8(cursor + tag->size);
    }

    console_printf("[ntclks] GRUB handoff ok bootloader=%s cmdline=%s modules=%u\n",
                   out->bootloader ? out->bootloader : "(unknown)",
                   out->cmdline ? out->cmdline : "(none)",
                   out->module_count);
}
