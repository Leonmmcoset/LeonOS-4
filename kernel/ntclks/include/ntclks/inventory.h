#ifndef NTCLKS_INVENTORY_H
#define NTCLKS_INVENTORY_H
#include <ntclks/types.h>
struct cpu_inventory {
    char vendor[13], model_name[49];
    uint32_t family, model, stepping, apic_id, package_id, core_id;
    uint32_t base_mhz, max_mhz, leaf1_edx, leaf1_ecx;
    bool valid;
};
/** @brief Capture CPUID on the named logical CPU before publishing it online. */
void cpu_inventory_capture(uint32_t cpu);
/** @brief Return immutable boot CPUID data for a captured logical CPU, otherwise NULL. */
const struct cpu_inventory *cpu_inventory_get(uint32_t cpu);
/** @brief Read a byte range of Linux x86 cpuinfo; only online, captured CPUs appear. */
int cpu_inventory_read(uint64_t offset, void *buffer, uint32_t capacity, uint32_t *out_read);
struct storage_node;
struct leonos_dir_entry;
/** @brief Read-only sysfs lookup; final symlinks remain un-followed. */
int sysfs_lookup(const char *path, struct storage_node *out);
/** @brief Read real hardware inventory attributes at a byte offset. */
int sysfs_read(const char *path, uint64_t offset, void *buffer, uint32_t length, uint32_t *out_read);
/** @brief Return symlink bytes without NUL or negative errno. */
int sysfs_readlink(const char *path, char *buffer, uint32_t capacity);
/** @brief Enumerate sysfs; returns one entry, zero at EOF, or negative errno. */
int sysfs_readdir(const char *path, uint64_t *offset, struct leonos_dir_entry *entry);
#endif
