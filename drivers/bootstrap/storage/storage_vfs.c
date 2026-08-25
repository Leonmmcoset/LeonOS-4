bool storage_ready(void)
{
    return g_volumes[0].ready;
}

/**
 * @brief Reports the filesystem implementation backing the runtime root at runtime.
 * @return A static lowercase name suitable for boot diagnostics.
 */
const char *storage_root_filesystem_name(void)
{
    switch (g_volumes[0].filesystem) {
    case STORAGE_FILESYSTEM_EXT2:
        return "ext2";
    case STORAGE_FILESYSTEM_FAT32:
        return "fat32";
    case STORAGE_FILESYSTEM_ISO9660:
        return "iso9660";
    default:
        return "none";
    }
}

bool storage_installer_root_active(void)
{
    return g_installer_root_active != 0;
}

int storage_resolve_path(const char *cwd, const char *input, char *out, uint32_t cap)
{
    char parts[16][LEONOS_FS_NAME_LEN];
    uint32_t part_count = 0;
    const char *sources[2];
    uint32_t source_count;
    if (!input || !out || cap < 2) {
        return -22;
    }
    for (uint32_t i = 0; input[i]; ++i) {
        if (input[i] == ':' || input[i] == '\\') {
            return -22;
        }
    }
    if (input[0] == '/') {
        sources[0] = input + 1;
        source_count = 1;
    } else {
        if (!cwd || cwd[0] != '/') {
            cwd = "/";
        }
        for (uint32_t i = 0; cwd[i]; ++i) {
            if (cwd[i] == ':' || cwd[i] == '\\') {
                return -22;
            }
        }
        sources[0] = cwd + 1;
        sources[1] = input;
        source_count = 2;
    }
    for (uint32_t src_i = 0; src_i < source_count; ++src_i) {
        const char *p = sources[src_i];
        char token[LEONOS_FS_NAME_LEN];
        uint32_t pos = 0;
        while (1) {
            char ch = *p;
            if (ch == '/' || ch == 0) {
                token[pos] = 0;
                if (pos != 0) {
                    if (storage_text_eq(token, ".")) {
                    } else if (storage_text_eq(token, "..")) {
                        if (part_count) {
                            --part_count;
                        }
                    } else if (part_count < 16) {
                        storage_copy_text(parts[part_count++], sizeof(parts[0]), token);
                    } else {
                        return -22;
                    }
                }
                pos = 0;
                if (ch == 0) {
                    break;
                }
                ++p;
                continue;
            }
            if (pos + 1 >= sizeof(token)) {
                return -22;
            }
            token[pos++] = ch;
            ++p;
        }
    }

    uint32_t out_pos = 0;
    out[out_pos++] = '/';
    out[out_pos] = 0;
    for (uint32_t i = 0; i < part_count; ++i) {
        if (out_pos + storage_strlen(parts[i]) + 1 >= cap) {
            return -22;
        }
        if (out_pos > 1) {
            out[out_pos++] = '/';
        }
        for (uint32_t j = 0; parts[i][j]; ++j) {
            out[out_pos++] = parts[i][j];
        }
        out[out_pos] = 0;
    }
    return 0;
}

static int storage_lookup_path_unlocked(const char *path, struct storage_node *out)
{
    char resolved[LEONOS_FS_PATH_LEN];
    char backend_path[LEONOS_FS_PATH_LEN];
    struct storage_volume *volume;
    int ret;
    if (!storage_ready()) {
        return -2;
    }
    if (storage_resolve_path("/", path, resolved, sizeof(resolved)) < 0) {
        return -22;
    }
    if (g_devfs_enabled && storage_text_eq_ci(resolved, "/dev")) {
        if (out) {
            out->type = LEONOS_FS_TYPE_DIR;
            out->flags = STORAGE_NODE_FLAG_DEV_DIR;
            out->first_cluster = 0;
            out->volume_id = STORAGE_VOLUME_ROOT;
            out->size = 0;
        }
        return 0;
    }
    if (g_devfs_enabled && storage_text_eq_ci(resolved, "/dev/fb0")) {
        if (out) {
            out->type = LEONOS_FS_TYPE_DEVICE;
            out->flags = STORAGE_NODE_FLAG_DEV_FB0;
            out->first_cluster = 0;
            out->volume_id = STORAGE_VOLUME_ROOT;
            out->size = 0;
        }
        return 0;
    }

    ret = storage_route_path(resolved, &volume, backend_path, sizeof(backend_path));
    if (ret < 0) {
        return ret;
    }
    ret = storage_select_volume(volume->volume_id);
    if (ret < 0) {
        return ret;
    }

    if (storage_path_cache_lookup(resolved, out)) {
        return 0;
    }

    if (g_storage.filesystem == STORAGE_FILESYSTEM_EXT2) {
        ret = ext2_lookup_path(backend_path, out);
        if (ret == 0 && out) storage_path_cache_store(resolved, out);
        return ret;
    }

    struct storage_node node = {
        .type = LEONOS_FS_TYPE_DIR,
        .flags = STORAGE_NODE_FLAG_ROOT,
        .first_cluster = g_storage.filesystem == STORAGE_FILESYSTEM_ISO9660
                             ? g_storage.iso_root_extent : g_storage.root_cluster,
        .volume_id = g_storage.volume_id,
        .size = g_storage.filesystem == STORAGE_FILESYSTEM_ISO9660
                    ? g_storage.iso_root_size : 0,
    };
    const char *p = backend_path + 1;
    char name[LEONOS_FS_NAME_LEN];
    uint32_t pos = 0;
    while (1) {
        char ch = *p;
        if (ch == '/' || ch == 0) {
            name[pos] = 0;
            if (pos) {
                if (node.type != LEONOS_FS_TYPE_DIR || node.flags == STORAGE_NODE_FLAG_DEV_DIR) {
                    return -20;
                }
                int ret = g_storage.filesystem == STORAGE_FILESYSTEM_ISO9660
                              ? iso9660_find_in_dir(node.first_cluster, (uint32_t)node.size,
                                                    name, &node)
                              : fat32_find_in_dir(node.first_cluster, name, &node);
                if (ret < 0) {
                    return ret;
                }
            }
            pos = 0;
            if (ch == 0) {
                break;
            }
            ++p;
            continue;
        }
        if (pos + 1 >= sizeof(name)) {
            return -22;
        }
        char out_ch = ch;
        if (out_ch >= 'A' && out_ch <= 'Z') {
            out_ch = (char)(out_ch - 'A' + 'a');
        }
        name[pos++] = out_ch;
        ++p;
    }
    if (out) {
        *out = node;
    }
    storage_path_cache_store(resolved, &node);
    return 0;
}

