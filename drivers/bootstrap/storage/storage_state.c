void storage_set_io_async_context(bool enabled)
{
    /* The asynchronous path stores a single pending command, scratch DMA
     * buffer and owner PID globally.  It is safe only while there is one
     * possible caller.  On SMP every operation is completed synchronously
     * inside its storage transaction; a page fault must never inherit or
     * overwrite another CPU's resumable command state. */
    if (smp_cpu_count() > 1u) {
        storage_io_async_context = false;
        storage_io_write_started = true;
        return;
    }
    storage_io_async_context = enabled;
    storage_io_write_started = false;
}

static int storage_acquire_task_io(void)
{
    uint32_t pid;

    if (!storage_io_async_context) {
        return 0;
    }
    pid = sched_current_pid();
    if (!pid) {
        return 0;
    }
    if (storage_task_io_owner == 0 || storage_task_io_owner == pid) {
        storage_task_io_owner = pid;
        return 0;
    }
    return -LEONOS_EAGAIN;
}

void storage_release_task_io(uint32_t pid)
{
    if (pid && storage_task_io_owner == pid) {
        storage_task_io_owner = 0;
    }
}

static bool storage_async_can_yield(void)
{
    /* AHCI command descriptors, DMA staging buffers, filesystem caches and
     * the pending-command record are still single-instance state.  Allowing
     * a command to survive a syscall yield lets another CPU reuse that state
     * before the original instruction is retried.  Keep the fast retry mode
     * for the single-CPU legacy path, but use a fully synchronous transaction
     * once SMP is active. */
    return storage_io_async_context && !storage_io_write_started &&
           smp_cpu_count() <= 1u;
}

static void storage_begin_mutation(void)
{
    if (storage_io_async_context) {
        storage_io_write_started = true;
    }
}

struct fat32_dir_ref {
    uint32_t entry_cluster;
    uint32_t entry_offset;
    struct fat32_dirent dirent;
};

struct fat32_dir_span {
    uint32_t entry_cluster;
    uint32_t entry_offset;
};

static void storage_memzero(void *dst, size_t len)
{
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

static void storage_cache_invalidate(void)
{
    exfat_cache_invalidate();
    storage_fat_cache.valid = 0;
    storage_read_cache.valid = 0;
    storage_dir_lookup_cache.valid = 0;
    for (uint32_t i = 0; i < STORAGE_PATH_CACHE_ENTRIES; ++i) {
        storage_path_cache[i].valid = 0;
    }
    storage_path_cache_next = 0;
    for (uint32_t i = 0; i < STORAGE_DIR_INDEX_ENTRIES; ++i) {
        storage_dir_index[i].valid = 0;
    }
    storage_dir_index_next = 0;
    storage_dir_iter_cache.valid = 0;
    storage_write_chain_cache.valid = 0;
}

static void storage_sector_cache_invalidate(void)
{
    storage_fat_cache.valid = 0;
    storage_read_cache.valid = 0;
    storage_dir_lookup_cache.valid = 0;
    for (uint32_t i = 0; i < STORAGE_PATH_CACHE_ENTRIES; ++i) {
        storage_path_cache[i].valid = 0;
    }
    storage_path_cache_next = 0;
    for (uint32_t i = 0; i < STORAGE_DIR_INDEX_ENTRIES; ++i) {
        storage_dir_index[i].valid = 0;
    }
    storage_dir_index_next = 0;
    storage_dir_iter_cache.valid = 0;
}

static void storage_memcpy(void *dst, const void *src, size_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
}

static int storage_memcmp(const void *a, const void *b, size_t len)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < len; ++i) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}

static size_t storage_strlen(const char *s)
{
    size_t n = 0;
    while (s && s[n]) {
        ++n;
    }
    return n;
}

