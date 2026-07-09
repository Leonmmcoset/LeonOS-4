#include <ntclks/console.h>
#include <ntclks/mm.h>
#include <ntclks/multiboot2.h>
#include <ntclks/osmlayer.h>
#include <ntclks/pci.h>
#include <ntclks/storage.h>

#define ATA_CLASS_MASS_STORAGE 0x01u
#define ATA_SUBCLASS_SATA 0x06u
#define ATA_PROGIF_AHCI 0x01u

#define AHCI_GHC_AE 0x80000000u
#define AHCI_PORT_CMD_ST 0x0001u
#define AHCI_PORT_CMD_FRE 0x0010u
#define AHCI_PORT_CMD_FR 0x4000u
#define AHCI_PORT_CMD_CR 0x8000u
#define AHCI_PORT_TFD_BSY 0x80u
#define AHCI_PORT_TFD_DRQ 0x08u
#define AHCI_PORT_IS_TFES 0x40000000u
#define AHCI_PORT_SIG_ATA 0x00000101u
#define AHCI_PORT_DET_PRESENT 0x3u
#define AHCI_PORT_IPM_ACTIVE 0x1u

#define ATA_CMD_READ_DMA_EXT 0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u
#define ATA_CMD_IDENTIFY_DEVICE 0xECu
#define ATA_DEV_BUSY 0x80u
#define ATA_DEV_DRQ 0x08u

#define FIS_TYPE_REG_H2D 0x27u

#define AHCI_CMDH_PRDTL 1u
#define AHCI_MAX_SECTORS 8u
#define AHCI_WAIT_SPINS 40000000u

#define SECTOR_SIZE 512u
#define GPT_ENTRY_COUNT 128u
#define GPT_ENTRY_SIZE 128u

#define FAT32_EOC 0x0ffffff8u
#define FAT32_ATTR_DIRECTORY 0x10u
#define FAT32_ATTR_LFN 0x0fu
#define FAT32_ATTR_HIDDEN 0x02u
#define FAT32_ATTR_SYSTEM 0x04u
#define FAT32_ATTR_ARCHIVE 0x20u
#define FAT32_DIR_ENTRY_BYTES 32u

#define STORAGE_SCRATCH_SECTORS 8u
#define FAT32_MAX_FILE_CLUSTERS 16384u
#define STORAGE_MAX_DRIVES 2u
#define STORAGE_MAX_INSTALL_DISKS LEONOS_INSTALL_MAX_DISKS
#define INSTALL_ESP_FIRST_LBA 2048ULL

enum storage_volume_kind {
    STORAGE_VOLUME_NONE = 0,
    STORAGE_VOLUME_AHCI = 1,
    STORAGE_VOLUME_RAM = 2,
};

struct __attribute__((packed)) ahci_hba_port {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
};

struct __attribute__((packed)) ahci_hba_mem {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t reserved[0xa0 - 0x2c];
    uint8_t vendor[0x100 - 0xa0];
    struct ahci_hba_port ports[32];
};

struct __attribute__((packed)) ahci_cmd_header {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
};

struct __attribute__((packed)) ahci_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved0;
    uint32_t dbc;
};

struct __attribute__((packed)) ahci_cmd_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[1];
};

struct __attribute__((packed)) fis_reg_h2d {
    uint8_t fis_type;
    uint8_t pmport : 4;
    uint8_t reserved0 : 3;
    uint8_t c : 1;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved1[4];
};

struct __attribute__((packed)) gpt_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entries_lba;
    uint32_t partition_entry_count;
    uint32_t partition_entry_size;
    uint32_t partition_entries_crc32;
};

struct __attribute__((packed)) gpt_entry {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attrs;
    uint16_t name[36];
};

struct __attribute__((packed)) fat32_bpb {
    uint8_t jump[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors16;
    uint8_t media;
    uint16_t fat_size16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
    uint32_t fat_size32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
};

struct __attribute__((packed)) fat32_dirent {
    uint8_t name[11];
    uint8_t attr;
    uint8_t ntres;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t first_cluster_lo;
    uint32_t size;
};

struct __attribute__((packed)) fat32_lfn {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t zero;
    uint16_t name3[2];
};

static const uint8_t esp_guid[16] = {
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b,
};

struct storage_volume {
    bool ready;
    uint8_t drive;
    uint8_t kind;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t port;
    struct ahci_hba_mem *abar;
    struct ahci_hba_port *hba_port;
    uint8_t *ram_base;
    uint64_t ram_bytes;
    uint64_t esp_start_lba;
    uint64_t esp_sector_count;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t cluster_bytes;
    uint32_t fat_count;
    uint32_t fat_start_sector;
    uint32_t fat_sector_count;
    uint32_t data_start_sector;
    uint32_t total_sectors;
    uint32_t data_cluster_count;
    uint32_t root_cluster;
    uint32_t next_free_cluster;
    uint8_t gpt_disk_guid[16];
    uint8_t esp_unique_guid[16];
    uint8_t has_gpt_identity;
};

struct install_disk_state {
    bool present;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t port;
    uint8_t boot_root;
    uint8_t target_mounted;
    struct ahci_hba_mem *abar;
    struct ahci_hba_port *hba_port;
    uint64_t sector_count;
};

static struct storage_volume g_volumes[STORAGE_MAX_DRIVES];
static struct storage_volume *g_active_volume = &g_volumes[0];
#define g_storage (*g_active_volume)

static struct install_disk_state g_install_disks[STORAGE_MAX_INSTALL_DISKS];
static uint32_t g_install_disk_count;
static uint8_t g_devfs_enabled = 1;
static uint8_t g_installer_root_active;

static uint8_t ahci_received_fis[256] __attribute__((aligned(256)));
static uint8_t ahci_cmd_table_buf[256] __attribute__((aligned(128)));
static struct ahci_cmd_header ahci_cmd_headers[32] __attribute__((aligned(1024)));
static uint8_t storage_scratch[STORAGE_SCRATCH_SECTORS * SECTOR_SIZE] __attribute__((aligned(4096)));
static uint8_t storage_cluster_buf[64 * SECTOR_SIZE] __attribute__((aligned(4096)));
static uint32_t storage_old_chain[FAT32_MAX_FILE_CLUSTERS];
static uint32_t storage_new_chain[FAT32_MAX_FILE_CLUSTERS];

static int fat32_mount(void);

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

static int storage_drive_id_from_path(const char *path)
{
    if (path && path[0] >= '0' && path[0] < (char)('0' + STORAGE_MAX_DRIVES) &&
        path[1] == ':' && path[2] == '/') {
        return path[0] - '0';
    }
    return -1;
}

static int storage_select_drive(uint32_t drive)
{
    if (drive >= STORAGE_MAX_DRIVES || !g_volumes[drive].ready) {
        return -2;
    }
    g_active_volume = &g_volumes[drive];
    return 0;
}

static int storage_select_node_drive(const struct storage_node *node,
                                     struct storage_volume **old_volume)
{
    if (!node || node->drive >= STORAGE_MAX_DRIVES ||
        !g_volumes[node->drive].ready) {
        return -2;
    }
    if (old_volume) {
        *old_volume = g_active_volume;
    }
    g_active_volume = &g_volumes[node->drive];
    return 0;
}

static void storage_restore_volume(struct storage_volume *old_volume)
{
    if (old_volume) {
        g_active_volume = old_volume;
    }
}

static int storage_parent_path(const char *path, char *parent, uint32_t parent_cap,
                               char *name, uint32_t name_cap)
{
    char resolved[LEONOS_FS_PATH_LEN];
    uint32_t slash = 0;
    if (!path || !parent || !name || parent_cap < 4 || name_cap == 0) {
        return -22;
    }
    if (storage_resolve_path("0:/", path, resolved, sizeof(resolved)) < 0) {
        return -22;
    }
    for (uint32_t i = 0; resolved[i]; ++i) {
        if (resolved[i] == '/') {
            slash = i;
        }
    }
    if (slash < 2 || resolved[slash + 1] == 0) {
        return -22;
    }
    if (slash == 2) {
        if (parent_cap < 4) {
            return -22;
        }
        parent[0] = resolved[0];
        parent[1] = ':';
        parent[2] = '/';
        parent[3] = 0;
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

static uint32_t fat_offset_for_cluster(uint32_t cluster)
{
    return (cluster * 4u) % g_storage.bytes_per_sector;
}

static void ahci_cpu_relax(void)
{
    __asm__ volatile("pause");
}

static int ahci_wait_idle(struct ahci_hba_port *port)
{
    for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
        if ((port->tfd & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0) {
            return 0;
        }
        ahci_cpu_relax();
    }
    return -1;
}

static int ahci_wait_cmd_slot(struct ahci_hba_port *port)
{
    for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
        if ((port->ci | port->sact) == 0) {
            return 0;
        }
        ahci_cpu_relax();
    }
    return -5;
}

static void ahci_stop_port(struct ahci_hba_port *port)
{
    port->cmd &= ~AHCI_PORT_CMD_ST;
    port->cmd &= ~AHCI_PORT_CMD_FRE;
    for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
        if ((port->cmd & (AHCI_PORT_CMD_FR | AHCI_PORT_CMD_CR)) == 0) {
            break;
        }
        ahci_cpu_relax();
    }
}

static void ahci_start_port(struct ahci_hba_port *port)
{
    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST;
}

static int ahci_setup_port(struct ahci_hba_port *port)
{
    ahci_stop_port(port);
    storage_memzero(ahci_cmd_headers, sizeof(ahci_cmd_headers));
    storage_memzero(ahci_received_fis, sizeof(ahci_received_fis));
    storage_memzero(ahci_cmd_table_buf, sizeof(ahci_cmd_table_buf));

    port->clb = (uint32_t)(uintptr_t)ahci_cmd_headers;
    port->clbu = 0;
    port->fb = (uint32_t)(uintptr_t)ahci_received_fis;
    port->fbu = 0;

    struct ahci_cmd_header *cmd = &ahci_cmd_headers[0];
    cmd->prdtl = AHCI_CMDH_PRDTL;
    cmd->ctba = (uint32_t)(uintptr_t)ahci_cmd_table_buf;
    cmd->ctbau = 0;

    ahci_start_port(port);
    return 0;
}

static int ahci_read_lba(struct ahci_hba_port *port, uint64_t lba, uint32_t sector_count, void *buffer)
{
    struct ahci_cmd_header *hdr;
    struct ahci_cmd_table *tbl;
    struct fis_reg_h2d *fis;

    if (!port || !buffer || sector_count == 0 || sector_count > AHCI_MAX_SECTORS) {
        return -22;
    }
    if (ahci_wait_idle(port) < 0) {
        return -5;
    }

    port->is = 0xffffffffu;
    hdr = &ahci_cmd_headers[0];
    hdr->flags = (uint16_t)((sizeof(struct fis_reg_h2d) / sizeof(uint32_t)) & 0x1f);
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    tbl = (struct ahci_cmd_table *)(void *)ahci_cmd_table_buf;
    storage_memzero(tbl, sizeof(ahci_cmd_table_buf));
    tbl->prdt[0].dba = (uint32_t)(uintptr_t)buffer;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc = (sector_count * SECTOR_SIZE) - 1u;

    fis = (struct fis_reg_h2d *)(void *)tbl->cfis;
    storage_memzero(fis, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_READ_DMA_EXT;
    fis->device = 1u << 6;
    fis->lba0 = (uint8_t)(lba & 0xffu);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xffu);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xffu);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xffu);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xffu);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xffu);
    fis->countl = (uint8_t)(sector_count & 0xffu);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xffu);

    if (ahci_wait_cmd_slot(port) < 0) {
        return -5;
    }
    port->ci = 1u;

    for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
        if ((port->ci & 1u) == 0) {
            if (port->is & AHCI_PORT_IS_TFES) {
                return -5;
            }
            return 0;
        }
        ahci_cpu_relax();
    }
    return -5;
}

