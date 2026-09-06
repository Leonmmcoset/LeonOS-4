/*
 * LeonOS anonymous shared-memory device (/dev/shm0).
 *
 * This is the Linux memfd replacement required by the Unix-IPC windowing
 * design.  ftruncate() sizes the segment, mmap() maps it through the same
 * device-page path as /dev/fb0, fork/SCM_RIGHTS keep sharing the same physical
 * buffer, and the last descriptor release frees the pages.
 */
#include <ntclks/heap.h>
#include <ntclks/mm.h>
#include <ntclks/object.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>

#define TASK_SHM_MAX 32u
#define TASK_SHM_PAGE 4096ULL
#define TASK_SHM_MAX_BYTES (1920ULL * 1080ULL * 4ULL)

struct task_shm {
    uint32_t used;
    uint32_t refs;
    uint32_t pages;
    uint64_t bytes;
    uint64_t physical;
};

static struct task_shm shm_segments[TASK_SHM_MAX];
static uint32_t shm_next_slot;

static uint64_t shm_page_count(uint64_t bytes)
{
    return (uint32_t)((bytes + TASK_SHM_PAGE - 1ULL) / TASK_SHM_PAGE);
}

void task_shm_retain(struct task_file *file)
{
    struct task_shm *segment;
    if (!file || !(file->flags & TASK_FILE_FLAG_DEV_SHM)) return;
    segment = (struct task_shm *)kernel_object_lookup(kernel_objects(),
                                                      (uint32_t)file->aux,
                                                      KERNEL_OBJECT_DEVICE);
    if (segment) ++segment->refs;
}

void task_shm_release(struct task_file *file)
{
    struct task_shm *segment;
    void *removed = NULL;
    if (!file || !(file->flags & TASK_FILE_FLAG_DEV_SHM)) return;
    segment = (struct task_shm *)kernel_object_lookup(kernel_objects(),
                                                      (uint32_t)file->aux,
                                                      KERNEL_OBJECT_DEVICE);
    if (!segment || !segment->refs) return;
    --segment->refs;
    if (segment->refs) return;
    if (kernel_object_remove(kernel_objects(), (uint32_t)file->aux,
                             KERNEL_OBJECT_DEVICE, &removed) == 0 && removed) {
        struct task_shm *dead = (struct task_shm *)removed;
        for (uint32_t slot = 0; slot < TASK_SHM_MAX; ++slot) {
            if (shm_segments[slot].used && shm_segments[slot].physical == dead->physical) {
                shm_segments[slot].used = 0;
                break;
            }
        }
        if (dead->physical && dead->pages) {
            mm_free_pages(dead->physical, dead->pages);
        }
        kernel_free(dead);
    }
}

int task_shm_attach(struct task_file *file)
{
    struct task_shm *segment;
    uint32_t handle;
    uint32_t slot;
    if (!file) return -LEONOS_EINVAL;
    for (slot = 0; slot < TASK_SHM_MAX; ++slot) {
        if (!shm_segments[slot].used) break;
    }
    if (slot == TASK_SHM_MAX) return -LEONOS_EMFILE;
    segment = (struct task_shm *)kernel_malloc(sizeof(*segment));
    if (!segment) return -LEONOS_ENOMEM;
    *segment = (struct task_shm){.used = 1, .refs = 1, .pages = 1, .bytes = TASK_SHM_PAGE};
    segment->physical = mm_alloc_pages(1);
    if (!segment->physical) {
        kernel_free(segment);
        return -LEONOS_ENOMEM;
    }
    handle = kernel_object_insert(kernel_objects(), segment, KERNEL_OBJECT_DEVICE);
    if (!handle) {
        mm_free_pages(segment->physical, 1);
        kernel_free(segment);
        return -LEONOS_EMFILE;
    }
    shm_segments[slot] = *segment;
    file->aux = handle;
    file->flags |= TASK_FILE_FLAG_DEV_SHM;
    file->node.size = TASK_SHM_PAGE;
    return 0;
}

int task_shm_truncate(struct task_file *file, uint64_t size)
{
    struct task_shm *segment;
    uint32_t pages;
    uint64_t physical;
    if (!file || !(file->flags & TASK_FILE_FLAG_DEV_SHM)) return -LEONOS_EBADF;
    if (!size) size = TASK_SHM_PAGE;
    if (size > TASK_SHM_MAX_BYTES) return -LEONOS_EINVAL;
    segment = (struct task_shm *)kernel_object_lookup(kernel_objects(),
                                                      (uint32_t)file->aux,
                                                      KERNEL_OBJECT_DEVICE);
    if (!segment) return -LEONOS_EBADF;
    pages = (uint32_t)shm_page_count(size);
    if (pages == segment->pages) {
        segment->bytes = size;
        file->node.size = size;
        return 0;
    }
    physical = mm_alloc_pages(pages);
    if (!physical) return -LEONOS_ENOMEM;
    if (segment->physical && segment->pages) {
        uint32_t copy_pages = segment->pages < pages ? segment->pages : pages;
        for (uint32_t i = 0; i < copy_pages * (uint32_t)(TASK_SHM_PAGE / sizeof(uint32_t)); ++i) {
            ((uint32_t *)(uintptr_t)physical)[i] =
                ((const uint32_t *)(uintptr_t)segment->physical)[i];
        }
        mm_free_pages(segment->physical, segment->pages);
    }
    segment->physical = physical;
    segment->pages = pages;
    segment->bytes = size;
    file->node.size = size;
    return 0;
}

int task_shm_map(const struct task_file *file, uint64_t offset, uint64_t length,
                 uint64_t *physical)
{
    struct task_shm *segment;
    if (!file || !physical || !(file->flags & TASK_FILE_FLAG_DEV_SHM)) {
        return -LEONOS_EBADF;
    }
    segment = (struct task_shm *)kernel_object_lookup(kernel_objects(),
                                                      (uint32_t)file->aux,
                                                      KERNEL_OBJECT_DEVICE);
    if (!segment || !segment->physical) return -LEONOS_EBADF;
    if (offset > segment->bytes || length > segment->bytes - offset) {
        return -LEONOS_EINVAL;
    }
    *physical = segment->physical + offset;
    return 0;
}
