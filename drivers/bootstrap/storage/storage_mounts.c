/* Read-only mount listing for /proc/mounts and /etc/mtab. */
int storage_node_mount_flags(const struct storage_node *node, uint64_t *mount_flags)
{
    if (!node || !mount_flags) return -22;
    uint64_t flags;
    kernel_execution_lock_irqsave(&flags);
    int ret = node->volume_id < STORAGE_MAX_VOLUMES && g_volumes[node->volume_id].ready ? 0 : -19;
    if (!ret) *mount_flags = g_volumes[node->volume_id].mount_flags;
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}

int storage_remount_path(const char *path, uint64_t mount_flags)
{
    if (!path) return -22;
    if (mount_flags & ~(uint64_t)(MS_NOSUID | MS_NOEXEC)) return -95;
    uint64_t flags;
    kernel_execution_lock_irqsave(&flags);
    int ret = -22;
    for (uint32_t i = 0; i < STORAGE_MAX_VOLUMES; ++i) {
        struct storage_volume *volume = &g_volumes[i];
        if (volume->ready && storage_text_eq(volume->mount_path, path)) {
            volume->mount_flags = mount_flags;
            ret = 0;
            break;
        }
    }
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}

struct storage_mount_text {
    uint64_t position, offset;
    uint32_t written, capacity;
    char *buffer;
};

/** @brief Emit bytes into a requested range without truncating the logical file. */
static void storage_mount_emit(struct storage_mount_text *text, const char *value)
{
    while (*value) {
        if (text->position >= text->offset && text->written < text->capacity)
            text->buffer[text->written++] = *value;
        ++text->position;
        ++value;
    }
}

/** @brief Escape mount fields as Linux procfs does, including spaces and backslashes. */
static void storage_mount_field(struct storage_mount_text *text, const char *value)
{
    while (*value) {
        char character[2] = {*value++, 0};
        const char *escaped = character;
        switch (character[0]) {
        case ' ': escaped = "\\040"; break;
        case '\t': escaped = "\\011"; break;
        case '\n': escaped = "\\012"; break;
        case '\\': escaped = "\\134"; break;
        }
        storage_mount_emit(text, escaped);
    }
}

/** @brief Append an unsigned decimal mount field without libc dependencies. */
static void storage_mount_number(struct storage_mount_text *text, uint32_t number)
{
    char value[16];
    storage_format_u32(value, sizeof(value), "", number, -1);
    storage_mount_emit(text, value);
}

/** @brief Find the closest containing mount, respecting component boundaries. */
static uint32_t storage_mount_parent(const char *path)
{
    uint32_t parent = 0, longest = 0;
    for (uint32_t i = 0; i < STORAGE_MAX_VOLUMES; ++i) {
        const struct storage_volume *volume = &g_volumes[i];
        uint32_t length = storage_strlen(volume->mount_path);
        if (!volume->ready || !length || length <= longest ||
            length >= storage_strlen(path)) continue;
        if (__builtin_strncmp(path, volume->mount_path, length)) continue;
        if (length > 1 && path[length] != '/') continue;
        longest = length;
        parent = i + 1;
    }
    return parent;
}

/** @brief Emit the common mountinfo prefix; IDs are stable for each mounted slot. */
static void storage_mount_prefix(struct storage_mount_text *text, uint32_t id,
                                 uint32_t device, const char *path, const char *options)
{
    storage_mount_number(text, id);
    storage_mount_emit(text, " ");
    storage_mount_number(text, storage_mount_parent(path));
    storage_mount_emit(text, " 0:");
    storage_mount_number(text, device);
    storage_mount_emit(text, " / ");
    storage_mount_field(text, path);
    storage_mount_emit(text, " ");
    storage_mount_emit(text, options);
    storage_mount_emit(text, " - ");
}

/** @brief Render real mounts under the same lock used by mount/unmount.
 * @param offset Byte offset into the live view.
 * @param buffer Destination of capacity bytes, nullable for an empty read.
 * @param capacity Maximum bytes to copy.
 * @param out_read Optional returned byte count.
 * @param mountinfo Select Linux mountinfo instead of mntent format.
 * @return Zero or negative errno. Reads do not invent tmpfs/sysfs mounts.
 */