static int ahci_write_lba(struct ahci_hba_port *port, uint64_t lba, uint32_t sector_count, const void *buffer)
{
    struct ahci_cmd_header *hdr;
    struct ahci_cmd_table *tbl;
    struct fis_reg_h2d *fis;

    if (!port || !buffer || sector_count == 0 || sector_count > AHCI_MAX_SECTORS) {
        return -22;
    }
    if (ahci_wait_idle(port) < 0) {
        return -5;
    }

    port->is = 0xffffffffu;
    hdr = &ahci_cmd_headers[0];
    hdr->flags = (uint16_t)(((sizeof(struct fis_reg_h2d) / sizeof(uint32_t)) & 0x1f) | (1u << 6));
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    tbl = (struct ahci_cmd_table *)(void *)ahci_cmd_table_buf;
    storage_memzero(tbl, sizeof(ahci_cmd_table_buf));
    tbl->prdt[0].dba = (uint32_t)(uintptr_t)buffer;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc = (sector_count * SECTOR_SIZE) - 1u;

    fis = (struct fis_reg_h2d *)(void *)tbl->cfis;
    storage_memzero(fis, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_WRITE_DMA_EXT;
    fis->device = 1u << 6;
    fis->lba0 = (uint8_t)(lba & 0xffu);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xffu);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xffu);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xffu);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xffu);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xffu);
    fis->countl = (uint8_t)(sector_count & 0xffu);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xffu);

    if (ahci_wait_cmd_slot(port) < 0) {
        return -5;
    }
    port->ci = 1u;

    for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
        if ((port->ci & 1u) == 0) {
            if (port->is & AHCI_PORT_IS_TFES) {
                return -5;
            }
            return 0;
        }
        ahci_cpu_relax();
    }
    return -5;
}

static int storage_read_sectors(uint64_t lba, uint32_t sector_count, void *buffer)
{
    uint8_t *dst = (uint8_t *)buffer;
    if (g_storage.kind == STORAGE_VOLUME_RAM) {
        uint64_t offset = lba * SECTOR_SIZE;
        uint64_t bytes = (uint64_t)sector_count * SECTOR_SIZE;
        if (!buffer || !g_storage.ram_base || offset + bytes < offset ||
            offset + bytes > g_storage.ram_bytes) {
            return -22;
        }
        storage_memcpy(buffer, g_storage.ram_base + offset, (size_t)bytes);
        return 0;
    }
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count, AHCI_MAX_SECTORS);
        int ret = ahci_read_lba(g_storage.hba_port, lba, chunk, dst);
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        sector_count -= chunk;
        dst += chunk * SECTOR_SIZE;
    }
    return 0;
}

static int storage_write_sectors(uint64_t lba, uint32_t sector_count, const void *buffer)
{
    const uint8_t *src = (const uint8_t *)buffer;
    if (g_storage.kind == STORAGE_VOLUME_RAM) {
        uint64_t offset = lba * SECTOR_SIZE;
        uint64_t bytes = (uint64_t)sector_count * SECTOR_SIZE;
        if (!buffer || !g_storage.ram_base || offset + bytes < offset ||
            offset + bytes > g_storage.ram_bytes) {
            return -22;
        }
        storage_memcpy(g_storage.ram_base + offset, buffer, (size_t)bytes);
        return 0;
    }
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count, AHCI_MAX_SECTORS);
        int ret = ahci_write_lba(g_storage.hba_port, lba, chunk, src);
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        sector_count -= chunk;
        src += chunk * SECTOR_SIZE;
    }
    return 0;
}

static int gpt_find_esp(void)
{
    struct gpt_header *hdr = (struct gpt_header *)(void *)storage_scratch;
    if (storage_read_sectors(1, 1, storage_scratch) < 0) {
        return -5;
    }
    if (hdr->signature != 0x5452415020494645ULL) {
        return -2;
    }
    uint32_t count = hdr->partition_entry_count;
    uint32_t size = hdr->partition_entry_size;
    if (!count || !size || size < sizeof(struct gpt_entry)) {
        return -2;
    }
    uint32_t total_bytes = count * size;
    uint32_t total_sectors = (total_bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE;
    uint64_t phys = mm_alloc_pages((total_sectors + 7u) / 8u);
    if (!phys) {
        return -12;
    }
    uint8_t *table = (uint8_t *)(uintptr_t)phys;
    int ret = storage_read_sectors(hdr->partition_entries_lba, total_sectors, table);
    if (ret < 0) {
        mm_free_pages(phys, (total_sectors + 7u) / 8u);
        return ret;
    }
    for (uint32_t i = 0; i < count; ++i) {
        struct gpt_entry *entry = (struct gpt_entry *)(void *)(table + (uint64_t)i * size);
        if (storage_memcmp(entry->type_guid, esp_guid, 16) == 0 &&
            entry->first_lba && entry->last_lba >= entry->first_lba) {
            g_storage.esp_start_lba = entry->first_lba;
            g_storage.esp_sector_count = entry->last_lba - entry->first_lba + 1u;
            storage_memcpy(g_storage.gpt_disk_guid, hdr->disk_guid,
                           sizeof(g_storage.gpt_disk_guid));
            storage_memcpy(g_storage.esp_unique_guid, entry->unique_guid,
                           sizeof(g_storage.esp_unique_guid));
            g_storage.has_gpt_identity =
                storage_guid_valid(g_storage.gpt_disk_guid) &&
                storage_guid_valid(g_storage.esp_unique_guid);
            mm_free_pages(phys, (total_sectors + 7u) / 8u);
            return 0;
        }
    }
    mm_free_pages(phys, (total_sectors + 7u) / 8u);
    return -2;
}

static int fat32_mount(void)
{
    struct fat32_bpb *bpb = (struct fat32_bpb *)(void *)storage_scratch;
    uint32_t total_sectors;
    uint32_t data_sectors;
    if (storage_read_sectors(g_storage.esp_start_lba, 1, storage_scratch) < 0) {
        return -5;
    }
    if (bpb->bytes_per_sector != SECTOR_SIZE || bpb->sectors_per_cluster == 0 ||
        bpb->fat_count == 0 || bpb->fat_size32 == 0 || bpb->root_cluster < 2) {
        return -2;
    }
    g_storage.bytes_per_sector = bpb->bytes_per_sector;
    g_storage.sectors_per_cluster = bpb->sectors_per_cluster;
    g_storage.cluster_bytes = bpb->bytes_per_sector * bpb->sectors_per_cluster;
    g_storage.fat_count = bpb->fat_count;
    g_storage.fat_start_sector = bpb->reserved_sector_count;
    g_storage.fat_sector_count = bpb->fat_size32;
    g_storage.data_start_sector = bpb->reserved_sector_count + (uint32_t)bpb->fat_count * bpb->fat_size32;
    g_storage.root_cluster = bpb->root_cluster;
    g_storage.next_free_cluster = bpb->root_cluster + 1u;
    total_sectors = bpb->total_sectors32 ? bpb->total_sectors32 : bpb->total_sectors16;
    g_storage.total_sectors = total_sectors;
    if (total_sectors <= g_storage.data_start_sector) {
        return -2;
    }
    data_sectors = total_sectors - g_storage.data_start_sector;
    g_storage.data_cluster_count = data_sectors / g_storage.sectors_per_cluster;
    if (g_storage.cluster_bytes > sizeof(storage_cluster_buf)) {
        console_printf("[ntclks] storage FAT32 cluster too large=%u\n", g_storage.cluster_bytes);
        return -2;
    }
    return 0;
}

static int fat32_read_fat_entry(uint32_t cluster, uint32_t *out_next)
{
    uint32_t sector = fat_sector_for_cluster(cluster);
    uint32_t offset = fat_offset_for_cluster(cluster);
    if (storage_read_sectors(sector, 1, storage_scratch) < 0) {
        return -5;
    }
    uint32_t value = *(const uint32_t *)(const void *)(storage_scratch + offset);
    *out_next = value & 0x0fffffffu;
    return 0;
}

static int fat32_write_fat_entry(uint32_t cluster, uint32_t value)
{
    uint32_t sector_offset = (cluster * 4u) / g_storage.bytes_per_sector;
    uint32_t offset = fat_offset_for_cluster(cluster);
    uint32_t masked = value & 0x0fffffffu;
    for (uint32_t fat = 0; fat < g_storage.fat_count; ++fat) {
        uint64_t lba = g_storage.esp_start_lba + g_storage.fat_start_sector +
                       (uint64_t)fat * g_storage.fat_sector_count + sector_offset;
        if (storage_read_sectors(lba, 1, storage_scratch) < 0) {
            return -5;
        }
        *(uint32_t *)(void *)(storage_scratch + offset) =
            (*(uint32_t *)(void *)(storage_scratch + offset) & 0xf0000000u) | masked;
        if (storage_write_sectors(lba, 1, storage_scratch) < 0) {
            return -5;
        }
    }
    return 0;
}

static int fat32_read_cluster(uint32_t cluster, void *buffer)
{
    if (cluster < 2) {
        return -2;
    }
    return storage_read_sectors(cluster_to_lba(cluster), g_storage.sectors_per_cluster, buffer);
}

static int fat32_write_cluster(uint32_t cluster, const void *buffer)
{
    if (cluster < 2 || !buffer) {
        return -2;
    }
    return storage_write_sectors(cluster_to_lba(cluster), g_storage.sectors_per_cluster, buffer);
}

static int storage_install_identify(struct install_disk_state *disk, uint64_t *out_sectors)
{
    uint16_t *id = (uint16_t *)storage_scratch;
    if (!disk || !disk->hba_port || !out_sectors) {
        return -22;
    }
    if (ahci_wait_idle(disk->hba_port) < 0) {
        return -5;
    }
    disk->hba_port->is = 0xffffffffu;
    storage_memzero(ahci_cmd_headers, sizeof(ahci_cmd_headers));
    storage_memzero(ahci_received_fis, sizeof(ahci_received_fis));
    storage_memzero(ahci_cmd_table_buf, sizeof(ahci_cmd_table_buf));
    ahci_cmd_headers[0].flags = (uint16_t)((sizeof(struct fis_reg_h2d) / sizeof(uint32_t)) & 0x1f);
    ahci_cmd_headers[0].prdtl = 1;
    ahci_cmd_headers[0].ctba = (uint32_t)(uintptr_t)ahci_cmd_table_buf;
    {
        struct ahci_cmd_table *tbl = (struct ahci_cmd_table *)(void *)ahci_cmd_table_buf;
        struct fis_reg_h2d *fis = (struct fis_reg_h2d *)(void *)tbl->cfis;
        storage_memzero(tbl, sizeof(ahci_cmd_table_buf));
        tbl->prdt[0].dba = (uint32_t)(uintptr_t)storage_scratch;
        tbl->prdt[0].dbau = 0;
        tbl->prdt[0].dbc = SECTOR_SIZE - 1u;
        fis->fis_type = FIS_TYPE_REG_H2D;
        fis->c = 1;
        fis->command = ATA_CMD_IDENTIFY_DEVICE;
        fis->device = 0;
        fis->countl = 1;
        if (ahci_wait_cmd_slot(disk->hba_port) < 0) {
            return -5;
        }
        disk->hba_port->ci = 1u;
        for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
            if ((disk->hba_port->ci & 1u) == 0) {
                if (disk->hba_port->is & AHCI_PORT_IS_TFES) {
                    return -5;
                }
                *out_sectors = ((uint64_t)id[103] << 48) |
                               ((uint64_t)id[102] << 32) |
                               ((uint64_t)id[101] << 16) |
                               id[100];
                if (*out_sectors == 0) {
                    *out_sectors = ((uint64_t)id[61] << 16) | id[60];
                }
                return 0;
            }
            ahci_cpu_relax();
        }
    }
    return -5;
}