int storage_lookup_path(const char *path, struct storage_node *out)
{
    int ret;
    uint64_t flags;
    /* Filesystem metadata, caches, the active-volume selector and the
     * polled AHCI command path are shared by all CPUs.  Use the reentrant
     * execution transaction shared with syscalls and page faults, so lazy
     * ELF loading cannot observe a write transaction halfway through. */
    kernel_execution_lock_irqsave(&flags);
    ret = storage_lookup_path_unlocked(path, out);
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}

static int storage_read_node_cursor_unlocked(const struct storage_node *node, uint64_t offset,
                             void *buf, uint32_t len, uint32_t *out_read,
                             struct storage_read_cursor *cursor)
{
    uint8_t *dst = (uint8_t *)buf;
    uint32_t done = 0;
    struct storage_volume *old_volume = 0;
    int ret;
    if (cursor && (!cursor->valid || cursor->offset != offset || cursor->cluster < 2u)) {
        cursor->valid = 0;
    }
    if (out_read) {
        *out_read = 0;
    }
    if (!node || !buf) {
        return -22;
    }
    if (node->type == LEONOS_FS_TYPE_DEVICE && (node->flags & STORAGE_NODE_FLAG_DEV_FB0)) {
        return -21;
    }
    if (node->type != LEONOS_FS_TYPE_FILE) {
        return -21;
    }
    if (offset >= node->size || len == 0) {
        if (cursor) {
            cursor->valid = 0;
        }
        return 0;
    }
    ret = storage_select_node_volume(node, &old_volume);
    if (ret < 0) {
        return ret;
    }
    if (len > node->size - offset) {
        len = (uint32_t)(node->size - offset);
    }

    if (g_storage.filesystem == STORAGE_FILESYSTEM_ISO9660) {
        if (cursor) {
            cursor->valid = 0;
        }
        uint64_t absolute = (uint64_t)node->first_cluster * ISO9660_BLOCK_SIZE + offset;
        uint32_t done = 0;
        while (done < len) {
            uint64_t block = absolute / ISO9660_BLOCK_SIZE;
            uint32_t block_offset = (uint32_t)(absolute % ISO9660_BLOCK_SIZE);
            uint32_t take = min_u32(ISO9660_BLOCK_SIZE - block_offset, len - done);
            ret = storage_read_iso_blocks(block, 1, storage_scratch);
            if (ret < 0) {
                storage_restore_volume(old_volume);
                return storage_read_failure(ret);
            }
            storage_memcpy(dst + done, storage_scratch + block_offset, take);
            absolute += take;
            done += take;
        }
        if (out_read) {
            *out_read = done;
        }
        storage_restore_volume(old_volume);
        return 0;
    }

    if (g_storage.filesystem == STORAGE_FILESYSTEM_EXT2) {
        if (cursor) {
            cursor->valid = 0;
        }
        ret = ext2_read_node(node, offset, buf, len, out_read);
        storage_restore_volume(old_volume);
        return ret;
    }

    uint32_t cluster = node->first_cluster;
    uint64_t skip_clusters = offset / g_storage.cluster_bytes;
    uint32_t cluster_off = (uint32_t)(offset % g_storage.cluster_bytes);
    if (cursor && cursor->valid) {
        cluster = cursor->cluster;
    } else {
        while (skip_clusters--) {
            uint32_t next = 0;
            ret = fat32_read_fat_entry(cluster, &next);
            if (ret < 0) {
                storage_restore_volume(old_volume);
                return storage_read_failure(ret);
            }
            if (next >= FAT32_EOC || next < 2u) {
                storage_restore_volume(old_volume);
                return -5;
            }
            cluster = next;
        }
    }

    while (done < len) {
        uint64_t cluster_file_offset = offset + done - cluster_off;
        uint64_t bytes_from_cluster = node->size - cluster_file_offset;
        /* Include the leading offset because cache fills are cluster-aligned. */
        uint64_t bytes_requested = len - done + cluster_off;
        uint32_t max_clusters;
        uint32_t cache_offset;
        uint32_t cache_clusters;
        uint32_t next;
        uint32_t take;
        if (bytes_from_cluster > bytes_requested) {
            bytes_from_cluster = bytes_requested;
        }
        max_clusters = (uint32_t)((bytes_from_cluster +
                                   g_storage.cluster_bytes - 1u) /
                                  g_storage.cluster_bytes);
        if (max_clusters > STORAGE_READAHEAD_SECTORS / g_storage.sectors_per_cluster) {
            max_clusters = STORAGE_READAHEAD_SECTORS / g_storage.sectors_per_cluster;
        }
        if (storage_read_cache_lookup(cluster, &cache_offset, &cache_clusters)) {
            if (cache_clusters > max_clusters) {
                cache_clusters = max_clusters;
            }
            ret = fat32_read_fat_entry(cluster + cache_clusters - 1u, &next);
            if (ret < 0) {
                storage_restore_volume(old_volume);
                return storage_read_failure(ret);
            }
        } else {
            ret = storage_read_contiguous_clusters(cluster, max_clusters,
                                                   &cache_clusters, &next);
            if (ret < 0) {
                storage_restore_volume(old_volume);
                return storage_read_failure(ret);
            }
            ret = storage_read_cache_fill(cluster, cache_clusters);
            if (ret < 0) {
                storage_restore_volume(old_volume);
                return storage_read_failure(ret);
            }
            cache_offset = 0;
        }
        take = cache_clusters * g_storage.cluster_bytes - cluster_off;
        if (take > len - done) {
            take = len - done;
        }
        storage_memcpy(dst + done, storage_read_cache_data + cache_offset + cluster_off,
                       take);
        done += take;
        if (done >= len) {
            if (cursor && offset + done < node->size) {
                uint32_t consumed = cluster_off + take;
                uint32_t advanced = consumed / g_storage.cluster_bytes;
                uint32_t next_cluster = cluster + advanced;
                if ((consumed % g_storage.cluster_bytes) == 0u &&
                    advanced >= cache_clusters) {
                    next_cluster = next;
                }
                if (next_cluster < 2u || next_cluster >= FAT32_EOC) {
                    cursor->valid = 0;
                    storage_restore_volume(old_volume);
                    return -5;
                }
                cursor->offset = offset + done;
                cursor->cluster = next_cluster;
                cursor->valid = 1;
            } else if (cursor) {
                cursor->valid = 0;
            }
            break;
        }
        if (next >= FAT32_EOC) {
            if (cursor) {
                cursor->valid = 0;
            }
            storage_restore_volume(old_volume);
            return -5;
        }
        if (next < 2u) {
            if (cursor) {
                cursor->valid = 0;
            }
            storage_restore_volume(old_volume);
            return -5;
        }
        if (cursor) {
            cursor->offset = offset + done;
            cursor->cluster = next;
            cursor->valid = 1;
        }
        cluster = next;
        cluster_off = 0;
    }
    if (out_read) {
        *out_read = done;
    }
    storage_restore_volume(old_volume);
    return 0;
}