static int storage_read_mount_table(uint64_t offset, void *buffer, uint32_t capacity,
                                     uint32_t *out_read, int mountinfo)
{
    struct storage_mount_text text = {0, offset, 0, capacity, buffer};
    uint64_t flags;
    if (out_read) *out_read = 0;
    if (!buffer && capacity) return -22;
    kernel_execution_lock_irqsave(&flags);
    for (uint32_t i = 0; i < STORAGE_MAX_VOLUMES; ++i) {
        const struct storage_volume *volume = &g_volumes[i];
        const char *filesystem;
        char source[80];
        if (!volume->ready || !volume->mount_path[0]) continue;
        switch (volume->filesystem) {
        case STORAGE_FILESYSTEM_EXT2: filesystem = "ext2"; break;
        case STORAGE_FILESYSTEM_FAT32: filesystem = "vfat"; break;
        case STORAGE_FILESYSTEM_EXFAT: filesystem = "exfat"; break;
        case STORAGE_FILESYSTEM_ISO9660: filesystem = "iso9660"; break;
        default:
            kernel_execution_unlock_irqrestore(flags);
            return -5;
        }
        if (volume->data_partition_mount) {
            storage_format_u32(source, sizeof(source), "/dev/disk",
                               volume->source_disk_id, (int32_t)volume->source_partition_index);
        } else if (!volume->ram_base && volume->transport &&
                   volume->filesystem != STORAGE_FILESYSTEM_ISO9660) {
            const uint8_t *guid = volume->filesystem == STORAGE_FILESYSTEM_EXT2 ? volume->ext2_unique_guid :
                volume->filesystem == STORAGE_FILESYSTEM_EXFAT ? volume->exfat_unique_guid : volume->esp_unique_guid;
            if (storage_guid_valid(guid)) {
                storage_copy_text(source, sizeof(source), "/dev/disk/by-partuuid/");
                storage_partition_guid_text(guid, source + 22);
            } else storage_copy_text(source, sizeof(source), "rootfs");
        } else {
            storage_copy_text(source, sizeof(source), volume->ram_base ? "ramdisk" : "rootfs");
        }
        char options[32];
        storage_copy_text(options, sizeof(options), volume->filesystem == STORAGE_FILESYSTEM_ISO9660 ? "ro" : "rw");
        if (volume->mount_flags & MS_NOSUID)
            storage_copy_text(options + 2, sizeof(options) - 2, ",nosuid");
        if (volume->mount_flags & MS_NOEXEC) {
            uint32_t length = storage_strlen(options);
            storage_copy_text(options + length, sizeof(options) - length, ",noexec");
        }
        if (mountinfo) {
            storage_mount_prefix(&text, i + 1, i + 1, volume->mount_path, options);
            storage_mount_emit(&text, filesystem);
            storage_mount_emit(&text, " ");
            storage_mount_field(&text, source);
        } else {
            storage_mount_field(&text, source);
            storage_mount_emit(&text, " ");
            storage_mount_field(&text, volume->mount_path);
            storage_mount_emit(&text, " ");
            storage_mount_emit(&text, filesystem);
        }
        storage_mount_emit(&text, " ");
        storage_mount_emit(&text, options);
        storage_mount_emit(&text, mountinfo ? "\n" : " 0 0\n");
    }
    if (g_devfs_enabled) {
        if (mountinfo) {
            storage_mount_prefix(&text, STORAGE_MAX_VOLUMES + 1, STORAGE_DEVFS_DEVICE, "/dev", "rw");
            storage_mount_emit(&text, "devfs devfs rw\n");
        } else storage_mount_emit(&text, "devfs /dev devfs rw 0 0\n");
    }
    if (mountinfo) {
        storage_mount_prefix(&text, STORAGE_MAX_VOLUMES + 2, STORAGE_PROCFS_DEVICE, "/proc", "ro");
        storage_mount_emit(&text, "proc proc ro\n");
    } else storage_mount_emit(&text, "proc /proc proc ro 0 0\n");
    if (mountinfo) {
        storage_mount_prefix(&text, STORAGE_MAX_VOLUMES + 3, STORAGE_SYSFS_DEVICE, "/sys", "ro");
        storage_mount_emit(&text, "sysfs sysfs ro\n");
    } else storage_mount_emit(&text,"sysfs /sys sysfs ro 0 0\n");
    kernel_execution_unlock_irqrestore(flags);
    if (out_read) *out_read = text.written;
    return 0;
}

/** @brief Read a range of the current mntent-format mount table. */
int storage_read_mounts(uint64_t offset, void *buffer, uint32_t capacity, uint32_t *out_read)
{
    return storage_read_mount_table(offset, buffer, capacity, out_read, 0);
}

/** @brief Read mountinfo with real mount parentage and stat-compatible device numbers. */
int storage_read_mountinfo(uint64_t offset, void *buffer, uint32_t capacity, uint32_t *out_read)
{
    return storage_read_mount_table(offset, buffer, capacity, out_read, 1);
}