static int fat32_is_short_compatible_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-';
}

static char fat32_upper_char(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - 'a' + 'A');
    }
    return ch;
}

static int fat32_make_short_name(const char *name, uint8_t out[11])
{
    uint32_t dot = 0xffffffffu;
    uint32_t len = 0;
    if (!name || !name[0] || !out) {
        return -22;
    }
    while (name[len]) {
        if (name[len] == '.') {
            if (dot != 0xffffffffu) {
                return -22;
            }
            dot = len;
        } else if (!fat32_is_short_compatible_char(name[len])) {
            return -22;
        }
        ++len;
    }
    if (dot == 0 || len == 0) {
        return -22;
    }
    if (dot == 0xffffffffu) {
        dot = len;
    }
    if (dot > 8 || (len - dot - (dot < len ? 1u : 0u)) > 3) {
        return -22;
    }
    for (uint32_t i = 0; i < 11; ++i) {
        out[i] = ' ';
    }
    for (uint32_t i = 0; i < dot; ++i) {
        out[i] = (uint8_t)fat32_upper_char(name[i]);
    }
    if (dot < len) {
        for (uint32_t i = dot + 1, j = 8; i < len; ++i, ++j) {
            out[j] = (uint8_t)fat32_upper_char(name[i]);
        }
    }
    return 0;
}

static int fat32_short_name_eq(const uint8_t lhs[11], const uint8_t rhs[11])
{
    return storage_memcmp(lhs, rhs, 11) == 0;
}

static int fat32_find_free_cluster(uint32_t *out_cluster)
{
    uint32_t max_cluster;
    uint32_t start;
    if (!out_cluster) {
        return -22;
    }
    max_cluster = g_storage.data_cluster_count + 1u;
    start = g_storage.next_free_cluster;
    if (start < 2 || start > max_cluster) {
        start = 2;
    }
    for (uint32_t pass = 0; pass < 2; ++pass) {
        uint32_t begin = pass == 0 ? start : 2;
        uint32_t end = pass == 0 ? max_cluster : (start > 2 ? start - 1u : 1u);
        for (uint32_t cluster = begin; cluster <= end; ++cluster) {
            uint32_t value = 0;
            if (fat32_read_fat_entry(cluster, &value) < 0) {
                return -5;
            }
            if (value == 0) {
                *out_cluster = cluster;
                g_storage.next_free_cluster = cluster < max_cluster ? cluster + 1u : 2u;
                return 0;
            }
        }
    }
    return -28;
}

static int fat32_collect_chain(uint32_t first_cluster, uint32_t chain[FAT32_MAX_FILE_CLUSTERS], uint32_t *out_count)
{
    uint32_t count = 0;
    uint32_t cluster = first_cluster;
    if (!out_count || !chain) {
        return -22;
    }
    *out_count = 0;
    if (cluster < 2) {
        return 0;
    }
    while (cluster >= 2 && cluster < FAT32_EOC) {
        uint32_t next = 0;
        if (count >= FAT32_MAX_FILE_CLUSTERS) {
            return -28;
        }
        chain[count++] = cluster;
        if (fat32_read_fat_entry(cluster, &next) < 0) {
            return -5;
        }
        if (next >= FAT32_EOC) {
            break;
        }
        cluster = next;
    }
    *out_count = count;
    return 0;
}

static int fat32_free_chain(uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;
    if (first_cluster >= 2 &&
        (g_storage.next_free_cluster < 2 || first_cluster < g_storage.next_free_cluster)) {
        g_storage.next_free_cluster = first_cluster;
    }
    while (cluster >= 2 && cluster < FAT32_EOC) {
        uint32_t next = 0;
        if (fat32_read_fat_entry(cluster, &next) < 0) {
            return -5;
        }
        if (fat32_write_fat_entry(cluster, 0) < 0) {
            return -5;
        }
        if (next >= FAT32_EOC) {
            break;
        }
        cluster = next;
    }
    return 0;
}
static int fat32_name_match_short(const struct fat32_dirent *de, const char *name)
{
    char short_name[LEONOS_FS_NAME_LEN];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 8 && de->name[i] != ' '; ++i) {
        char ch = (char)de->name[i];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (pos + 1 < sizeof(short_name)) {
            short_name[pos++] = ch;
        }
    }
    if (de->name[8] != ' ') {
        if (pos + 1 < sizeof(short_name)) {
            short_name[pos++] = '.';
        }
        for (uint32_t i = 8; i < 11 && de->name[i] != ' '; ++i) {
            char ch = (char)de->name[i];
            if (ch >= 'A' && ch <= 'Z') {
                ch = (char)(ch - 'A' + 'a');
            }
            if (pos + 1 < sizeof(short_name)) {
                short_name[pos++] = ch;
            }
        }
    }
    short_name[pos] = 0;
    return storage_text_eq_ci(short_name, name);
}

static int fat32_dirent_is_acl_metadata(const struct fat32_dirent *de)
{
    if (!de || de->attr == FAT32_ATTR_LFN ||
        (de->attr & FAT32_ATTR_DIRECTORY) != 0 ||
        (de->attr & 0x08u) != 0) {
        return 0;
    }
    return fat32_name_match_short(de, "LEONACL.SYS");
}

static void fat32_lfn_extract_utf16(const struct fat32_lfn *lfn, uint16_t *dst,
                                    uint32_t *len, uint32_t cap)
{
    const uint16_t *parts[3] = {lfn->name1, lfn->name2, lfn->name3};
    const uint32_t counts[3] = {5, 6, 2};
    for (uint32_t p = 0; p < 3; ++p) {
        for (uint32_t i = 0; i < counts[p]; ++i) {
            uint16_t ch = parts[p][i];
            if (ch == 0x0000 || ch == 0xffff) {
                return;
            }
            if (*len < cap) {
                dst[(*len)++] = ch;
            }
        }
    }
}

static void fat32_build_lfn_name(const uint16_t lfn_parts[20][13], uint32_t lfn_count,
                                 char *dst, uint32_t cap)
{
    uint16_t utf16[260];
    uint32_t len = 0;
    struct leonos_unicode_utf16_to_utf8 cmd;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    for (uint32_t i = 0; i < lfn_count; ++i) {
        for (uint32_t j = 0; j < 13 && len < sizeof(utf16) / sizeof(utf16[0]); ++j) {
            uint16_t ch = lfn_parts[i][j];
            if (ch == 0) {
                break;
            }
            utf16[len++] = ch;
        }
    }
    cmd.utf16 = utf16;
    cmd.utf16_len = len;
    cmd.utf8 = dst;
    cmd.utf8_capacity = cap;
    cmd.utf8_len = 0;
    if (osmlayer_unicode_utf16le_to_utf8(&cmd) < 0) {
        dst[0] = 0;
        return;
    }
    if (cmd.utf8_len >= cap) {
        dst[cap - 1] = 0;
    }
}

static int fat32_is_lfn_char_valid(char ch)
{
    unsigned char uch = (unsigned char)ch;
    if (uch < 32) {
        return 0;
    }
    switch (ch) {
    case '"':
    case '*':
    case '/':
    case ':':
    case '<':
    case '>':
    case '?':
    case '\\':
    case '|':
        return 0;
    default:
        return 1;
    }
}

static int fat32_validate_name(const char *name)
{
    uint32_t len = (uint32_t)storage_strlen(name);
    uint16_t utf16[260];
    struct leonos_unicode_utf8_to_utf16 cmd;
    if (!name || !name[0] || len >= LEONOS_FS_NAME_LEN) {
        return -22;
    }
    if (storage_text_eq(name, ".") || storage_text_eq(name, "..")) {
        return -22;
    }
    if (name[len - 1] == ' ' || name[len - 1] == '.') {
        return -22;
    }
    for (uint32_t i = 0; i < len; ++i) {
        if (!fat32_is_lfn_char_valid(name[i])) {
            return -22;
        }
    }
    cmd.utf8 = name;
    cmd.utf8_len = len;
    cmd.utf16 = utf16;
    cmd.utf16_capacity = sizeof(utf16) / sizeof(utf16[0]);
    cmd.utf16_len = 0;
    if (osmlayer_unicode_utf8_to_utf16le(&cmd) < 0 ||
        cmd.utf16_len == 0 || cmd.utf16_len > 255u) {
        return -22;
    }
    return 0;
}

static void fat32_name_split(const char *name,
                             const char **base_start, uint32_t *base_len,
                             const char **ext_start, uint32_t *ext_len)
{
    uint32_t len = (uint32_t)storage_strlen(name);
    uint32_t dot = 0xffffffffu;
    for (uint32_t i = 0; i < len; ++i) {
        if (name[i] == '.') {
            dot = i;
        }
    }
    *base_start = name;
    *base_len = len;
    *ext_start = 0;
    *ext_len = 0;
    if (dot != 0xffffffffu && dot != 0 && dot + 1u < len) {
        *base_len = dot;
        *ext_start = name + dot + 1u;
        *ext_len = len - dot - 1u;
    }
}

static void fat32_collect_short_fragment(const char *src, uint32_t len,
                                         char *dst, uint32_t cap)
{
    uint32_t out = 0;
    if (!dst || cap == 0) {
        return;
    }
    for (uint32_t i = 0; i < len && out + 1 < cap; ++i) {
        char ch = src[i];
        if (fat32_is_short_compatible_char(ch)) {
            dst[out++] = fat32_upper_char(ch);
        }
    }
    dst[out] = 0;
}