int storage_read_node(const struct storage_node *node, uint64_t offset,
                      void *buf, uint32_t len, uint32_t *out_read)
{
    return storage_read_node_cursor(node, offset, buf, len, out_read, NULL);
}

int storage_read_node_cursor(const struct storage_node *node, uint64_t offset,
                             void *buf, uint32_t len, uint32_t *out_read,
                             struct storage_read_cursor *cursor)
{
    int ret;
    uint64_t flags;
    kernel_execution_lock_irqsave(&flags);
    ret = storage_read_node_cursor_unlocked(node, offset, buf, len, out_read, cursor);
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}

static int storage_readdir_node_unlocked(const struct storage_node *node, uint64_t *cursor,
                         struct leonos_dir_entry *entry)
{
    struct storage_volume *old_volume = 0;
    int ret;
    if (!node || !cursor || !entry) {
        return -22;
    }
    if (node->type != LEONOS_FS_TYPE_DIR) {
        return -20;
    }
    if (node->flags & STORAGE_NODE_FLAG_DEV_DIR) {
        if (*cursor == 0) {
            entry->type = LEONOS_FS_TYPE_DEVICE;
            storage_copy_text(entry->name, sizeof(entry->name), "fb0");
            *cursor = 1;
            return 1;
        }
        return 0;
    }
    ret = storage_select_node_volume(node, &old_volume);
    if (ret < 0) {
        return ret;
    }
    if (g_storage.filesystem == STORAGE_FILESYSTEM_ISO9660) {
        ret = iso9660_iter_dir_entry(node->first_cluster, (uint32_t)node->size,
                                     *cursor, entry);
        storage_restore_volume(old_volume);
        if (ret == 0) {
            ++(*cursor);
            return 1;
        }
        if (ret == -2) {
            return 0;
        }
        return ret;
    }
    if (g_storage.filesystem == STORAGE_FILESYSTEM_EXT2) {
        ret = ext2_iter_dir_entry(node->first_cluster, *cursor, entry);
        storage_restore_volume(old_volume);
        if (ret == 0) {
            ++(*cursor);
            return 1;
        }
        return ret == -2 ? 0 : ret;
    }
    ret = fat32_iter_dir_entry(node->first_cluster, *cursor, entry);
    storage_restore_volume(old_volume);
    if (ret == 0) {
        ++(*cursor);
        return 1;
    }
    if (ret == -2) {
        return 0;
    }
    return ret;
}

int storage_readdir_node(const struct storage_node *node, uint64_t *cursor,
                         struct leonos_dir_entry *entry)
{
    int ret;
    uint64_t flags;
    kernel_execution_lock_irqsave(&flags);
    ret = storage_readdir_node_unlocked(node, cursor, entry);
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}

int storage_read_file(const char *path, const void **out_data, size_t *out_len)
{
    struct storage_node node;
    uint64_t pages;
    uint64_t phys;
    uint32_t got = 0;
    if (!out_data || !out_len) {
        return -22;
    }
    *out_data = 0;
    *out_len = 0;
    int ret = storage_lookup_path(path, &node);
    if (ret < 0) {
        return ret;
    }
    if (node.type != LEONOS_FS_TYPE_FILE) {
        return -21;
    }
    /* storage_read_node() and the page allocator use 32-bit lengths. Do not
     * silently truncate a larger FAT32 file into a short executable/image. */
    if (node.size > 0xffffffffULL ||
        (node.size + 4095ULL) / 4096ULL > 0xffffffffULL) {
        return -28;
    }
    pages = (node.size + 4095u) / 4096u;
    phys = pages ? mm_alloc_pages((uint32_t)pages) : mm_alloc_page();
    if (!phys) {
        return -12;
    }
    ret = storage_read_node(&node, 0, (void *)(uintptr_t)phys, (uint32_t)node.size, &got);
    if (ret < 0 || got != node.size) {
        mm_free_pages(phys, pages ? (uint32_t)pages : 1u);
        return ret < 0 ? ret : -5;
    }
    *out_data = (const void *)(uintptr_t)phys;
    *out_len = node.size;
    return 0;
}

