#include <ntclks/page_cache.h>
#include <ntclks/permissions.h>

/* Reference ext2 inodes independently of directory links. The storage execution
 * lock protects this list as well as lookup, unlink and inode allocation. */
struct storage_inode_ref {
    struct storage_inode_ref *next;
    struct storage_node node;
    uint64_t references;
    bool unlinked;
};
static struct storage_inode_ref *storage_inode_refs;
static int ext2_read_inode(uint32_t number, struct ext2_inode *out);
static int ext2_destroy_inode(uint32_t inode_no, uint8_t directory);
static int ext2_write_node(const struct storage_node *node, uint64_t offset, const void *buffer,
                           uint32_t len, uint32_t *out_written);
static int ext2_truncate_file(const struct storage_node *node, uint64_t length);

static struct storage_inode_ref *storage_inode_find(uint32_t volume, uint32_t inode)
{
    for (struct storage_inode_ref *ref = storage_inode_refs; ref; ref = ref->next)
        if (ref->node.volume_id == volume && ref->node.first_cluster == inode) return ref;
    return NULL;
}

static bool storage_inode_volume_busy(uint32_t volume)
{
    for (struct storage_inode_ref *ref = storage_inode_refs; ref; ref = ref->next)
        if (ref->node.volume_id == volume) return true;
    return false;
}

int storage_inode_get(const struct storage_node *node, struct storage_inode_ref **out)
{
    if (!node || !out) return -22;
    *out = NULL;
    if (!(node->flags & STORAGE_NODE_FLAG_EXT2)) return 0;
    uint64_t flags;
    kernel_execution_lock_irqsave(&flags);
    struct storage_inode_ref *ref = storage_inode_find(node->volume_id, node->first_cluster);
    if (!ref) {
        struct storage_volume *previous = NULL;
        struct ext2_inode inode;
        int ret = storage_select_node_volume(node, &previous);
        if (!ret) ret = ext2_read_inode(node->first_cluster, &inode);
        storage_restore_volume(previous);
        if (ret < 0 || !inode.mode) {
            kernel_execution_unlock_irqrestore(flags);
            return ret < 0 ? ret : -2;
        }
        ref = kernel_malloc(sizeof(*ref));
        if (!ref) {
            kernel_execution_unlock_irqrestore(flags);
            return -12;
        }
        *ref = (struct storage_inode_ref){.node = *node, .next = storage_inode_refs};
        storage_inode_refs = ref;
    }
    __atomic_add_fetch(&ref->references, 1, __ATOMIC_RELAXED);
    *out = ref;
    kernel_execution_unlock_irqrestore(flags);
    return 0;
}

void storage_inode_retain(struct storage_inode_ref *reference)
{
    if (!reference) return;
    __atomic_add_fetch(&reference->references, 1, __ATOMIC_RELAXED);
}

int storage_inode_put(struct storage_inode_ref *reference)
{
    if (!reference) return 0;
    uint64_t flags;
    kernel_execution_lock_irqsave(&flags);
    if (reference->references && __atomic_sub_fetch(&reference->references, 1, __ATOMIC_ACQ_REL)) {
        kernel_execution_unlock_irqrestore(flags);
        return 0;
    }
    struct storage_volume *previous = NULL;
    struct ext2_inode inode;
    int ret = 0;
    bool saved_async = storage_io_async_context;
    if (reference->unlinked) {
        /* Final close/munmap cannot be replayed after reference release. */
        storage_io_async_context = false;
        ret = storage_select_node_volume(&reference->node, &previous);
        if (!ret) ret = ext2_read_inode(reference->node.first_cluster, &inode);
        if (!ret && !inode.links_count)
            ret = ext2_destroy_inode(reference->node.first_cluster, reference->node.type == LEONOS_FS_TYPE_DIR);
        storage_io_async_context = saved_async;
    }
    storage_restore_volume(previous);
    if (!ret) {
        struct storage_inode_ref **link = &storage_inode_refs;
        while (*link && *link != reference) link = &(*link)->next;
        if (*link) *link = reference->next;
        kernel_free(reference);
    } else {
        /* Keep failed deletion metadata reserved; do not recycle this inode or
         * unmount the volume as though cleanup had succeeded. */
        console_printf("[storage] inode release failed volume=%u inode=%u ret=%d\n",
                       reference->node.volume_id, reference->node.first_cluster, ret);
    }
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}

int storage_inode_refresh(struct storage_node *node)
{
    if (!node) return -22;
    if (!(node->flags & STORAGE_NODE_FLAG_EXT2)) return 0;
    struct linux_stat_abi info;
    int ret = storage_inode_stat(node, &info);
    if (!ret) node->size = info.st_size;
    return ret;
}

int storage_write_held_node(struct storage_node *node, uint64_t offset,
                            const void *buffer, uint32_t length, uint32_t *written)
{
    if (written) *written = 0;
    if (!node || !(node->flags & STORAGE_NODE_FLAG_EXT2)) return -95;
    uint64_t flags;
    struct storage_volume *previous = NULL;
    kernel_execution_lock_irqsave(&flags);
    int ret = storage_select_node_volume(node, &previous);
    if (!ret) {
        storage_begin_mutation();
        ret = ext2_write_node(node, offset, buffer, length, written);
    }
    storage_restore_volume(previous);
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}

int storage_truncate_held_node(struct storage_node *node, uint64_t length)
{
    if (!node || !(node->flags & STORAGE_NODE_FLAG_EXT2)) return -95;
    uint64_t flags;
    struct storage_volume *previous = NULL;
    kernel_execution_lock_irqsave(&flags);
    int ret = storage_select_node_volume(node, &previous);
    if (!ret) {
        storage_begin_mutation();
        ret = ext2_truncate_file(node, length);
    }
    if (!ret) node->size = length;
    storage_restore_volume(previous);
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}