static int fat32_short_name_exists_in_dir(uint32_t dir_cluster, const uint8_t short_name[11])
{
    uint32_t cluster = dir_cluster;
    for (;;) {
        if (fat32_read_cluster(cluster, storage_cluster_buf) < 0) {
            return -5;
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return 0;
            }
            if (de->name[0] == 0xe5 || de->attr == FAT32_ATTR_LFN) {
                continue;
            }
            if (fat32_short_name_eq(de->name, short_name)) {
                return 1;
            }
        }
        {
            uint32_t next = 0;
            if (fat32_read_fat_entry(cluster, &next) < 0) {
                return -5;
            }
            if (next >= FAT32_EOC) {
                return 0;
            }
            cluster = next;
        }
    }
}

static int fat32_make_short_alias(uint32_t dir_cluster, const char *name, uint8_t out[11])
{
    const char *base_start = 0;
    const char *ext_start = 0;
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    char base_part[LEONOS_FS_NAME_LEN];
    char ext_part[4];
    fat32_name_split(name, &base_start, &base_len, &ext_start, &ext_len);
    fat32_collect_short_fragment(base_start, base_len, base_part, sizeof(base_part));
    fat32_collect_short_fragment(ext_start, ext_len, ext_part, sizeof(ext_part));
    for (uint32_t ord = 1; ord <= 999999u; ++ord) {
        uint32_t suffix_len = 0;
        uint32_t value = ord;
        char digits[6];
        uint32_t digits_len = 0;
        char tmp[8];
        uint32_t base_limit;
        uint32_t base_copy = 0;
        int exists;

        while (value > 0 && digits_len < sizeof(digits)) {
            digits[digits_len++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        tmp[suffix_len++] = '~';
        while (digits_len) {
            tmp[suffix_len++] = digits[--digits_len];
        }
        if (suffix_len >= 8) {
            continue;
        }
        base_limit = 8u - suffix_len;
        for (uint32_t i = 0; i < 11; ++i) {
            out[i] = ' ';
        }
        if (!base_part[0]) {
            storage_copy_text(base_part, sizeof(base_part), "FILE");
        }
        while (base_part[base_copy] && base_copy < base_limit) {
            out[base_copy] = (uint8_t)base_part[base_copy];
            ++base_copy;
        }
        for (uint32_t i = 0; i < suffix_len; ++i) {
            out[base_copy + i] = (uint8_t)tmp[i];
        }
        for (uint32_t i = 0; ext_part[i] && i < 3; ++i) {
            out[8 + i] = (uint8_t)ext_part[i];
        }
        exists = fat32_short_name_exists_in_dir(dir_cluster, out);
        if (exists < 0) {
            return exists;
        }
        if (!exists) {
            return 0;
        }
    }
    return -28;
}

static uint8_t fat32_short_name_checksum(const uint8_t short_name[11])
{
    uint8_t sum = 0;
    for (uint32_t i = 0; i < 11; ++i) {
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + short_name[i]);
    }
    return sum;
}

static uint32_t fat32_utf16_name(const char *name, uint16_t *utf16, uint32_t cap)
{
    struct leonos_unicode_utf8_to_utf16 cmd = {
        .utf8 = name,
        .utf8_len = (uint32_t)storage_strlen(name),
        .utf16 = utf16,
        .utf16_capacity = cap,
        .utf16_len = 0,
    };
    if (osmlayer_unicode_utf8_to_utf16le(&cmd) < 0) {
        return 0;
    }
    return cmd.utf16_len;
}

static uint32_t fat32_lfn_entry_count(const char *name)
{
    uint16_t utf16[260];
    uint32_t len = fat32_utf16_name(name, utf16, sizeof(utf16) / sizeof(utf16[0]));
    return (len + 12u) / 13u;
}

static void fat32_fill_lfn_entry(struct fat32_lfn *lfn,
                                 const uint16_t *name, uint32_t name_len,
                                 uint32_t part_index, uint32_t part_count,
                                 uint8_t checksum)
{
    uint16_t *slots[3] = {lfn->name1, lfn->name2, lfn->name3};
    const uint32_t slot_counts[3] = {5, 6, 2};
    uint32_t cursor = part_index * 13u;
    int terminated = 0;

    storage_memzero(lfn, sizeof(*lfn));
    lfn->order = (uint8_t)(part_index + 1u);
    if (part_index + 1u == part_count) {
        lfn->order |= 0x40u;
    }
    lfn->attr = FAT32_ATTR_LFN;
    lfn->type = 0;
    lfn->checksum = checksum;
    lfn->zero = 0;

    for (uint32_t part = 0; part < 3; ++part) {
        for (uint32_t i = 0; i < slot_counts[part]; ++i) {
            uint16_t value = 0xffffu;
            if (cursor < name_len) {
                value = name[cursor++];
            } else if (!terminated) {
                value = 0x0000u;
                terminated = 1;
            }
            slots[part][i] = value;
        }
    }
}

static int fat32_find_in_dir(uint32_t dir_cluster, const char *name, struct storage_node *out)
{
    uint32_t cluster = dir_cluster;
    uint16_t lfn_parts[20][13];
    uint32_t lfn_count = 0;
    for (;;) {
        if (fat32_read_cluster(cluster, storage_cluster_buf) < 0) {
            return -5;
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return -2;
            }
            if (de->name[0] == 0xe5) {
                lfn_count = 0;
                continue;
            }
            if (de->attr == FAT32_ATTR_LFN) {
                const struct fat32_lfn *lfn = (const struct fat32_lfn *)(const void *)de;
                uint8_t order = lfn->order & 0x1fu;
                if (order == 0 || order > 20) {
                    lfn_count = 0;
                    continue;
                }
                storage_memzero(lfn_parts[order - 1], sizeof(lfn_parts[0]));
                uint32_t len = 0;
                fat32_lfn_extract_utf16(lfn, lfn_parts[order - 1], &len, 13);
                if (order > lfn_count) {
                    lfn_count = order;
                }
                continue;
            }
            if ((de->attr & 0x08u) != 0) {
                lfn_count = 0;
                continue;
            }
            int matched = 0;
            if (lfn_count) {
                char full[LEONOS_FS_NAME_LEN];
                fat32_build_lfn_name(lfn_parts, lfn_count, full, sizeof(full));
                matched = storage_text_eq_ci(full, name) || fat32_name_match_short(de, name);
            } else {
                matched = fat32_name_match_short(de, name);
            }
            lfn_count = 0;
            if (!matched) {
                continue;
            }
            if (out) {
                out->type = (de->attr & FAT32_ATTR_DIRECTORY) ? LEONOS_FS_TYPE_DIR : LEONOS_FS_TYPE_FILE;
                out->size = de->size;
                out->first_cluster = ((uint32_t)de->first_cluster_hi << 16) | de->first_cluster_lo;
                out->flags = 0;
            }
            return 0;
        }
        uint32_t next = 0;
        if (fat32_read_fat_entry(cluster, &next) < 0) {
            return -5;
        }
        if (next >= FAT32_EOC) {
            return -2;
        }
        cluster = next;
    }
}

static int fat32_find_dirent_ref_in_dir(uint32_t dir_cluster, const char *name, struct fat32_dir_ref *out)
{
    uint32_t cluster = dir_cluster;
    uint16_t lfn_parts[20][13];
    uint32_t lfn_count = 0;
    for (;;) {
        if (fat32_read_cluster(cluster, storage_cluster_buf) < 0) {
            return -5;
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return -2;
            }
            if (de->name[0] == 0xe5) {
                lfn_count = 0;
                continue;
            }
            if (de->attr == FAT32_ATTR_LFN) {
                const struct fat32_lfn *lfn = (const struct fat32_lfn *)(const void *)de;
                uint8_t order = lfn->order & 0x1fu;
                if (order == 0 || order > 20) {
                    lfn_count = 0;
                    continue;
                }
                storage_memzero(lfn_parts[order - 1], sizeof(lfn_parts[0]));
                {
                    uint32_t len = 0;
                    fat32_lfn_extract_utf16(lfn, lfn_parts[order - 1], &len, 13);
                }
                if (order > lfn_count) {
                    lfn_count = order;
                }
                continue;
            }
            if ((de->attr & 0x08u) != 0) {
                lfn_count = 0;
                continue;
            }
            {
                int matched = 0;
                if (lfn_count) {
                    char full[LEONOS_FS_NAME_LEN];
                    fat32_build_lfn_name(lfn_parts, lfn_count, full, sizeof(full));
                    matched = storage_text_eq_ci(full, name) || fat32_name_match_short(de, name);
                } else {
                    matched = fat32_name_match_short(de, name);
                }
                lfn_count = 0;
                if (!matched) {
                    continue;
                }
                if (out) {
                    out->entry_cluster = cluster;
                    out->entry_offset = off;
                    out->dirent = *de;
                }
                return 0;
            }
        }
        {
            uint32_t next = 0;
            if (fat32_read_fat_entry(cluster, &next) < 0) {
                return -5;
            }
            if (next >= FAT32_EOC) {
                return -2;
            }
            cluster = next;
        }
    }
}

static int fat32_update_dirent(const struct fat32_dir_ref *ref)
{
    if (!ref || ref->entry_cluster < 2 || ref->entry_offset + sizeof(struct fat32_dirent) > g_storage.cluster_bytes) {
        return -22;
    }
    if (fat32_read_cluster(ref->entry_cluster, storage_cluster_buf) < 0) {
        return -5;
    }
    *(struct fat32_dirent *)(void *)(storage_cluster_buf + ref->entry_offset) = ref->dirent;
    if (fat32_write_cluster(ref->entry_cluster, storage_cluster_buf) < 0) {
        return -5;
    }
    return 0;
}

static int fat32_name_needs_lfn(const char *name, uint8_t short_name[11], uint8_t *need_lfn)
{
    int ret = fat32_make_short_name(name, short_name);
    if (!need_lfn) {
        return -22;
    }
    *need_lfn = 0;
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
        *need_lfn = storage_text_eq(name, rendered) ? 0u : 1u;
        return 0;
    }
    *need_lfn = 1;
    return 1;
}

