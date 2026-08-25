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
