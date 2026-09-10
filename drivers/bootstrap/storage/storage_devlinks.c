/* GPT identities exposed as ordinary devfs symlinks. All callers hold the
 * execution/storage lock; the GPT range cache is invalidated on disk writes. */

/** @brief Enumerate a real GPT link by slot; skip absent/malformed GPT tables.
 * @param cursor In/out position over disk/partition slots.
 * @param uuid Output UUID (37 bytes).
 * @param target Output relative target (48 bytes).
 * @return One entry, zero at EOF, or a transport error.
 */
static int storage_devlink_next(uint64_t *cursor, char uuid[37], char target[48])
{
    while (*cursor < (uint64_t)g_install_disk_count * LEONOS_DISK_MAX_PARTITIONS) {
        uint32_t disk = *cursor / LEONOS_DISK_MAX_PARTITIONS;
        uint32_t part = *cursor % LEONOS_DISK_MAX_PARTITIONS;
        int ret = storage_disk_partition_uuid(disk, part, uuid);
        if (ret < 0 && ret != -2 && ret != -22) return ret;
        ++*cursor;
        if (ret < 0) continue;
        storage_format_u32(target, 48, "../../disk", disk, (int32_t)part);
        return 1;
    }
    return 0;
}

/** @brief Resolve a UUID link; reject ambiguous cloned partition identities.
 * @param path Absolute devfs path.
 * @param target Output relative target (48 bytes).
 * @return Zero, ENOENT, ENOTUNIQ, or a disk transport error.
 */
static int storage_devlink_target(const char *path, char target[48])
{
    const char *prefix = "/dev/disk/by-partuuid/";
    uint64_t cursor = 0;
    char uuid[37], candidate[48];
    int found = 0, ret;
    if (__builtin_strncmp(path, prefix, 22) || storage_strlen(path + 22) != 36) return -2;
    while ((ret = storage_devlink_next(&cursor, uuid, candidate)) > 0) {
        if (!storage_text_eq(path + 22, uuid)) continue;
        if (found) return -76; /* ENOTUNIQ: never silently select another disk. */
        storage_copy_text(target, 48, candidate);
        found = 1;
    }
    return ret < 0 ? ret : found ? 0 : -2;
}