static int fat32_find_free_dirent_span(uint32_t dir_cluster, uint32_t slots,
                                       struct fat32_dir_span *out)
{
    uint32_t cluster = dir_cluster;
    uint32_t per_cluster = g_storage.cluster_bytes / sizeof(struct fat32_dirent);
    if (!out || slots == 0 || slots > per_cluster) {
        return -22;
    }
    for (;;) {
        uint32_t run = 0;
        uint32_t run_start = 0;
        if (fat32_read_cluster(cluster, storage_cluster_buf) < 0) {
            return -5;
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00 || de->name[0] == 0xe5) {
                if (run == 0) {
                    run_start = off;
                }
                ++run;
                if (run >= slots) {
                    out->entry_cluster = cluster;
                    out->entry_offset = run_start;
                    return 0;
                }
            } else {
                run = 0;
            }
        }
        {
            uint32_t next = 0;
            if (fat32_read_fat_entry(cluster, &next) < 0) {
                return -5;
            }
            if (next >= FAT32_EOC) {
                uint32_t new_cluster = 0;
                if (fat32_find_free_cluster(&new_cluster) < 0) {
                    return -28;
                }
                if (fat32_write_fat_entry(cluster, new_cluster) < 0 ||
                    fat32_write_fat_entry(new_cluster, FAT32_EOC) < 0) {
                    return -5;
                }
                storage_memzero(storage_cluster_buf, g_storage.cluster_bytes);
                if (fat32_write_cluster(new_cluster, storage_cluster_buf) < 0) {
                    return -5;
                }
                out->entry_cluster = new_cluster;
                out->entry_offset = 0;
                return 0;
            }
            cluster = next;
        }
    }
}

static int fat32_create_dirent(uint32_t parent_cluster, const char *name, uint8_t attr,
                               uint32_t first_cluster, uint32_t size)
{
    struct fat32_dir_span span;
    struct fat32_dirent dirent;
    uint8_t short_name[11];
    uint8_t need_lfn = 0;
    uint32_t lfn_count = 0;
    int ret;

    ret = fat32_name_needs_lfn(name, short_name, &need_lfn);
    if (ret < 0) {
        return ret;
    }
    if (need_lfn) {
        ret = fat32_make_short_alias(parent_cluster, name, short_name);
        if (ret < 0) {
            return ret;
        }
        lfn_count = fat32_lfn_entry_count(name);
        if (lfn_count > 20u) {
            return -22;
        }
    }
    ret = fat32_find_free_dirent_span(parent_cluster, lfn_count + 1u, &span);
    if (ret < 0) {
        return ret;
    }
    storage_memzero(&dirent, sizeof(dirent));
    storage_memcpy(dirent.name, short_name, 11);
    dirent.attr = attr;
    dirent.first_cluster_hi = (uint16_t)(first_cluster >> 16);
    dirent.first_cluster_lo = (uint16_t)(first_cluster & 0xffffu);
    dirent.size = size;

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
    *(struct fat32_dirent *)(void *)(storage_cluster_buf +
        span.entry_offset + lfn_count * sizeof(struct fat32_dirent)) = dirent;
    if (fat32_write_cluster(span.entry_cluster, storage_cluster_buf) < 0) {
        return -5;
    }
    return 0;
}

static int fat32_delete_dirent(uint32_t dir_cluster, const char *name,
                               struct fat32_dirent *deleted)
{
    uint32_t cluster = dir_cluster;
    uint16_t lfn_parts[20][13];
    uint32_t lfn_count = 0;
    uint32_t lfn_start = 0xffffffffu;
    for (;;) {
        if (fat32_read_cluster(cluster, storage_cluster_buf) < 0) {
            return -5;
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return -2;
            }
            if (de->name[0] == 0xe5) {
                lfn_count = 0;
                lfn_start = 0xffffffffu;
                continue;
            }
            if (de->attr == FAT32_ATTR_LFN) {
                const struct fat32_lfn *lfn = (const struct fat32_lfn *)(const void *)de;
                uint8_t order = lfn->order & 0x1fu;
                if (lfn_start == 0xffffffffu) {
                    lfn_start = off;
                }
                if (order == 0 || order > 20) {
                    lfn_count = 0;
                    continue;
                }
                storage_memzero(lfn_parts[order - 1], sizeof(lfn_parts[0]));
                {
                    uint32_t len = 0;
                    fat32_lfn_extract_utf16(lfn, lfn_parts[order - 1], &len, 13);
                }
                if (order > lfn_count) {
                    lfn_count = order;
                }
                continue;
            }
            if ((de->attr & 0x08u) != 0) {
                lfn_count = 0;
                lfn_start = 0xffffffffu;
                continue;
            }
            {
                int matched;
                if (lfn_count) {
                    char full[LEONOS_FS_NAME_LEN];
                    fat32_build_lfn_name(lfn_parts, lfn_count, full, sizeof(full));
                    matched = storage_text_eq_ci(full, name) || fat32_name_match_short(de, name);
                } else {
                    matched = fat32_name_match_short(de, name);
                }
                if (!matched) {
                    lfn_count = 0;
                    lfn_start = 0xffffffffu;
                    continue;
                }
                if (deleted) {
                    *deleted = *de;
                }
                uint32_t start = lfn_start == 0xffffffffu ? off : lfn_start;
                for (uint32_t clear = start; clear <= off; clear += sizeof(struct fat32_dirent)) {
                    struct fat32_dirent *clear_de =
                        (struct fat32_dirent *)(void *)(storage_cluster_buf + clear);
                    clear_de->name[0] = 0xe5u;
                }
                if (fat32_write_cluster(cluster, storage_cluster_buf) < 0) {
                    return -5;
                }
                return 0;
            }
        }
        {
            uint32_t next = 0;
            if (fat32_read_fat_entry(cluster, &next) < 0) {
                return -5;
            }
            if (next >= FAT32_EOC) {
                return -2;
            }
            cluster = next;
        }
    }
}

static int fat32_delete_acl_metadata_file(uint32_t dir_cluster)
{
    struct storage_node meta;
    int ret = fat32_find_in_dir(dir_cluster, "LEONACL.SYS", &meta);
    if (ret == -2) {
        return 0;
    }
    if (ret < 0) {
        return ret;
    }
    if (meta.type != LEONOS_FS_TYPE_FILE) {
        return 0;
    }
    ret = fat32_delete_dirent(dir_cluster, "LEONACL.SYS", 0);
    if (ret < 0) {
        return ret;
    }
    if (meta.first_cluster >= 2) {
        ret = fat32_free_chain(meta.first_cluster);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static int fat32_dir_is_empty(uint32_t dir_cluster)
{
    uint32_t cluster = dir_cluster;
    for (;;) {
        if (fat32_read_cluster(cluster, storage_cluster_buf) < 0) {
            return -5;
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return 1;
            }
            if (de->name[0] == 0xe5 || de->attr == FAT32_ATTR_LFN || (de->attr & 0x08u) != 0) {
                continue;
            }
            if (fat32_dirent_is_acl_metadata(de)) {
                continue;
            }
            if (de->name[0] == '.' &&
                (de->name[1] == ' ' || (de->name[1] == '.' && de->name[2] == ' '))) {
                continue;
            }
            return 0;
        }
        {
            uint32_t next = 0;
            if (fat32_read_fat_entry(cluster, &next) < 0) {
                return -5;
            }
            if (next >= FAT32_EOC) {
                return 1;
            }
            cluster = next;
        }
    }
}

static int fat32_iter_dir_entry(uint32_t dir_cluster, uint64_t index, struct leonos_dir_entry *entry)
{
    uint32_t cluster = dir_cluster;
    uint64_t emitted = 0;
    uint16_t lfn_parts[20][13];
    uint32_t lfn_count = 0;
    for (;;) {
        if (fat32_read_cluster(cluster, storage_cluster_buf) < 0) {
            return -5;
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return -2;
            }
            if (de->name[0] == 0xe5) {
                lfn_count = 0;
                continue;
            }
            if (de->attr == FAT32_ATTR_LFN) {
                const struct fat32_lfn *lfn = (const struct fat32_lfn *)(const void *)de;
                uint8_t order = lfn->order & 0x1fu;
                if (order && order <= 20) {
                    storage_memzero(lfn_parts[order - 1], sizeof(lfn_parts[0]));
                    uint32_t len = 0;
                    fat32_lfn_extract_utf16(lfn, lfn_parts[order - 1], &len, 13);
                    if (order > lfn_count) {
                        lfn_count = order;
                    }
                }
                continue;
            }
            if ((de->attr & 0x08u) != 0) {
                lfn_count = 0;
                continue;
            }
            char name[LEONOS_FS_NAME_LEN];
            if (lfn_count) {
                fat32_build_lfn_name(lfn_parts, lfn_count, name, sizeof(name));
            } else {
                struct storage_node tmp;
                (void)tmp;
                uint32_t pos = 0;
                for (uint32_t i = 0; i < 8 && de->name[i] != ' '; ++i) {
                    char ch = (char)de->name[i];
                    if (ch >= 'A' && ch <= 'Z') {
                        ch = (char)(ch - 'A' + 'a');
                    }
                    if (pos + 1 < sizeof(name)) {
                        name[pos++] = ch;
                    }
                }
                if (de->name[8] != ' ' && pos + 1 < sizeof(name)) {
                    name[pos++] = '.';
                }
                for (uint32_t i = 8; i < 11 && de->name[i] != ' '; ++i) {
                    char ch = (char)de->name[i];
                    if (ch >= 'A' && ch <= 'Z') {
                        ch = (char)(ch - 'A' + 'a');
                    }
                    if (pos + 1 < sizeof(name)) {
                        name[pos++] = ch;
                    }
                }
                name[pos] = 0;
            }
            lfn_count = 0;
            if ((name[0] == '.' && name[1] == 0) ||
                (name[0] == '.' && name[1] == '.' && name[2] == 0)) {
                continue;
            }
            if (storage_is_acl_metadata_name(name)) {
                continue;
            }
            if (emitted++ != index) {
                continue;
            }
            entry->type = (de->attr & FAT32_ATTR_DIRECTORY) ? LEONOS_FS_TYPE_DIR : LEONOS_FS_TYPE_FILE;
            storage_copy_text(entry->name, sizeof(entry->name), name);
            return 0;
        }
        uint32_t next = 0;
        if (fat32_read_fat_entry(cluster, &next) < 0) {
            return -5;
        }
        if (next >= FAT32_EOC) {
            return -2;
        }
        cluster = next;
    }
}