int storage_write_node(const char *path, uint64_t offset,
                       const void *buf, uint32_t len, uint32_t *out_written)
{
    struct storage_node node;
    uint64_t end64;
    uint32_t total_len;
    uint32_t final_len;
    uint32_t old_count = 0;
    uint32_t clusters_needed;
    uint32_t first_cluster;
    const uint8_t *src = (const uint8_t *)buf;
    uint64_t pos;
    uint32_t remaining;
    uint32_t read_len = 0;
    uint64_t phys = 0;
    uint32_t pages = 0;
    int ret;
    if (out_written) {
        *out_written = 0;
    }
    if (!path || (!buf && len != 0)) {
        return -22;
    }
    ret = storage_lookup_path(path, &node);
    if (ret < 0) {
        return ret;
    }
    if (node.type != LEONOS_FS_TYPE_FILE) {
        return -21;
    }
    if (g_storage.filesystem == STORAGE_FILESYSTEM_EXT2) {
        storage_begin_mutation();
        /* storage_lookup_path() already selected /target's ext2 volume and
         * returned its inode. Passing a backend-local string through the
         * global router again would reinterpret /docs/foo as the installer
         * FAT root rather than /target/docs/foo. */
        return ext2_write_node(&node, offset, buf, len, out_written);
    }
    if (g_storage.filesystem != STORAGE_FILESYSTEM_FAT32) {
        return -30;
    }
    if (node.size > 0xffffffffULL) {
        return -28;
    }
    end64 = offset + len;
    if (end64 > 0xffffffffu || end64 < offset) {
        return -28;
    }
    total_len = (uint32_t)end64;
    if (len == 0) {
        return 0;
    }
    /* The remaining path modifies file data and its directory entry. Keep
     * pre-read/modify/write sequences in one non-replayable transaction. */
    storage_begin_mutation();
    if (offset <= node.size) {
        char parent[LEONOS_FS_PATH_LEN];
        char name[LEONOS_FS_NAME_LEN];
        struct storage_node parent_node;
        struct fat32_dir_ref ref;

        final_len = total_len > node.size ? total_len : (uint32_t)node.size;
        clusters_needed = final_len ?
            (final_len + g_storage.cluster_bytes - 1u) / g_storage.cluster_bytes : 0;
        if (clusters_needed > FAT32_MAX_FILE_CLUSTERS) {
            return -28;
        }
        uint8_t cached_append =
            offset == node.size && node.first_cluster >= 2 &&
            storage_write_chain_cache.valid &&
            storage_write_chain_cache.volume == g_active_volume &&
            storage_write_chain_cache.first_cluster == node.first_cluster &&
            storage_write_chain_cache.size == (uint32_t)node.size &&
            storage_text_eq(storage_write_chain_cache.path, path) &&
            storage_write_chain_cache.count > 0 &&
            clusters_needed >= storage_write_chain_cache.count;
        if (node.first_cluster >= 2) {
            if (cached_append) {
                old_count = storage_write_chain_cache.count;
                /* Only the last cluster is needed when the append stays
                 * inside the existing chain; restore that cached tail. */
                storage_old_chain[old_count - 1u] = storage_write_chain_cache.tail;
            } else {
                ret = fat32_collect_chain(node.first_cluster, storage_old_chain, &old_count);
                if (ret < 0) {
                    return ret;
                }
            }
        }
        /* Any later I/O failure can leave the chain partly changed. Rebuild
         * the append hint only after the directory entry commits below. */
        storage_write_chain_cache.valid = 0;
        for (uint32_t i = old_count; i < clusters_needed; ++i) {
            ret = fat32_find_free_cluster(&storage_old_chain[i]);
            if (ret < 0) {
                return ret;
            }
            if (fat32_write_fat_entry(storage_old_chain[i], FAT32_EOC) < 0) {
                return -5;
            }
            if (fat32_note_allocated() < 0) {
                return -5;
            }
        }
        if (cached_append) {
            if (clusters_needed > old_count) {
                if (fat32_write_fat_entry(storage_old_chain[old_count - 1u],
                                          storage_old_chain[old_count]) < 0) {
                    return -5;
                }
                for (uint32_t i = old_count; i < clusters_needed; ++i) {
                    uint32_t next = i + 1u < clusters_needed
                                        ? storage_old_chain[i + 1u]
                                        : FAT32_EOC;
                    if (fat32_write_fat_entry(storage_old_chain[i], next) < 0) {
                        return -5;
                    }
                }
            }
        } else if (offset == node.size && old_count && clusters_needed > old_count) {
            if (fat32_write_fat_entry(storage_old_chain[old_count - 1u],
                                      storage_old_chain[old_count]) < 0) {
                return -5;
            }
            for (uint32_t i = old_count; i < clusters_needed; ++i) {
                uint32_t next = i + 1u < clusters_needed
                                    ? storage_old_chain[i + 1u]
                                    : FAT32_EOC;
                if (fat32_write_fat_entry(storage_old_chain[i], next) < 0) {
                    return -5;
                }
            }
        } else {
            for (uint32_t i = 0; i < clusters_needed; ++i) {
                uint32_t next = i + 1u < clusters_needed
                                    ? storage_old_chain[i + 1u]
                                    : FAT32_EOC;
                if (fat32_write_fat_entry(storage_old_chain[i], next) < 0) {
                    return -5;
                }
            }
        }

        pos = offset;
        remaining = len;
        while (remaining) {
            uint32_t cluster_index = (uint32_t)(pos / g_storage.cluster_bytes);
            uint32_t cluster_off = (uint32_t)(pos % g_storage.cluster_bytes);
            uint32_t take = min_u32(g_storage.cluster_bytes - cluster_off, remaining);
            if (cluster_index >= clusters_needed) {
                return -5;
            }
            if (cluster_off != 0 || take != g_storage.cluster_bytes) {
                if (cluster_index >= old_count) {
                    /* Newly allocated clusters have no visible contents yet.
                     * Zero only the bytes this partial write does not cover;
                     * avoid an otherwise redundant full-cluster DMA write. */
                    storage_memzero(storage_cluster_buf, g_storage.cluster_bytes);
                } else if (fat32_read_cluster(storage_old_chain[cluster_index],
                                               storage_cluster_buf) < 0) {
                    return -5;
                }
            }
            storage_memcpy(storage_cluster_buf + cluster_off, src, take);
            if (fat32_write_cluster(storage_old_chain[cluster_index], storage_cluster_buf) < 0) {
                return -5;
            }
            pos += take;
            src += take;
            remaining -= take;
        }

        first_cluster = clusters_needed ?
            (cached_append ? node.first_cluster : storage_old_chain[0]) : 0;
        if (storage_parent_path(path, parent, sizeof(parent), name, sizeof(name)) < 0) {
            return -22;
        }
        ret = storage_lookup_path(parent, &parent_node);
        if (ret < 0) {
            return ret;
        }
        ret = fat32_find_dirent_ref_in_dir(parent_node.first_cluster, name, &ref);
        if (ret < 0) {
            return ret;
        }
        ref.dirent.first_cluster_hi = (uint16_t)(first_cluster >> 16);
        ref.dirent.first_cluster_lo = (uint16_t)(first_cluster & 0xffffu);
        ref.dirent.size = final_len;
        if (fat32_update_dirent(&ref) < 0) {
            return -5;
        }
        if (out_written) {
            *out_written = len;
        }
        storage_write_chain_cache.volume = g_active_volume;
        storage_write_chain_cache.first_cluster = first_cluster;
        storage_write_chain_cache.size = final_len;
        storage_write_chain_cache.count = clusters_needed;
        storage_write_chain_cache.tail = clusters_needed ?
            storage_old_chain[clusters_needed - 1u] : 0;
        storage_copy_text(storage_write_chain_cache.path,
                          sizeof(storage_write_chain_cache.path), path);
        storage_write_chain_cache.valid = 1;
        return 0;
    }

    if (g_storage.cluster_bytes == 0 ||
        (uint64_t)total_len > (uint64_t)FAT32_MAX_FILE_CLUSTERS *
                              g_storage.cluster_bytes) {
        return -28;
    }
    pages = total_len ? (uint32_t)((total_len + 4095u) / 4096u) : 1u;
    phys = mm_alloc_pages(pages);
    if (!phys) {
        return -12;
    }
    if (node.size) {
        ret = storage_read_node(&node, 0, (void *)(uintptr_t)phys, (uint32_t)node.size, &read_len);
        if (ret < 0 || read_len != node.size) {
            mm_free_pages(phys, pages);
            return ret < 0 ? ret : -5;
        }
    }
    if (offset > node.size) {
        storage_memzero((uint8_t *)(uintptr_t)phys + node.size, (uint32_t)offset - (uint32_t)node.size);
    }
    if (len) {
        storage_memcpy((uint8_t *)(uintptr_t)phys + offset, buf, len);
    }
    ret = storage_write_file(path, (const void *)(uintptr_t)phys, total_len);
    mm_free_pages(phys, pages);
    if (ret < 0) {
        return ret;
    }
    if (out_written) {
        *out_written = len;
    }
    storage_write_chain_cache.valid = 0;
    return 0;
}