static void storage_copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int storage_text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int storage_text_eq_ci(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int storage_path_cache_lookup(const char *path, struct storage_node *out)
{
    if (!path || !path[0]) {
        return 0;
    }
    for (uint32_t i = 0; i < STORAGE_PATH_CACHE_ENTRIES; ++i) {
        struct storage_path_cache_entry *entry = &storage_path_cache[i];
        if (!entry->valid || entry->volume != g_active_volume ||
            !storage_text_eq_ci(entry->path, path)) {
            continue;
        }
        if (out) {
            *out = entry->node;
        }
        return 1;
    }
    return 0;
}

static void storage_path_cache_store(const char *path, const struct storage_node *node)
{
    struct storage_path_cache_entry *entry;
    if (!path || !path[0] || !node) {
        return;
    }
    entry = &storage_path_cache[storage_path_cache_next % STORAGE_PATH_CACHE_ENTRIES];
    storage_path_cache_next = (storage_path_cache_next + 1u) % STORAGE_PATH_CACHE_ENTRIES;
    entry->volume = g_active_volume;
    entry->node = *node;
    storage_copy_text(entry->path, sizeof(entry->path), path);
    entry->valid = 1;
}

static int storage_dir_index_lookup(uint32_t directory_cluster, const char *name,
                                    struct storage_node *out)
{
    if (directory_cluster < 2 || !name || !name[0]) {
        return 0;
    }
    for (uint32_t i = 0; i < STORAGE_DIR_INDEX_ENTRIES; ++i) {
        struct storage_dir_index_entry *entry = &storage_dir_index[i];
        if (!entry->valid || entry->volume != g_active_volume ||
            entry->directory_cluster != directory_cluster ||
            !storage_text_eq_ci(entry->name, name)) {
            continue;
        }
        if (out) {
            *out = entry->node;
        }
        return 1;
    }
    return 0;
}

static void storage_dir_index_store(uint32_t directory_cluster, const char *name,
                                    const struct storage_node *node)
{
    struct storage_dir_index_entry *entry;
    if (directory_cluster < 2 || !name || !name[0] || !node) {
        return;
    }
    entry = &storage_dir_index[storage_dir_index_next % STORAGE_DIR_INDEX_ENTRIES];
    storage_dir_index_next = (storage_dir_index_next + 1u) % STORAGE_DIR_INDEX_ENTRIES;
    entry->volume = g_active_volume;
    entry->directory_cluster = directory_cluster;
    entry->node = *node;
    storage_copy_text(entry->name, sizeof(entry->name), name);
    entry->valid = 1;
}

static uint64_t storage_rdtsc(void)
{
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t storage_mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static void storage_make_guid(uint8_t guid[16], uint64_t tag,
                              const struct install_disk_state *disk,
                              uint64_t sector_count)
{
    uint64_t a = storage_mix64(storage_rdtsc() ^ tag ^
                               ((uint64_t)(disk ? disk->port : 0) << 32) ^
                               sector_count);
    uint64_t b = storage_mix64(storage_rdtsc() ^ (tag << 1) ^
                               ((uint64_t)(disk ? disk->bus : 0) << 40) ^
                               ((uint64_t)(disk ? disk->slot : 0) << 24) ^
                               ((uint64_t)(disk ? disk->function : 0) << 16));
    for (uint32_t i = 0; i < 8U; ++i) {
        guid[i] = (uint8_t)(a >> (i * 8U));
        guid[8U + i] = (uint8_t)(b >> (i * 8U));
    }
    guid[6] = (uint8_t)((guid[6] & 0x0fU) | 0x40U);
    guid[8] = (uint8_t)((guid[8] & 0x3fU) | 0x80U);
}

static void storage_guid_append_hex2(char *dst, uint32_t *pos,
                                     uint32_t cap, uint8_t value)
{
    static const char hex[] = "0123456789abcdef";
    if (!dst || !pos || *pos + 2U >= cap) {
        return;
    }
    dst[(*pos)++] = hex[value >> 4];
    dst[(*pos)++] = hex[value & 0x0fU];
    dst[*pos] = 0;
}

static int storage_guid_valid(const uint8_t guid[16])
{
    uint8_t or_all = 0;
    uint8_t and_all = 0xffU;
    for (uint32_t i = 0; i < 16U; ++i) {
        or_all |= guid[i];
        and_all &= guid[i];
    }
    return or_all != 0 && and_all != 0xffU;
}

static void storage_format_guid(const uint8_t guid[16], char *out, uint32_t cap)
{
    uint32_t pos = 0;
    if (!out || cap < LEONOS_MACHINE_IDENTITY_UUID_LEN) {
        if (out && cap) {
            out[0] = 0;
        }
        return;
    }
    out[0] = 0;
    for (uint32_t i = 0; i < 16U; ++i) {
        if (i == 4U || i == 6U || i == 8U || i == 10U) {
            out[pos++] = '-';
            out[pos] = 0;
        }
        storage_guid_append_hex2(out, &pos, cap, guid[i]);
    }
}

static int storage_is_acl_metadata_name(const char *name)
{
    return storage_text_eq_ci(name, "LEONACL.SYS");
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint64_t min_u64(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

static int storage_select_volume(uint32_t volume_id)
{
    int ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (volume_id >= STORAGE_MAX_VOLUMES || !g_volumes[volume_id].ready) {
        return -2;
    }
    g_active_volume = &g_volumes[volume_id];
    return 0;
}

static int storage_select_node_volume(const struct storage_node *node,
                                      struct storage_volume **old_volume)
{
    int ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (!node || node->volume_id >= STORAGE_MAX_VOLUMES ||
        !g_volumes[node->volume_id].ready) {
        return -2;
    }
    if (old_volume) {
        *old_volume = g_active_volume;
    }
    g_active_volume = &g_volumes[node->volume_id];
    return 0;
}

static void storage_restore_volume(struct storage_volume *old_volume)
{
    if (old_volume) {
        g_active_volume = old_volume;
    }
}

static bool storage_mount_path_matches(const char *path, const char *mount_path)
{
    uint32_t length;
    if (!path || !mount_path || mount_path[0] != '/') {
        return false;
    }
    if (mount_path[1] == 0) {
        return path[0] == '/';
    }
    length = storage_strlen(mount_path);
    return storage_memcmp(path, mount_path, length) == 0 &&
           (path[length] == 0 || path[length] == '/');
}

/* Routes a canonical global path to the mounted backend with the longest
 * matching mount point.  Backends only receive paths rooted at their own
 * filesystem, never a user-visible mount prefix. */
static int storage_route_path(const char *path, struct storage_volume **out_volume,
                              char *backend_path, uint32_t backend_capacity)
{
    struct storage_volume *best = NULL;
    uint32_t best_length = 0;
    uint32_t path_length;
    uint32_t local_start;
    uint32_t out_pos = 0;
    if (!path || path[0] != '/' || !out_volume || !backend_path || backend_capacity < 2) {
        return -22;
    }
    path_length = storage_strlen(path);
    for (uint32_t i = 0; i < STORAGE_MAX_VOLUMES; ++i) {
        struct storage_volume *volume = &g_volumes[i];
        uint32_t length;
        if (!volume->ready || volume->mount_path[0] != '/' ||
            !storage_mount_path_matches(path, volume->mount_path)) {
            continue;
        }
        length = storage_strlen(volume->mount_path);
        if (!best || length > best_length) {
            best = volume;
            best_length = length;
        }
    }
    if (!best) {
        return -2;
    }
    local_start = best_length == 1 ? 1 : best_length;
    backend_path[out_pos++] = '/';
    if (path[local_start] == '/') {
        ++local_start;
    }
    while (local_start < path_length) {
        if (out_pos + 1 >= backend_capacity) {
            return -22;
        }
        backend_path[out_pos++] = path[local_start++];
    }
    backend_path[out_pos] = 0;
    *out_volume = best;
    return 0;
}

static int storage_backend_path(const char *path, char *backend_path, uint32_t backend_capacity)
{
    struct storage_volume *volume;
    int ret = storage_route_path(path, &volume, backend_path, backend_capacity);
    if (ret < 0) {
        return ret;
    }
    return storage_select_volume(volume->volume_id);
}

int storage_path_volume_id(const char *path, uint32_t *out_volume_id)
{
    char resolved[LEONOS_FS_PATH_LEN];
    char backend_path[LEONOS_FS_PATH_LEN];
    struct storage_volume *volume;
    if (!out_volume_id || storage_resolve_path("/", path, resolved, sizeof(resolved)) < 0) {
        return -22;
    }
    if (g_devfs_enabled &&
        (storage_text_eq_ci(resolved, "/dev") ||
         (storage_memcmp(resolved, "/dev/", 5u) == 0))) {
        *out_volume_id = STORAGE_VOLUME_ROOT;
        return 0;
    }
    if (storage_route_path(resolved, &volume, backend_path, sizeof(backend_path)) < 0) {
        return -2;
    }
    *out_volume_id = volume->volume_id;
    return 0;
}

static int storage_parent_path(const char *path, char *parent, uint32_t parent_cap,
                               char *name, uint32_t name_cap)
{
    char resolved[LEONOS_FS_PATH_LEN];
    uint32_t slash = 0;
    if (!path || !parent || !name || parent_cap < 2 || name_cap == 0) {
        return -22;
    }
    if (storage_resolve_path("/", path, resolved, sizeof(resolved)) < 0) {
        return -22;
    }
    for (uint32_t i = 0; resolved[i]; ++i) {
        if (resolved[i] == '/') {
            slash = i;
        }
    }
    if (resolved[slash + 1] == 0) {
        return -22;
    }
    if (slash == 0) {
        if (parent_cap < 2) {
            return -22;
        }
        parent[0] = '/';
        parent[1] = 0;
    } else {
        if (slash + 1 > parent_cap) {
            return -22;
        }
        for (uint32_t i = 0; i < slash && i + 1 < parent_cap; ++i) {
            parent[i] = resolved[i];
            parent[i + 1] = 0;
        }
    }
    storage_copy_text(name, name_cap, resolved + slash + 1);
    return 0;
}

static uint64_t cluster_to_lba(uint32_t cluster)
{
    return g_storage.esp_start_lba + g_storage.data_start_sector +
           (cluster - 2u) * g_storage.sectors_per_cluster;
}

static uint64_t fat_sector_for_cluster(uint32_t cluster)
{
    return g_storage.esp_start_lba + g_storage.fat_start_sector +
           ((cluster * 4u) / g_storage.bytes_per_sector);
}