void storage_init(void)
{
    storage_memzero(g_volumes, sizeof(g_volumes));
    storage_memzero(g_install_disks, sizeof(g_install_disks));
    g_install_disk_count = 0;
    g_devfs_enabled = 1;
    g_installer_root_active = 0;
    g_active_volume = &g_volumes[0];

    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                if (pci_config_read16((uint8_t)bus, slot, func, 0x00) == 0xffffu) {
                    if (func == 0) {
                        break;
                    }
                    continue;
                }
                uint32_t class_reg = pci_config_read32((uint8_t)bus, slot, func, 0x08);
                uint8_t class_code = (uint8_t)(class_reg >> 24);
                uint8_t subclass = (uint8_t)(class_reg >> 16);
                uint8_t progif = (uint8_t)(class_reg >> 8);
                if (class_code != ATA_CLASS_MASS_STORAGE ||
                    subclass != ATA_SUBCLASS_SATA ||
                    progif != ATA_PROGIF_AHCI) {
                    continue;
                }
                uint32_t abar_lo = pci_config_read32((uint8_t)bus, slot, func, 0x24);
                uint32_t abar_hi = pci_config_read32((uint8_t)bus, slot, func, 0x28);
                uint64_t abar_phys = ((uint64_t)abar_hi << 32) | (abar_lo & ~0x0fu);
                if (!abar_phys) {
                    continue;
                }
                g_storage.bus = (uint8_t)bus;
                g_storage.slot = slot;
                g_storage.function = func;
                g_storage.abar = (struct ahci_hba_mem *)(uintptr_t)abar_phys;
                g_storage.abar->ghc |= AHCI_GHC_AE;
                for (uint8_t port = 0; port < 32; ++port) {
                    if ((g_storage.abar->pi & (1u << port)) == 0) {
                        continue;
                    }
                    struct ahci_hba_port *p = &g_storage.abar->ports[port];
                    uint32_t det = p->ssts & 0x0fu;
                    uint32_t ipm = (p->ssts >> 8) & 0x0fu;
                    if (det != AHCI_PORT_DET_PRESENT || ipm != AHCI_PORT_IPM_ACTIVE ||
                        p->sig != AHCI_PORT_SIG_ATA) {
                        continue;
                    }
                    uint32_t disk_id = g_install_disk_count;
                    if (disk_id < STORAGE_MAX_INSTALL_DISKS) {
                        g_install_disks[disk_id].present = true;
                        g_install_disks[disk_id].bus = (uint8_t)bus;
                        g_install_disks[disk_id].slot = slot;
                        g_install_disks[disk_id].function = func;
                        g_install_disks[disk_id].port = port;
                        g_install_disks[disk_id].abar = g_storage.abar;
                        g_install_disks[disk_id].hba_port = p;
                        g_install_disks[disk_id].sector_count = 0;
                        ++g_install_disk_count;
                    }
                    g_storage.hba_port = p;
                    g_storage.port = port;
                    g_storage.kind = STORAGE_VOLUME_AHCI;
                    g_storage.drive = 0;
                    if (ahci_setup_port(p) < 0) {
                        continue;
                    }
                    if (gpt_find_esp() < 0) {
                        continue;
                    }
                    if (fat32_mount() < 0) {
                        continue;
                    }
                    if (disk_id < STORAGE_MAX_INSTALL_DISKS) {
                        g_install_disks[disk_id].boot_root = 1;
                    }
                    g_storage.ready = true;
                    console_printf("[ntclks] storage ready ahci=%u:%u.%u port=%u esp_lba=%llu fat32_root=%u\n",
                                   g_storage.bus,
                                   g_storage.slot,
                                   g_storage.function,
                                   g_storage.port,
                                   (unsigned long long)g_storage.esp_start_lba,
                                   g_storage.root_cluster);
                    return;
                }
            }
        }
    }
    console_printf("[ntclks] storage init failed: no AHCI FAT32 ESP found\n");
}

void storage_apply_mount_policy(const struct leonos_mount_policy *policy)
{
    uint32_t count = 0;
    int root_ramdisk_status = 0;
    if (!policy || policy->version != LEONOS_MOUNT_POLICY_VERSION) {
        console_printf("[ntclks] storage using legacy mount policy fallback\n");
        storage_init();
        return;
    }

    console_printf("[ntclks] storage applying middlelayer mount policy entries=%u root=%u:/\n",
                   policy->count,
                   policy->root_drive);

    storage_init();
    g_devfs_enabled = 0;
    count = policy->count;
    if (count > LEONOS_MOUNT_MAX_ENTRIES) {
        count = LEONOS_MOUNT_MAX_ENTRIES;
    }

    for (uint32_t i = 0; i < count; ++i) {
        const struct leonos_mount_entry *entry = &policy->entries[i];
        switch (entry->kind) {
        case LEONOS_MOUNT_KIND_FAT32_BOOT:
            console_printf("[ntclks] mount policy %s kind=ahci-esp flags=0x%x\n",
                           entry->path,
                           entry->flags);
            break;
        case LEONOS_MOUNT_KIND_FAT32_RAMDISK:
            if (entry->drive != 0 || !entry->module_start || !entry->module_len) {
                root_ramdisk_status = -2;
            } else {
                root_ramdisk_status =
                    storage_mount_ramdisk_root((const void *)(uintptr_t)entry->module_start,
                                               entry->module_len);
            }
            console_printf("[ntclks] mount policy %s kind=ramdisk source=%s ret=%d\n",
                           entry->path,
                           entry->source,
                           root_ramdisk_status);
            if (root_ramdisk_status < 0) {
                storage_memzero(g_volumes, sizeof(g_volumes));
                g_active_volume = &g_volumes[0];
            }
            break;
        case LEONOS_MOUNT_KIND_DEVFS:
            if (entry->drive == 0) {
                g_devfs_enabled = 1;
            }
            console_printf("[ntclks] mount policy %s kind=devfs enabled=%u\n",
                           entry->path,
                           g_devfs_enabled);
            break;
        case LEONOS_MOUNT_KIND_TARGET_ESP:
            console_printf("[ntclks] mount policy %s kind=installer-target flags=0x%x\n",
                           entry->path,
                           entry->flags);
            break;
        default:
            break;
        }
    }
}

int storage_mount_ramdisk_root(const void *image, uint64_t len)
{
    struct storage_volume *root = &g_volumes[0];
    uint64_t copy_phys;
    uint32_t copy_pages;
    storage_memzero(root, sizeof(*root));
    g_active_volume = root;
    g_installer_root_active = 0;
    if (!image || len < SECTOR_SIZE || (len % SECTOR_SIZE) != 0) {
        return -22;
    }
    copy_pages = (uint32_t)((len + 4095ULL) / 4096ULL);
    copy_phys = mm_alloc_pages(copy_pages);
    if (!copy_phys) {
        return -12;
    }
    storage_memcpy((void *)(uintptr_t)copy_phys, image, (size_t)len);
    root->drive = 0;
    root->kind = STORAGE_VOLUME_RAM;
    root->ram_base = (uint8_t *)(uintptr_t)copy_phys;
    root->ram_bytes = len;
    root->esp_start_lba = 0;
    root->esp_sector_count = len / SECTOR_SIZE;
    if (fat32_mount() < 0) {
        storage_memzero(root, sizeof(*root));
        mm_free_pages(copy_phys, copy_pages);
        return -2;
    }
    root->ready = true;
    g_installer_root_active = 1;
    console_printf("[ntclks] storage installer root ready ramdisk=%p copy=%p bytes=%llu fat32_root=%u\n",
                   image,
                   (void *)(uintptr_t)copy_phys,
                   (unsigned long long)len,
                   root->root_cluster);
    return 0;
}

void storage_init_installer_root(const struct boot_info *boot)
{
    storage_memzero(g_volumes, sizeof(g_volumes));
    g_active_volume = &g_volumes[0];
    g_installer_root_active = 0;
    if (boot) {
        for (uint32_t i = 0; i < boot->module_count; ++i) {
            const struct boot_module *mod = &boot->modules[i];
            if (storage_text_eq(mod->name, "leonos-installer-root") &&
                mod->end > mod->start &&
                storage_mount_ramdisk_root((const void *)(uintptr_t)mod->start,
                                           mod->end - mod->start) == 0) {
                return;
            }
        }
    }
    console_printf("[ntclks] installer root ramdisk module not found\n");
}

bool storage_ready(void)
{
    return g_volumes[0].ready;
}

bool storage_installer_root_active(void)
{
    return g_installer_root_active != 0;
}

int storage_resolve_path(const char *cwd, const char *input, char *out, uint32_t cap)
{
    char parts[16][LEONOS_FS_NAME_LEN];
    uint32_t part_count = 0;
    char drive = '0';
    uint8_t use_cwd = 1;
    struct leonos_vfs_resolve_path query;
    if (!input || !out || cap < 4) {
        return -22;
    }
    query = (struct leonos_vfs_resolve_path){
        .cwd = cwd,
        .input = input,
        .out = out,
        .capacity = cap,
        .drive = 0,
        .node_kind = LEONOS_VFS_NODE_UNKNOWN,
        .flags = 0,
        .reserved = 0,
    };
    if (osmlayer_vfs_resolve_path(&query) == 0) {
        return 0;
    }
    if (input[0] >= '0' && input[0] < (char)('0' + STORAGE_MAX_DRIVES) &&
        input[1] == ':' && input[2] == '/') {
        drive = input[0];
        cwd = input;
        use_cwd = 0;
    } else if (input[0] == '/') {
        if (cwd && cwd[0] >= '0' && cwd[0] < (char)('0' + STORAGE_MAX_DRIVES) &&
            cwd[1] == ':' && cwd[2] == '/') {
            drive = cwd[0];
        }
        cwd = drive == '1' ? "1:/" : "0:/";
        use_cwd = 0;
    } else if (!cwd || cwd[0] < '0' || cwd[0] >= (char)('0' + STORAGE_MAX_DRIVES) ||
               cwd[1] != ':' || cwd[2] != '/') {
        cwd = "0:/";
    } else {
        drive = cwd[0];
    }

    const char *sources[2] = {
        use_cwd ? cwd + 3 : "",
        (input[0] >= '0' && input[0] < (char)('0' + STORAGE_MAX_DRIVES) &&
         input[1] == ':' && input[2] == '/') ? input + 3 : (input[0] == '/' ? input + 1 : input)
    };
    for (uint32_t src_i = 0; src_i < 2; ++src_i) {
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
    out[out_pos++] = drive;
    out[out_pos++] = ':';
    out[out_pos++] = '/';
    out[out_pos] = 0;
    for (uint32_t i = 0; i < part_count; ++i) {
        if (out_pos + storage_strlen(parts[i]) + 1 >= cap) {
            return -22;
        }
        if (out_pos > 3) {
            out[out_pos++] = '/';
        }
        for (uint32_t j = 0; parts[i][j]; ++j) {
            out[out_pos++] = parts[i][j];
        }
        out[out_pos] = 0;
    }
    return 0;
}

int storage_lookup_path(const char *path, struct storage_node *out)
{
    char resolved[LEONOS_FS_PATH_LEN];
    int drive;
    if (!storage_ready()) {
        return -2;
    }
    if (storage_resolve_path("0:/", path, resolved, sizeof(resolved)) < 0) {
        return -22;
    }
    drive = storage_drive_id_from_path(resolved);
    if (drive < 0) {
        return -22;
    }
    if (storage_select_drive((uint32_t)drive) < 0) {
        return -2;
    }
    if (storage_text_eq_ci(resolved, "0:/")) {
        if (out) {
            out->type = LEONOS_FS_TYPE_DIR;
            out->flags = STORAGE_NODE_FLAG_ROOT;
            out->first_cluster = g_storage.root_cluster;
            out->drive = (uint32_t)drive;
            out->size = 0;
            out->drive = (uint32_t)drive;
        }
        return 0;
    }
    if (g_devfs_enabled && storage_text_eq_ci(resolved, "0:/dev")) {
        if (out) {
            out->type = LEONOS_FS_TYPE_DIR;
            out->flags = STORAGE_NODE_FLAG_DEV_DIR;
            out->first_cluster = 0;
            out->drive = (uint32_t)drive;
            out->size = 0;
        }
        return 0;
    }
    if (g_devfs_enabled && storage_text_eq_ci(resolved, "0:/dev/fb0")) {
        if (out) {
            out->type = LEONOS_FS_TYPE_DEVICE;
            out->flags = STORAGE_NODE_FLAG_DEV_FB0;
            out->first_cluster = 0;
            out->drive = (uint32_t)drive;
            out->size = 0;
        }
        return 0;
    }

    struct storage_node node = {
        .type = LEONOS_FS_TYPE_DIR,
        .flags = STORAGE_NODE_FLAG_ROOT,
        .first_cluster = g_storage.root_cluster,
        .drive = (uint32_t)drive,
        .size = 0,
    };
    const char *p = resolved + 3;
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
                int ret = fat32_find_in_dir(node.first_cluster, name, &node);
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
    return 0;
}

int storage_read_node(const struct storage_node *node, uint64_t offset,
                      void *buf, uint32_t len, uint32_t *out_read)
{
    uint8_t *dst = (uint8_t *)buf;
    uint32_t done = 0;
    struct storage_volume *old_volume = 0;
    int ret;
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
        return 0;
    }
    ret = storage_select_node_drive(node, &old_volume);
    if (ret < 0) {
        return ret;
    }
    if (len > node->size - offset) {
        len = (uint32_t)(node->size - offset);
    }

    uint32_t cluster = node->first_cluster;
    uint64_t skip_clusters = offset / g_storage.cluster_bytes;
    uint32_t cluster_off = (uint32_t)(offset % g_storage.cluster_bytes);
    while (skip_clusters--) {
        uint32_t next = 0;
        if (fat32_read_fat_entry(cluster, &next) < 0) {
            storage_restore_volume(old_volume);
            return -5;
        }
        if (next >= FAT32_EOC) {
            storage_restore_volume(old_volume);
            return -5;
        }
        cluster = next;
    }

    while (done < len) {
        if (fat32_read_cluster(cluster, storage_cluster_buf) < 0) {
            storage_restore_volume(old_volume);
            return -5;
        }
        uint32_t take = g_storage.cluster_bytes - cluster_off;
        if (take > len - done) {
            take = len - done;
        }
        storage_memcpy(dst + done, storage_cluster_buf + cluster_off, take);
        done += take;
        cluster_off = 0;
        if (done >= len) {
            break;
        }
        uint32_t next = 0;
        if (fat32_read_fat_entry(cluster, &next) < 0) {
            storage_restore_volume(old_volume);
            return -5;
        }
        if (next >= FAT32_EOC) {
            break;
        }
        cluster = next;
    }
    if (out_read) {
        *out_read = done;
    }
    storage_restore_volume(old_volume);
    return 0;
}