int storage_write_file(const char *path, const void *buf, uint32_t len)
{
    char resolved[LEONOS_FS_PATH_LEN];
    char backend_path[LEONOS_FS_PATH_LEN];
    char parent[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct storage_node parent_node;
    struct storage_node existing;
    struct fat32_dir_ref ref;
    struct fat32_dir_span span;
    uint8_t short_name[11];
    uint8_t need_lfn = 0;
    uint32_t clusters_needed = 0;
    uint32_t old_count = 0;
    uint32_t data_written = 0;
    uint32_t start_cluster = 0;
    uint8_t creating = 0;
    int ret;
    if (!path || (!buf && len != 0)) {
        return -22;
    }
    storage_write_chain_cache.valid = 0;
    if (!storage_ready()) {
        return -2;
    }
    if (storage_resolve_path("/", path, resolved, sizeof(resolved)) < 0) {
        return -22;
    }
    if (storage_parent_path(resolved, parent, sizeof(parent), name, sizeof(name)) < 0) {
        return -22;
    }
    ret = storage_lookup_path(parent, &parent_node);
    if (ret < 0) {
        return ret;
    }
    if (parent_node.type != LEONOS_FS_TYPE_DIR) {
        return -20;
    }
    if (g_storage.filesystem == STORAGE_FILESYSTEM_EXT2) {
        if (storage_backend_path(resolved, backend_path, sizeof(backend_path)) < 0) {
            return -22;
        }
        storage_begin_mutation();
        return ext2_write_file(backend_path, buf, len);
    }
    if (g_storage.filesystem != STORAGE_FILESYSTEM_FAT32) {
        return -30;
    }
    if (parent_node.flags & STORAGE_NODE_FLAG_DEV_DIR) {
        return -21;
    }
    ret = fat32_validate_name(name);
    if (ret < 0) {
        return ret;
    }
    ret = fat32_make_short_name(name, short_name);
    if (ret == 0) {
        char rendered[LEONOS_FS_NAME_LEN];
        uint32_t pos = 0;
        for (uint32_t i = 0; i < 8 && short_name[i] != ' '; ++i) {
            char ch = (char)short_name[i];
            if (ch >= 'A' && ch <= 'Z') {
                ch = (char)(ch - 'A' + 'a');
            }
            if (pos + 1 < sizeof(rendered)) {
                rendered[pos++] = ch;
            }
        }
        if (short_name[8] != ' ' && pos + 1 < sizeof(rendered)) {
            rendered[pos++] = '.';
        }
        for (uint32_t i = 8; i < 11 && short_name[i] != ' '; ++i) {
            char ch = (char)short_name[i];
            if (ch >= 'A' && ch <= 'Z') {
                ch = (char)(ch - 'A' + 'a');
            }
            if (pos + 1 < sizeof(rendered)) {
                rendered[pos++] = ch;
            }
        }
        rendered[pos] = 0;
        need_lfn = storage_text_eq(name, rendered) ? 0u : 1u;
    } else {
        need_lfn = 1;
        ret = fat32_make_short_alias(parent_node.first_cluster, name, short_name);
        if (ret < 0) {
            return ret;
        }
    }

    ret = storage_lookup_path(resolved, &existing);
    if (ret == 0) {
        if (existing.type != LEONOS_FS_TYPE_FILE) {
            return -21;
        }
        ret = fat32_find_dirent_ref_in_dir(parent_node.first_cluster, name, &ref);
        if (ret < 0) {
            return ret;
        }
    } else if (ret == -2) {
        uint32_t lfn_count = need_lfn ? fat32_lfn_entry_count(name) : 0;
        uint32_t slot_count = lfn_count + 1u;
        if (lfn_count > 20u) {
            return -22;
        }
        creating = 1;
        ret = fat32_find_free_dirent_span(parent_node.first_cluster, slot_count, &span);
        if (ret < 0) {
            return ret;
        }
        storage_memzero(&ref, sizeof(ref));
        ref.entry_cluster = span.entry_cluster;
        ref.entry_offset = span.entry_offset + lfn_count * sizeof(struct fat32_dirent);
        storage_memzero(&ref.dirent, sizeof(ref.dirent));
        storage_memcpy(ref.dirent.name, short_name, 11);
        ref.dirent.attr = storage_is_acl_metadata_name(name)
                               ? (FAT32_ATTR_HIDDEN | FAT32_ATTR_SYSTEM | FAT32_ATTR_ARCHIVE)
                               : FAT32_ATTR_ARCHIVE;
        existing.first_cluster = 0;
        existing.size = 0;
    } else {
        return ret;
    }

    /* Empty replacement only needs the chain head. Avoid collecting the
     * entire old chain before immediately freeing it; on a large file that
     * otherwise performs two full FAT walks during O_TRUNC. */
    if (existing.first_cluster >= 2 && len != 0) {
        ret = fat32_collect_chain(existing.first_cluster, storage_old_chain, &old_count);
        if (ret < 0) {
            return ret;
        }
    }

    /* All discovery above is replayable. The work below changes FAT chains,
     * file clusters or directory entries, so its supporting reads must not
     * return EAGAIN and be misreported to user space as EIO. */
    storage_begin_mutation();

    if (len) {
        clusters_needed = (len + g_storage.cluster_bytes - 1u) / g_storage.cluster_bytes;
        if (clusters_needed > FAT32_MAX_FILE_CLUSTERS) {
            return -28;
        }
        for (uint32_t i = 0; i < clusters_needed; ++i) {
            if (i < old_count) {
                storage_new_chain[i] = storage_old_chain[i];
            } else {
                ret = fat32_find_free_cluster(&storage_new_chain[i]);
                if (ret < 0) {
                    return ret;
                }
                if (fat32_write_fat_entry(storage_new_chain[i], FAT32_EOC) < 0) {
                    return -5;
                }
                if (fat32_note_allocated() < 0) {
                    return -5;
                }
            }
        }
        for (uint32_t i = 0; i < clusters_needed; ++i) {
            uint32_t next = (i + 1u < clusters_needed) ? storage_new_chain[i + 1u] : FAT32_EOC;
            if (fat32_write_fat_entry(storage_new_chain[i], next) < 0) {
                return -5;
            }
        }
        start_cluster = storage_new_chain[0];
        for (uint32_t i = 0; i < clusters_needed; ++i) {
            uint32_t take = min_u32(g_storage.cluster_bytes, len - data_written);
            storage_memzero(storage_cluster_buf, g_storage.cluster_bytes);
            storage_memcpy(storage_cluster_buf, (const uint8_t *)buf + data_written, take);
            if (fat32_write_cluster(storage_new_chain[i], storage_cluster_buf) < 0) {
                return -5;
            }
            data_written += take;
        }
    }

    if (len == 0 && existing.first_cluster >= 2) {
        if (fat32_free_chain(existing.first_cluster) < 0) {
            return -5;
        }
    } else if (old_count > clusters_needed) {
        if (fat32_free_chain(storage_old_chain[clusters_needed]) < 0) {
            return -5;
        }
    }

    ref.dirent.first_cluster_hi = (uint16_t)(start_cluster >> 16);
    ref.dirent.first_cluster_lo = (uint16_t)(start_cluster & 0xffffu);
    ref.dirent.size = len;
    if (creating) {
        uint32_t lfn_count = need_lfn ? fat32_lfn_entry_count(name) : 0;
        if (fat32_read_cluster(span.entry_cluster, storage_cluster_buf) < 0) {
            return -5;
        }
        if (need_lfn) {
            uint16_t utf16_name[260];
            uint32_t name_len = fat32_utf16_name(name, utf16_name,
                                                 sizeof(utf16_name) / sizeof(utf16_name[0]));
            uint8_t checksum = fat32_short_name_checksum(short_name);
            for (uint32_t i = 0; i < lfn_count; ++i) {
                struct fat32_lfn *lfn = (struct fat32_lfn *)(void *)(storage_cluster_buf +
                    span.entry_offset + i * sizeof(struct fat32_dirent));
                uint32_t part = lfn_count - i - 1u;
                fat32_fill_lfn_entry(lfn, utf16_name, name_len, part, lfn_count, checksum);
            }
        }
        *(struct fat32_dirent *)(void *)(storage_cluster_buf + ref.entry_offset) = ref.dirent;
        if (fat32_write_cluster(span.entry_cluster, storage_cluster_buf) < 0) {
            return -5;
        }
    } else if (fat32_update_dirent(&ref) < 0) {
        return -5;
    }
    return 0;
}

int storage_truncate_file(const char *path, uint64_t length)
{
    struct storage_node node;
    uint64_t phys;
    uint32_t pages;
    uint32_t got = 0;
    uint32_t target;
    int ret;

    if (!path || length > 0xffffffffULL) {
        return -22;
    }
    ret = storage_lookup_path(path, &node);
    if (ret < 0) {
        return ret;
    }
    if (node.type != LEONOS_FS_TYPE_FILE) {
        return -21;
    }
    if (g_storage.filesystem == STORAGE_FILESYSTEM_EXT2) {
        storage_begin_mutation();
        return ext2_truncate_file(&node, length);
    }
    if (g_storage.filesystem != STORAGE_FILESYSTEM_FAT32) {
        return -30;
    }
    target = (uint32_t)length;
    if (target == node.size) {
        return 0;
    }
    pages = target ? (target + 4095u) / 4096u : 1u;
    phys = mm_alloc_pages(pages);
    if (!phys) {
        return -12;
    }
    storage_memzero((void *)(uintptr_t)phys, pages * 4096u);
    if (node.size && target) {
        uint32_t copy = node.size < target ? (uint32_t)node.size : target;
        ret = storage_read_node(&node, 0, (void *)(uintptr_t)phys, copy, &got);
        if (ret < 0 || got != copy) {
            mm_free_pages(phys, pages);
            return ret < 0 ? ret : -5;
        }
    }
    ret = storage_write_file(path, (const void *)(uintptr_t)phys, target);
    mm_free_pages(phys, pages);
    return ret;
}

int storage_mkdir(const char *path)
{
    char resolved[LEONOS_FS_PATH_LEN];
    char backend_path[LEONOS_FS_PATH_LEN];
    char parent[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct storage_node parent_node;
    struct storage_node existing;
    uint32_t cluster = 0;
    int ret;
    if (!path) {
        return -22;
    }
    if (!storage_ready()) {
        return -2;
    }
    if (storage_resolve_path("/", path, resolved, sizeof(resolved)) < 0 ||
        storage_parent_path(resolved, parent, sizeof(parent), name, sizeof(name)) < 0) {
        return -22;
    }
    ret = storage_lookup_path(parent, &parent_node);
    if (ret < 0) {
        return ret;
    }
    if (parent_node.type != LEONOS_FS_TYPE_DIR) {
        return -20;
    }
    if (g_storage.filesystem == STORAGE_FILESYSTEM_EXT2) {
        if (storage_backend_path(resolved, backend_path, sizeof(backend_path)) < 0) {
            return -22;
        }
        storage_begin_mutation();
        return ext2_mkdir(backend_path);
    }
    if (g_storage.filesystem != STORAGE_FILESYSTEM_FAT32) {
        return -30;
    }
    if (parent_node.flags & STORAGE_NODE_FLAG_DEV_DIR) {
        return -21;
    }
    ret = fat32_validate_name(name);
    if (ret < 0) {
        return ret;
    }
    ret = storage_lookup_path(resolved, &existing);
    if (ret == 0) {
        return -17;
    }
    if (ret != -2) {
        return ret;
    }
    ret = fat32_find_free_cluster(&cluster);
    if (ret < 0) {
        return ret;
    }
    storage_begin_mutation();
    if (fat32_write_fat_entry(cluster, FAT32_EOC) < 0) {
        return -5;
    }
    if (fat32_note_allocated() < 0) {
        return -5;
    }
    storage_memzero(storage_cluster_buf, g_storage.cluster_bytes);
    {
        struct fat32_dirent *dot = (struct fat32_dirent *)(void *)storage_cluster_buf;
        struct fat32_dirent *dotdot =
            (struct fat32_dirent *)(void *)(storage_cluster_buf + sizeof(struct fat32_dirent));
        storage_memzero(dot, sizeof(*dot));
        storage_memzero(dotdot, sizeof(*dotdot));
        dot->name[0] = '.';
        for (uint32_t i = 1; i < 11; ++i) {
            dot->name[i] = ' ';
        }
        dot->attr = FAT32_ATTR_DIRECTORY;
        dot->first_cluster_hi = (uint16_t)(cluster >> 16);
        dot->first_cluster_lo = (uint16_t)(cluster & 0xffffu);
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
        for (uint32_t i = 2; i < 11; ++i) {
            dotdot->name[i] = ' ';
        }
        dotdot->attr = FAT32_ATTR_DIRECTORY;
        /* FAT32 encodes the parent of a root child as cluster zero, not the
         * root directory's physical cluster number. */
        uint32_t dotdot_cluster = (parent_node.flags & STORAGE_NODE_FLAG_ROOT)
                                      ? 0u : parent_node.first_cluster;
        dotdot->first_cluster_hi = (uint16_t)(dotdot_cluster >> 16);
        dotdot->first_cluster_lo = (uint16_t)(dotdot_cluster & 0xffffu);
    }
    if (fat32_write_cluster(cluster, storage_cluster_buf) < 0) {
        (void)fat32_write_fat_entry(cluster, 0);
        (void)fat32_note_freed();
        return -5;
    }
    ret = fat32_create_dirent(parent_node.first_cluster, name, FAT32_ATTR_DIRECTORY, cluster, 0);
    if (ret < 0) {
        (void)fat32_free_chain(cluster);
        return ret;
    }
    return 0;
}

int storage_unlink(const char *path)
{
    char resolved[LEONOS_FS_PATH_LEN];
    char backend_path[LEONOS_FS_PATH_LEN];
    char parent[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct storage_node parent_node;
    struct storage_node node;
    struct fat32_dirent deleted;
    uint32_t first_cluster;
    int ret;
    if (!path) {
        return -22;
    }
    if (!storage_ready()) {
        return -2;
    }
    storage_write_chain_cache.valid = 0;
    if (storage_resolve_path("/", path, resolved, sizeof(resolved)) < 0 ||
        storage_parent_path(resolved, parent, sizeof(parent), name, sizeof(name)) < 0) {
        return -22;
    }
    ret = storage_lookup_path(parent, &parent_node);
    if (ret < 0) {
        return ret;
    }
    if (parent_node.type != LEONOS_FS_TYPE_DIR || (parent_node.flags & STORAGE_NODE_FLAG_DEV_DIR)) {
        return -20;
    }
    ret = storage_lookup_path(resolved, &node);
    if (ret < 0) {
        return ret;
    }
    if (g_storage.filesystem == STORAGE_FILESYSTEM_EXT2) {
        if (node.type == LEONOS_FS_TYPE_DIR) return -21;
        if (storage_backend_path(resolved, backend_path, sizeof(backend_path)) < 0) {
            return -22;
        }
        storage_begin_mutation();
        return ext2_unlink(backend_path);
    }
    if (g_storage.filesystem != STORAGE_FILESYSTEM_FAT32) {
        return -30;
    }
    if (node.type == LEONOS_FS_TYPE_DIR) {
        return -21;
    }
    storage_begin_mutation();
    ret = fat32_delete_dirent(parent_node.first_cluster, name, &deleted);
    if (ret < 0) {
        return ret;
    }
    first_cluster = ((uint32_t)deleted.first_cluster_hi << 16) | deleted.first_cluster_lo;
    if (first_cluster >= 2) {
        ret = fat32_free_chain(first_cluster);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

int storage_write_boot_esp_file(const char *path, const void *buf, uint32_t len)
{
    if (!path || !storage_mount_path_matches(path, "/boot")) {
        return -22;
    }
    (void)storage_mkdir("/boot/system");
    (void)storage_mkdir("/boot/system/state");
    return storage_write_file(path, buf, len);
}

int storage_unlink_boot_esp_file(const char *path)
{
    int ret;
    if (!path || !storage_mount_path_matches(path, "/boot")) {
        return -22;
    }
    ret = storage_unlink(path);
    return ret == -2 ? 0 : ret;
}

int storage_rmdir(const char *path)
{
    char resolved[LEONOS_FS_PATH_LEN];
    char backend_path[LEONOS_FS_PATH_LEN];
    char parent[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct storage_node parent_node;
    struct storage_node node;
    struct fat32_dirent deleted;
    int empty;
    int ret;
    if (!path) {
        return -22;
    }
    if (!storage_ready()) {
        return -2;
    }
    storage_write_chain_cache.valid = 0;
    if (storage_resolve_path("/", path, resolved, sizeof(resolved)) < 0 ||
        storage_parent_path(resolved, parent, sizeof(parent), name, sizeof(name)) < 0) {
        return -22;
    }
    if (storage_text_eq_ci(resolved, "/") ||
        (g_devfs_enabled && storage_text_eq_ci(resolved, "/dev"))) {
        return -22;
    }
    ret = storage_lookup_path(parent, &parent_node);
    if (ret < 0) {
        return ret;
    }
    if (parent_node.type != LEONOS_FS_TYPE_DIR || (parent_node.flags & STORAGE_NODE_FLAG_DEV_DIR)) {
        return -20;
    }
    ret = storage_lookup_path(resolved, &node);
    if (ret < 0) {
        return ret;
    }
    if (g_storage.filesystem == STORAGE_FILESYSTEM_EXT2) {
        if (node.type != LEONOS_FS_TYPE_DIR) return -20;
        if (storage_backend_path(resolved, backend_path, sizeof(backend_path)) < 0) {
            return -22;
        }
        storage_begin_mutation();
        return ext2_rmdir(backend_path);
    }
    if (g_storage.filesystem != STORAGE_FILESYSTEM_FAT32) {
        return -30;
    }
    if (node.type != LEONOS_FS_TYPE_DIR || (node.flags & STORAGE_NODE_FLAG_DEV_DIR)) {
        return -20;
    }
    empty = fat32_dir_is_empty(node.first_cluster);
    if (empty < 0) {
        return empty;
    }
    if (!empty) {
        return -39;
    }
    storage_begin_mutation();
    ret = fat32_delete_acl_metadata_file(node.first_cluster);
    if (ret < 0) {
        return ret;
    }
    ret = fat32_delete_dirent(parent_node.first_cluster, name, &deleted);
    if (ret < 0) {
        return ret;
    }
    if (node.first_cluster >= 2) {
        ret = fat32_free_chain(node.first_cluster);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

int storage_rename(const char *old_path, const char *new_path)
{
    char old_resolved[LEONOS_FS_PATH_LEN];
    char new_resolved[LEONOS_FS_PATH_LEN];
    char old_backend_path[LEONOS_FS_PATH_LEN];
    char new_backend_path[LEONOS_FS_PATH_LEN];
    char old_parent[LEONOS_FS_PATH_LEN];
    char new_parent[LEONOS_FS_PATH_LEN];
    char old_name[LEONOS_FS_NAME_LEN];
    char new_name[LEONOS_FS_NAME_LEN];
    struct storage_node parent_node;
    struct storage_node node;
    struct storage_node existing;
    struct fat32_dirent deleted;
    uint32_t first_cluster;
    int ret;
    if (!old_path || !new_path) {
        return -22;
    }
    if (!storage_ready()) {
        return -2;
    }
    if (storage_resolve_path("/", old_path, old_resolved, sizeof(old_resolved)) < 0 ||
        storage_resolve_path("/", new_path, new_resolved, sizeof(new_resolved)) < 0 ||
        storage_parent_path(old_resolved, old_parent, sizeof(old_parent), old_name, sizeof(old_name)) < 0 ||
        storage_parent_path(new_resolved, new_parent, sizeof(new_parent), new_name, sizeof(new_name)) < 0) {
        return -22;
    }
    if (!storage_text_eq_ci(old_parent, new_parent)) {
        return -22;
    }
    if (storage_text_eq_ci(old_name, new_name)) {
        return 0;
    }
    ret = fat32_validate_name(new_name);
    if (ret < 0) {
        return ret;
    }
    ret = storage_lookup_path(old_parent, &parent_node);
    if (ret < 0) {
        return ret;
    }
    if (parent_node.type != LEONOS_FS_TYPE_DIR || (parent_node.flags & STORAGE_NODE_FLAG_DEV_DIR)) {
        return -20;
    }
    if (g_storage.filesystem == STORAGE_FILESYSTEM_EXT2) {
        struct storage_volume *old_volume;
        struct storage_volume *new_volume;
        if (storage_route_path(old_resolved, &old_volume, old_backend_path,
                               sizeof(old_backend_path)) < 0 ||
            storage_route_path(new_resolved, &new_volume, new_backend_path,
                               sizeof(new_backend_path)) < 0 ||
            old_volume->volume_id != new_volume->volume_id) {
            return -18;
        }
        storage_begin_mutation();
        return ext2_rename(old_backend_path, new_backend_path);
    }
    if (g_storage.filesystem != STORAGE_FILESYSTEM_FAT32) {
        return -30;
    }
    ret = storage_lookup_path(old_resolved, &node);
    if (ret < 0) {
        return ret;
    }
    ret = storage_lookup_path(new_resolved, &existing);
    if (ret == 0) {
        return -17;
    }
    if (ret != -2) {
        return ret;
    }
    first_cluster = node.first_cluster;
    storage_begin_mutation();
    ret = fat32_create_dirent(parent_node.first_cluster, new_name,
                              node.type == LEONOS_FS_TYPE_DIR ? FAT32_ATTR_DIRECTORY : FAT32_ATTR_ARCHIVE,
                              first_cluster, node.type == LEONOS_FS_TYPE_FILE ? (uint32_t)node.size : 0);
    if (ret < 0) {
        return ret;
    }
    ret = fat32_delete_dirent(parent_node.first_cluster, old_name, &deleted);
    if (ret < 0) {
        (void)fat32_delete_dirent(parent_node.first_cluster, new_name, 0);
        return ret;
    }
    return 0;
}

int storage_list_dir(const char *path, struct leonos_dir_entry *entries,
                     uint32_t capacity, uint32_t *out_count)
{
    struct storage_node node;
    uint64_t cursor = 0;
    uint32_t count = 0;
    if (!out_count) {
        return -22;
    }
    *out_count = 0;
    int ret = storage_lookup_path(path, &node);
    if (ret < 0) {
        return ret;
    }
    if (node.type != LEONOS_FS_TYPE_DIR) {
        return -20;
    }
    while (count < capacity) {
        struct leonos_dir_entry tmp;
        int step = storage_readdir_node(&node, &cursor, &tmp);
        if (step < 0) {
            return step;
        }
        if (step == 0) {
            break;
        }
        if (entries) {
            entries[count] = tmp;
        }
        ++count;
    }
    if (g_devfs_enabled && node.volume_id == 0 &&
        (node.flags & STORAGE_NODE_FLAG_ROOT) && count < capacity) {
        if (entries) {
            entries[count].type = LEONOS_FS_TYPE_DIR;
            storage_copy_text(entries[count].name, sizeof(entries[count].name), "dev");
        }
        ++count;
    }
    *out_count = count;
    return 0;
}

int storage_stat_path(const char *path, struct leonos_stat *st)
{
    struct storage_node node;
    if (!st) {
        return -22;
    }
    int ret = storage_lookup_path(path, &node);
    if (ret < 0) {
        return ret;
    }
    st->type = node.type;
    st->reserved = 0;
    st->size = node.size;
    return 0;
}

