#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/ntclks/procfs.c"

static struct task current = {.pid = 42, .name = "test", .uid = 1000};
uint32_t address_space_user_resident_kib(const struct address_space *as)
{ assert(as == sched_task_as(&current)); return 12; }
struct task *sched_current_task(void) { return &current; }
uint32_t sched_current_pid(void) { return current.pid; }
struct task *sched_find(uint32_t pid) { return pid == current.pid ? &current : NULL; }
uint32_t sched_snapshot(struct task_snapshot_info *out, uint32_t capacity, uint64_t *tick)
{
    if (tick) *tick = 123;
    if (out && capacity) out[0] = (struct task_snapshot_info){.pid = current.pid};
    return 1;
}
uint64_t time_uptime_ms(void) { return 1234; }
uint64_t mm_total_memory_kib(void) { return 1024; }
uint64_t mm_free_memory_kib(void) { return 512; }
uint32_t smp_cpu_count(void) { return 1; }
void sched_cpu_ticks(uint64_t *busy, uint64_t *idle) { *busy = 10; *idle = 20; }
void sched_cpu_ticks_per_cpu(uint64_t *busy, uint64_t *idle, uint32_t capacity)
{ if (capacity) sched_cpu_ticks(busy, idle); }
const struct leonos_system_info *ntclks_system_info(void) { return NULL; }

int main(void)
{
    struct storage_node node;
    assert(proc_lookup("/proc/42", &node) == 0 && node.type == LEONOS_FS_TYPE_DIR);
    assert(proc_lookup("/proc/self", &node) == 0 && node.type == LEONOS_FS_TYPE_DIR);
    assert(proc_lookup("/proc/43", &node) == -2);
    assert(proc_lookup("/proc/4294967338", &node) == -2);
    assert(proc_lookup("/proc/42unknown/stat", &node) == -2);
    assert(proc_lookup("/proc/42/stat", &node) == 0 && node.type == LEONOS_FS_TYPE_FILE);
    char contents[512];
    uint32_t got;
    assert(proc_read("/proc/42/stat", 0, contents, sizeof(contents) - 1, &got) == 0 && got);
    contents[got] = 0;
    assert(strstr(contents, "42 (test)"));
    assert(proc_lookup("/proc/42/status", &node) == 0 && node.type == LEONOS_FS_TYPE_FILE);
    current.euid = current.suid = 1001;
    current.as.cr3 = 4096;
    current.gid = 4000000000U; current.egid = current.sgid = 4000000001U;
    assert(proc_read("/proc/42/status", 0, contents, sizeof(contents) - 1, &got) == 0 && got);
    contents[got] = 0;
    assert(strstr(contents, "Name:\ttest\n"));
    assert(strstr(contents, "Uid:\t1000\t1001\t1001\t1001\n"));
    assert(strstr(contents, "Gid:\t4000000000\t4000000001\t4000000001\t4000000001\n"));
    assert(strstr(contents, "VmRSS:\t12 kB\n"));
    uint64_t offset = 0;
    struct leonos_dir_entry entry;
    assert(proc_readdir("/proc/42", &offset, &entry) == 1 && !strcmp(entry.name, "stat"));
    assert(proc_readdir("/proc/42", &offset, &entry) == 1 && !strcmp(entry.name, "cmdline"));
    assert(proc_readdir("/proc/42", &offset, &entry) == 1 && !strcmp(entry.name, "status"));
    assert(proc_readdir("/proc/42", &offset, &entry) == 0);
    offset = 0;
    int found = 0;
    while (proc_readdir("/proc", &offset, &entry) > 0) {
        char path[256];
        snprintf(path, sizeof(path), "/proc/%s", entry.name);
        assert(proc_lookup(path, &node) == 0 && entry.type == node.type);
        if (!strcmp(entry.name, "42")) found = 1;
    }
    assert(found);
    puts("PASS procfs task directory lookup, traversal, enumeration and overflow rejection");
}