int storage_readdir_node(const struct storage_node *node, uint64_t *cursor,
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
    ret = storage_select_node_drive(node, &old_volume);
    if (ret < 0) {
        return ret;
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
    end64 = offset + len;
    if (end64 > 0xffffffffu || end64 < offset) {
        return -28;
    }
    total_len = (uint32_t)end64;
    if (offset == 0) {
        ret = storage_write_file(path, buf, total_len);
        if (ret < 0) {
            return ret;
        }
        if (out_written) {
            *out_written = len;
        }
        return 0;
    }
    if (len == 0) {
        return 0;
    }
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
        if (node.first_cluster >= 2) {
            ret = fat32_collect_chain(node.first_cluster, storage_old_chain, &old_count);
            if (ret < 0) {
                return ret;
            }
        }
        for (uint32_t i = old_count; i < clusters_needed; ++i) {
            ret = fat32_find_free_cluster(&storage_old_chain[i]);
            if (ret < 0) {
                return ret;
            }
            if (fat32_write_fat_entry(storage_old_chain[i], FAT32_EOC) < 0) {
                return -5;
            }
            storage_memzero(storage_cluster_buf, g_storage.cluster_bytes);
            if (fat32_write_cluster(storage_old_chain[i], storage_cluster_buf) < 0) {
                return -5;
            }
        }
        for (uint32_t i = 0; i < clusters_needed; ++i) {
            uint32_t next = (i + 1u < clusters_needed) ? storage_old_chain[i + 1u] : FAT32_EOC;
            if (fat32_write_fat_entry(storage_old_chain[i], next) < 0) {
                return -5;
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
                if (fat32_read_cluster(storage_old_chain[cluster_index], storage_cluster_buf) < 0) {
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

        first_cluster = clusters_needed ? storage_old_chain[0] : 0;
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
        return 0;
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
    return 0;
}

int storage_write_file(const char *path, const void *buf, uint32_t len)
{
    char resolved[LEONOS_FS_PATH_LEN];
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
    if (!storage_ready()) {
        return -2;
    }
    if (storage_resolve_path("0:/", path, resolved, sizeof(resolved)) < 0) {
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
        if (fat32_find_dirent_ref_in_dir(parent_node.first_cluster, name, &ref) < 0) {
            return -5;
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

    if (existing.first_cluster >= 2) {
        ret = fat32_collect_chain(existing.first_cluster, storage_old_chain, &old_count);
        if (ret < 0) {
            return ret;
        }
    }

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

    if (old_count > clusters_needed) {
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

int storage_mkdir(const char *path)
{
    char resolved[LEONOS_FS_PATH_LEN];
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
    if (storage_resolve_path("0:/", path, resolved, sizeof(resolved)) < 0 ||
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
    if (fat32_write_fat_entry(cluster, FAT32_EOC) < 0) {
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
        dotdot->first_cluster_hi = (uint16_t)(parent_node.first_cluster >> 16);
        dotdot->first_cluster_lo = (uint16_t)(parent_node.first_cluster & 0xffffu);
    }
    if (fat32_write_cluster(cluster, storage_cluster_buf) < 0) {
        (void)fat32_write_fat_entry(cluster, 0);
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
    if (storage_resolve_path("0:/", path, resolved, sizeof(resolved)) < 0 ||
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
    if (node.type == LEONOS_FS_TYPE_DIR) {
        return -21;
    }
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

int storage_rmdir(const char *path)
{
    char resolved[LEONOS_FS_PATH_LEN];
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
    if (storage_resolve_path("0:/", path, resolved, sizeof(resolved)) < 0 ||
        storage_parent_path(resolved, parent, sizeof(parent), name, sizeof(name)) < 0) {
        return -22;
    }
    if (storage_text_eq_ci(resolved, "0:/") ||
        (g_devfs_enabled && storage_text_eq_ci(resolved, "0:/dev"))) {
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
    if (storage_resolve_path("0:/", old_path, old_resolved, sizeof(old_resolved)) < 0 ||
        storage_resolve_path("0:/", new_path, new_resolved, sizeof(new_resolved)) < 0 ||
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
    if (g_devfs_enabled && node.drive == 0 &&
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

static void storage_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint32_t storage_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (uint32_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static int install_write_sectors(struct install_disk_state *disk, uint64_t lba,
                                 uint32_t sector_count, const void *buffer)
{
    const uint8_t *src = (const uint8_t *)buffer;
    if (!disk || !disk->present || !disk->hba_port || !buffer) {
        return -22;
    }
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count, AHCI_MAX_SECTORS);
        int ret = ahci_write_lba(disk->hba_port, lba, chunk, src);
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        sector_count -= chunk;
        src += chunk * SECTOR_SIZE;
    }
    return 0;
}

static int install_clear_sectors(struct install_disk_state *disk, uint64_t lba,
                                 uint64_t sector_count)
{
    storage_memzero(storage_scratch, sizeof(storage_scratch));
    while (sector_count) {
        uint32_t chunk = (uint32_t)min_u64(sector_count, STORAGE_SCRATCH_SECTORS);
        int ret = install_write_sectors(disk, lba, chunk, storage_scratch);
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        sector_count -= chunk;
    }
    return 0;
}

static uint32_t install_choose_fat32_spc(uint64_t total_sectors)
{
    if (total_sectors < 16ULL * 1024ULL * 1024ULL) {
        return 8;
    }
    if (total_sectors < 32ULL * 1024ULL * 1024ULL) {
        return 16;
    }
    if (total_sectors < 64ULL * 1024ULL * 1024ULL) {
        return 32;
    }
    return 32;
}

static int install_format_fat32(struct install_disk_state *disk, uint64_t start_lba,
                                uint64_t sector_count)
{
    const uint32_t reserved = 32;
    const uint32_t fat_count = 2;
    uint32_t total_sectors;
    uint32_t sectors_per_cluster;
    uint32_t fat_size = 1;
    uint32_t data_start;
    uint32_t data_sectors;
    uint32_t clusters = 0;
    if (!disk || sector_count > 0xffffffffULL || sector_count < 65536ULL) {
        return -28;
    }
    total_sectors = (uint32_t)sector_count;
    sectors_per_cluster = install_choose_fat32_spc(sector_count);
    for (uint32_t i = 0; i < 16; ++i) {
        uint32_t next_fat_size;
        if (total_sectors <= reserved + fat_count * fat_size) {
            return -28;
        }
        data_sectors = total_sectors - reserved - fat_count * fat_size;
        clusters = data_sectors / sectors_per_cluster;
        next_fat_size = ((clusters + 2u) * 4u + SECTOR_SIZE - 1u) / SECTOR_SIZE;
        if (next_fat_size == fat_size) {
            break;
        }
        fat_size = next_fat_size;
    }
    data_start = reserved + fat_count * fat_size;
    if (clusters < 2 || data_start >= total_sectors) {
        return -28;
    }

    if (install_clear_sectors(disk, start_lba, (uint64_t)data_start + sectors_per_cluster) < 0) {
        return -5;
    }

    storage_memzero(storage_scratch, SECTOR_SIZE);
    struct fat32_bpb *bpb = (struct fat32_bpb *)(void *)storage_scratch;
    bpb->jump[0] = 0xeb;
    bpb->jump[1] = 0x58;
    bpb->jump[2] = 0x90;
    storage_memcpy(bpb->oem, "LEONOS4 ", 8);
    bpb->bytes_per_sector = SECTOR_SIZE;
    bpb->sectors_per_cluster = (uint8_t)sectors_per_cluster;
    bpb->reserved_sector_count = reserved;
    bpb->fat_count = fat_count;
    bpb->root_entry_count = 0;
    bpb->total_sectors16 = 0;
    bpb->media = 0xf8;
    bpb->fat_size16 = 0;
    bpb->sectors_per_track = 63;
    bpb->head_count = 255;
    bpb->hidden_sectors = (uint32_t)start_lba;
    bpb->total_sectors32 = total_sectors;
    bpb->fat_size32 = fat_size;
    bpb->root_cluster = 2;
    bpb->fs_info = 1;
    bpb->backup_boot_sector = 6;
    bpb->drive_number = 0x80;
    bpb->boot_signature = 0x29;
    bpb->volume_id = 0x4c454f34u;
    storage_memcpy(bpb->volume_label, "LEONOS4    ", 11);
    storage_memcpy(bpb->fs_type, "FAT32   ", 8);
    storage_scratch[510] = 0x55;
    storage_scratch[511] = 0xaa;
    if (install_write_sectors(disk, start_lba, 1, storage_scratch) < 0 ||
        install_write_sectors(disk, start_lba + 6, 1, storage_scratch) < 0) {
        return -5;
    }

    storage_memzero(storage_scratch, SECTOR_SIZE);
    storage_put_u32(storage_scratch + 0, 0x41615252u);
    storage_put_u32(storage_scratch + 484, 0x61417272u);
    storage_put_u32(storage_scratch + 488, 0xffffffffu);
    storage_put_u32(storage_scratch + 492, 3u);
    storage_put_u32(storage_scratch + 508, 0xaa550000u);
    if (install_write_sectors(disk, start_lba + 1, 1, storage_scratch) < 0 ||
        install_write_sectors(disk, start_lba + 7, 1, storage_scratch) < 0) {
        return -5;
    }

    for (uint32_t fat = 0; fat < fat_count; ++fat) {
        uint64_t fat_lba = start_lba + reserved + (uint64_t)fat * fat_size;
        storage_memzero(storage_scratch, SECTOR_SIZE);
        storage_put_u32(storage_scratch + 0, 0x0ffffff8u);
        storage_put_u32(storage_scratch + 4, 0xffffffffu);
        storage_put_u32(storage_scratch + 8, FAT32_EOC);
        if (install_write_sectors(disk, fat_lba, 1, storage_scratch) < 0) {
            return -5;
        }
    }

    return 0;
}

static void install_utf16_name(uint16_t dst[36], const char *name)
{
    uint32_t i = 0;
    for (; i < 36 && name && name[i]; ++i) {
        dst[i] = (uint16_t)(uint8_t)name[i];
    }
    for (; i < 36; ++i) {
        dst[i] = 0;
    }
}

static int install_write_gpt(struct install_disk_state *disk, uint64_t sector_count,
                             uint64_t *out_first_lba, uint64_t *out_sector_count)
{
    uint8_t disk_guid[16];
    uint8_t part_guid[16];
    uint64_t last_lba;
    uint64_t backup_entries_lba;
    uint64_t first_usable;
    uint64_t last_usable;
    uint32_t table_bytes = GPT_ENTRY_COUNT * GPT_ENTRY_SIZE;
    uint32_t table_sectors = table_bytes / SECTOR_SIZE;
    uint32_t table_crc;
    if (!disk || !out_first_lba || !out_sector_count || sector_count < 131072ULL) {
        return -28;
    }
    storage_make_guid(disk_guid, 0x4c3447494449534bULL, disk, sector_count);
    storage_make_guid(part_guid, 0x4c34474944504152ULL, disk, sector_count);
    last_lba = sector_count - 1u;
    backup_entries_lba = last_lba - table_sectors;
    first_usable = INSTALL_ESP_FIRST_LBA;
    last_usable = backup_entries_lba - 1u;
    if (last_usable <= first_usable || last_usable - first_usable + 1u < 65536ULL) {
        return -28;
    }

    if (install_clear_sectors(disk, 0, first_usable) < 0 ||
        install_clear_sectors(disk, backup_entries_lba, table_sectors + 1u) < 0) {
        return -5;
    }

    storage_memzero(storage_scratch, SECTOR_SIZE);
    storage_scratch[446 + 4] = 0xee;
    storage_put_u32(storage_scratch + 446 + 8, 1);
    storage_put_u32(storage_scratch + 446 + 12,
                    sector_count - 1u > 0xffffffffULL ? 0xffffffffu : (uint32_t)(sector_count - 1u));
    storage_scratch[510] = 0x55;
    storage_scratch[511] = 0xaa;
    if (install_write_sectors(disk, 0, 1, storage_scratch) < 0) {
        return -5;
    }

    storage_memzero(storage_cluster_buf, table_bytes);
    struct gpt_entry *entry = (struct gpt_entry *)(void *)storage_cluster_buf;
    storage_memcpy(entry->type_guid, esp_guid, sizeof(entry->type_guid));
    storage_memcpy(entry->unique_guid, part_guid, sizeof(entry->unique_guid));
    entry->first_lba = first_usable;
    entry->last_lba = last_usable;
    entry->attrs = 0;
    install_utf16_name(entry->name, "LeonOS 4 ESP");
    table_crc = storage_crc32(storage_cluster_buf, table_bytes);
    if (install_write_sectors(disk, 2, table_sectors, storage_cluster_buf) < 0 ||
        install_write_sectors(disk, backup_entries_lba, table_sectors, storage_cluster_buf) < 0) {
        return -5;
    }

    storage_memzero(storage_scratch, SECTOR_SIZE);
    struct gpt_header *hdr = (struct gpt_header *)(void *)storage_scratch;
    hdr->signature = 0x5452415020494645ULL;
    hdr->revision = 0x00010000u;
    hdr->header_size = sizeof(struct gpt_header);
    hdr->current_lba = 1;
    hdr->backup_lba = last_lba;
    hdr->first_usable_lba = first_usable;
    hdr->last_usable_lba = last_usable;
    storage_memcpy(hdr->disk_guid, disk_guid, sizeof(hdr->disk_guid));
    hdr->partition_entries_lba = 2;
    hdr->partition_entry_count = GPT_ENTRY_COUNT;
    hdr->partition_entry_size = GPT_ENTRY_SIZE;
    hdr->partition_entries_crc32 = table_crc;
    hdr->header_crc32 = 0;
    hdr->header_crc32 = storage_crc32(hdr, hdr->header_size);
    if (install_write_sectors(disk, 1, 1, storage_scratch) < 0) {
        return -5;
    }

    hdr->header_crc32 = 0;
    hdr->current_lba = last_lba;
    hdr->backup_lba = 1;
    hdr->partition_entries_lba = backup_entries_lba;
    hdr->header_crc32 = storage_crc32(hdr, hdr->header_size);
    if (install_write_sectors(disk, last_lba, 1, storage_scratch) < 0) {
        return -5;
    }

    *out_first_lba = first_usable;
    *out_sector_count = last_usable - first_usable + 1u;
    return 0;
}

int storage_install_list_disks(struct leonos_install_disk *disks,
                               uint32_t capacity, uint32_t *out_count)
{
    if (!out_count) {
        return -22;
    }
    *out_count = g_install_disk_count;
    if (capacity > g_install_disk_count) {
        capacity = g_install_disk_count;
    }
    if (capacity && !disks) {
        return -22;
    }
    for (uint32_t i = 0; i < capacity; ++i) {
        struct install_disk_state *src = &g_install_disks[i];
        if (src->present && src->sector_count == 0) {
            uint64_t sectors = 0;
            if (storage_install_identify(src, &sectors) == 0) {
                src->sector_count = sectors;
            }
        }
        disks[i].id = i;
        disks[i].port = src->port;
        disks[i].sector_size = SECTOR_SIZE;
        disks[i].flags = 0;
        if (src->boot_root) {
            disks[i].flags |= LEONOS_INSTALL_DISK_FLAG_BOOT_ROOT;
        }
        if (src->target_mounted) {
            disks[i].flags |= LEONOS_INSTALL_DISK_FLAG_TARGET_MOUNTED;
        }
        disks[i].sector_count = src->sector_count;
        storage_copy_text(disks[i].name, sizeof(disks[i].name), "SATA/AHCI Disk");
    }
    return 0;
}

int storage_install_format_esp(uint32_t disk_id)
{
    uint64_t sector_count;
    uint64_t esp_lba = 0;
    uint64_t esp_sectors = 0;
    int ret;
    if (disk_id >= g_install_disk_count || !g_install_disks[disk_id].present) {
        return -2;
    }
    struct install_disk_state *disk = &g_install_disks[disk_id];
    if (ahci_setup_port(disk->hba_port) < 0) {
        return -5;
    }
    sector_count = disk->sector_count;
    if (sector_count == 0) {
        ret = storage_install_identify(disk, &sector_count);
        if (ret < 0) {
            return ret;
        }
        disk->sector_count = sector_count;
    }
    ret = install_write_gpt(disk, sector_count, &esp_lba, &esp_sectors);
    if (ret < 0) {
        return ret;
    }
    ret = install_format_fat32(disk, esp_lba, esp_sectors);
    if (ret < 0) {
        return ret;
    }
    disk->target_mounted = 0;
    storage_memzero(&g_volumes[1], sizeof(g_volumes[1]));
    if (g_active_volume == &g_volumes[1]) {
        g_active_volume = &g_volumes[0];
    }
    return 0;
}

int storage_install_mount_target(uint32_t disk_id)
{
    if (disk_id >= g_install_disk_count || !g_install_disks[disk_id].present) {
        return -2;
    }
    struct install_disk_state *disk = &g_install_disks[disk_id];
    struct storage_volume *target = &g_volumes[1];
    struct storage_volume *old = g_active_volume;
    storage_memzero(target, sizeof(*target));
    target->drive = 1;
    target->kind = STORAGE_VOLUME_AHCI;
    target->bus = disk->bus;
    target->slot = disk->slot;
    target->function = disk->function;
    target->port = disk->port;
    target->abar = disk->abar;
    target->hba_port = disk->hba_port;
    if (ahci_setup_port(disk->hba_port) < 0) {
        g_active_volume = old;
        return -5;
    }
    g_active_volume = target;
    int ret = gpt_find_esp();
    if (ret == 0) {
        ret = fat32_mount();
    }
    if (ret == 0) {
        target->ready = true;
        disk->target_mounted = 1;
        console_printf("[ntclks] installer target mounted drive=1 disk=%u port=%u esp_lba=%llu\n",
                       disk_id,
                       disk->port,
                       (unsigned long long)target->esp_start_lba);
    } else {
        storage_memzero(target, sizeof(*target));
        disk->target_mounted = 0;
    }
    g_active_volume = old;
    return ret;
}

void storage_boot_identity(struct leonos_machine_identity *identity)
{
    const struct storage_volume *root = &g_volumes[0];
    if (!identity) {
        return;
    }
    if (identity->version == 0) {
        identity->version = LEONOS_MACHINE_IDENTITY_VERSION;
    }
    if (!root->ready || !root->has_gpt_identity) {
        return;
    }
    storage_format_guid(root->gpt_disk_guid, identity->boot_disk_guid,
                        sizeof(identity->boot_disk_guid));
    storage_format_guid(root->esp_unique_guid, identity->boot_partition_guid,
                        sizeof(identity->boot_partition_guid));
    identity->flags |= LEONOS_MACHINE_IDENTITY_FLAG_BOOT_DISK_GUID |
                       LEONOS_MACHINE_IDENTITY_FLAG_BOOT_PARTITION_GUID;
    if (!(identity->flags & LEONOS_MACHINE_IDENTITY_FLAG_PLATFORM_UUID)) {
        storage_copy_text(identity->source, sizeof(identity->source),
                          "boot-gpt-guid");
    }
}
