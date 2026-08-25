static uint32_t iso_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t iso_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static int iso9660_mount(void)
{
    const uint8_t *pvd = storage_scratch;
    const uint8_t *root;
    uint32_t block_size;
    uint32_t sector_count;
    uint32_t root_extent;
    uint32_t root_size;
    int ret;

    g_storage.filesystem = STORAGE_FILESYSTEM_ISO9660;
    ret = storage_read_iso_blocks(ISO9660_PVD_LBA, 1, storage_scratch);
    if (ret < 0) {
        return storage_read_failure(ret);
    }
    if (pvd[0] != 1 || pvd[1] != 'C' || pvd[2] != 'D' ||
        pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1') {
        return -2;
    }
    block_size = (uint32_t)pvd[128] | ((uint32_t)pvd[129] << 8);
    if (block_size != ISO9660_BLOCK_SIZE) {
        return -2;
    }
    sector_count = iso_u32_le(pvd + 80);
    if (!sector_count) {
        sector_count = iso_u32_be(pvd + 84);
    }
    root = pvd + 156;
    if (root[0] < 34 || root[1] != 0) {
        return -2;
    }
    root_extent = iso_u32_le(root + 2);
    root_size = iso_u32_le(root + 10);
    if (!root_extent || !root_size || root[32] == 0 ||
        root_extent >= sector_count ||
        (uint64_t)root_size > (uint64_t)(sector_count - root_extent) * ISO9660_BLOCK_SIZE) {
        return -2;
    }
    g_storage.bytes_per_sector = ISO9660_BLOCK_SIZE;
    g_storage.iso_block_size = ISO9660_BLOCK_SIZE;
    g_storage.iso_sector_count = sector_count;
    g_storage.iso_root_extent = root_extent;
    g_storage.iso_root_size = root_size;
    g_storage.filesystem = STORAGE_FILESYSTEM_ISO9660;
    return 0;
}

static uint32_t iso_visible_name_length(const uint8_t *name, uint32_t length)
{
    uint32_t visible = length;
    if (!name) {
        return 0;
    }
    for (uint32_t i = 0; i + 1u < visible; ++i) {
        if (name[i] == ';') {
            visible = i;
            break;
        }
    }
    while (visible && name[visible - 1u] == '.') {
        --visible;
    }
    return visible;
}

static int iso_name_match(const uint8_t *record_name, uint32_t record_length,
                          const char *name)
{
    uint32_t record_visible;
    uint32_t input_length;
    uint32_t input_visible;
    if (!record_name || !name || !name[0]) {
        return 0;
    }
    input_length = (uint32_t)storage_strlen(name);
    input_visible = iso_visible_name_length((const uint8_t *)name, input_length);
    record_visible = iso_visible_name_length(record_name, record_length);
    if (!record_visible || record_visible != input_visible) {
        return 0;
    }
    for (uint32_t i = 0; i < record_visible; ++i) {
        char left = (char)record_name[i];
        char right = name[i];
        if (left >= 'A' && left <= 'Z') {
            left = (char)(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = (char)(right - 'A' + 'a');
        }
        if (left != right) {
            return 0;
        }
    }
    return 1;
}

static void iso_copy_name(char *dst, uint32_t cap, const uint8_t *record_name,
                          uint32_t record_length)
{
    uint32_t length = iso_visible_name_length(record_name, record_length);
    uint32_t out = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (out < length && out + 1u < cap) {
        char ch = (char)record_name[out];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        dst[out++] = ch;
    }
    dst[out] = 0;
}

static int iso9660_find_in_dir(uint32_t extent, uint32_t size, const char *name,
                               struct storage_node *out)
{
    uint32_t offset = 0;
    if (!extent || !size || !name || !name[0]) {
        return -2;
    }
    while (offset < size) {
        uint32_t block = offset / ISO9660_BLOCK_SIZE;
        uint32_t block_limit = min_u32(ISO9660_BLOCK_SIZE, size - block * ISO9660_BLOCK_SIZE);
        int ret = storage_read_iso_blocks((uint64_t)extent + block, 1, storage_scratch);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        for (uint32_t pos = 0; pos < block_limit;) {
            const uint8_t *record = storage_scratch + pos;
            uint32_t length = record[0];
            if (!length) {
                break;
            }
            if (length < 34u || pos + length > ISO9660_BLOCK_SIZE ||
                record[32] == 0 || 33u + record[32] > length) {
                return -5;
            }
            if (record[32] > 1u && iso_name_match(record + 33, record[32], name)) {
                if (out) {
                    out->type = (record[25] & 0x02u) ? LEONOS_FS_TYPE_DIR : LEONOS_FS_TYPE_FILE;
                    out->flags = 0;
                    out->first_cluster = iso_u32_le(record + 2);
                    out->volume_id = g_storage.volume_id;
                    out->size = iso_u32_le(record + 10);
                }
                return 0;
            }
            pos += length;
        }
        offset = (block + 1u) * ISO9660_BLOCK_SIZE;
    }
    return -2;
}

static int iso9660_iter_dir_entry(uint32_t extent, uint32_t size, uint64_t index,
                                  struct leonos_dir_entry *entry)
{
    uint32_t offset = 0;
    uint64_t ordinal = 0;
    if (!extent || !size || !entry) {
        return -2;
    }
    while (offset < size) {
        uint32_t block = offset / ISO9660_BLOCK_SIZE;
        uint32_t block_limit = min_u32(ISO9660_BLOCK_SIZE, size - block * ISO9660_BLOCK_SIZE);
        int ret = storage_read_iso_blocks((uint64_t)extent + block, 1, storage_scratch);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        for (uint32_t pos = 0; pos < block_limit;) {
            const uint8_t *record = storage_scratch + pos;
            uint32_t length = record[0];
            if (!length) {
                break;
            }
            if (length < 34u || pos + length > ISO9660_BLOCK_SIZE ||
                record[32] == 0 || 33u + record[32] > length) {
                return -5;
            }
            if (record[32] > 1u) {
                if (ordinal == index) {
                    entry->type = (record[25] & 0x02u) ? LEONOS_FS_TYPE_DIR : LEONOS_FS_TYPE_FILE;
                    iso_copy_name(entry->name, sizeof(entry->name), record + 33, record[32]);
                    return 0;
                }
                ++ordinal;
            }
            pos += length;
        }
        offset = (block + 1u) * ISO9660_BLOCK_SIZE;
    }
    return -2;
}

