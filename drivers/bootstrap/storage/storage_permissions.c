/* ext2 ownership and mode live in the inode, including Linux's high uid/gid
 * words. Other backends use the versioned metadata sidecar. */
int storage_inode_permissions(const struct storage_node *node,
                              struct leonos_permissions *value, bool write)
{
    struct storage_volume *previous = NULL;
    struct ext2_inode inode;
    uint64_t flags;
    int ret;
    if (!node || !value) return -22;
    if (!(node->flags & STORAGE_NODE_FLAG_EXT2)) return -95;
    kernel_execution_lock_irqsave(&flags);
    ret = storage_select_node_volume(node, &previous);
    if (ret < 0) goto out;
    ret = ext2_read_inode(node->first_cluster, &inode);
    if (ret < 0) goto restore;
    if (write) {
        struct leonos_time_info now;
        if (time_wall_clock(&now) == 0) inode.ctime = (uint32_t)now.unix_seconds;
        inode.mode = (inode.mode & 0170000u) | (value->mode & 07777u);
        inode.uid = (uint16_t)value->uid;
        inode.gid = (uint16_t)value->gid;
        inode.osd2[4] = (uint8_t)(value->uid >> 16);
        inode.osd2[5] = (uint8_t)(value->uid >> 24);
        inode.osd2[6] = (uint8_t)(value->gid >> 16);
        inode.osd2[7] = (uint8_t)(value->gid >> 24);
        ret = ext2_write_inode(node->first_cluster, &inode);
    } else {
        value->mode = inode.mode & 07777u;
        value->uid = inode.uid | ((uint32_t)inode.osd2[4] << 16) | ((uint32_t)inode.osd2[5] << 24);
        value->gid = inode.gid | ((uint32_t)inode.osd2[6] << 16) | ((uint32_t)inode.osd2[7] << 24);
    }
restore:
    storage_restore_volume(previous);
out:
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}

int storage_inode_stat(const struct storage_node *node, struct linux_stat_abi *value)
{
    struct storage_volume *previous = NULL;
    struct ext2_inode inode;
    uint64_t flags;
    if (!node || !value) return -22;
    if (!(node->flags & STORAGE_NODE_FLAG_EXT2)) return -95;
    kernel_execution_lock_irqsave(&flags);
    int ret = storage_select_node_volume(node, &previous);
    if (!ret) {
        ret = ext2_read_inode(node->first_cluster, &inode);
        if (!ret) {
            *value = (struct linux_stat_abi){0};
            value->st_dev = (uint64_t)node->volume_id + 1;
            value->st_ino = node->first_cluster;
            value->st_nlink = inode.links_count;
            value->st_mode = inode.mode;
            value->st_uid = inode.uid | ((uint32_t)inode.osd2[4] << 16) | ((uint32_t)inode.osd2[5] << 24);
            value->st_gid = inode.gid | ((uint32_t)inode.osd2[6] << 16) | ((uint32_t)inode.osd2[7] << 24);
            value->st_size = inode.size_lo;
            if ((inode.mode & LINUX_S_IFMT) == LINUX_S_IFREG) value->st_size |= (uint64_t)inode.size_high << 32;
            value->st_blksize = g_storage.ext2_block_size;
            value->st_blocks = inode.blocks_512;
            value->atime_sec = (int32_t)inode.atime;
            value->mtime_sec = (int32_t)inode.mtime;
            value->ctime_sec = (int32_t)inode.ctime;
            if ((inode.mode & LINUX_S_IFMT) == LINUX_S_IFCHR || (inode.mode & LINUX_S_IFMT) == LINUX_S_IFBLK) {
                uint32_t dev = inode.block[0];
                if (dev) value->st_rdev = dev;
                else value->st_rdev = inode.block[1];
            }
        }
        storage_restore_volume(previous);
    }
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}

int storage_inode_utimensat(const struct storage_node *node, int64_t atime, int64_t mtime,
                            bool set_atime, bool set_mtime)
{
    struct storage_volume *previous = NULL;
    struct ext2_inode inode;
    uint64_t flags;
    int ret;
    if (!node || !(node->flags & STORAGE_NODE_FLAG_EXT2)) return -95;
    kernel_execution_lock_irqsave(&flags);
    ret = storage_select_node_volume(node, &previous);
    if (!ret) ret = ext2_read_inode(node->first_cluster, &inode);
    if (!ret) {
        if (set_atime) inode.atime = (uint32_t)atime;
        if (set_mtime) inode.mtime = (uint32_t)mtime;
        ret = ext2_write_inode(node->first_cluster, &inode);
    }
    storage_restore_volume(previous);
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}
