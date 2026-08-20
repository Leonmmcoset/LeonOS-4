/*
 * LeonOS kernel object table implementation.
 * Handles are index plus generation values, preventing stale descriptor reuse.
 */
#include <ntclks/object.h>

static struct kernel_object_table global_objects;

static uint32_t make_handle(uint32_t index, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (index + 1u);
}

static uint32_t handle_index(uint32_t handle)
{
    uint32_t value = handle & 0xffffu;
    return value ? value - 1u : KERNEL_OBJECT_MAX;
}

void kernel_object_table_init(struct kernel_object_table *table)
{
    if (!table) {
        return;
    }
    kernel_spin_init(&table->lock);
    for (uint32_t i = 0; i < KERNEL_OBJECT_MAX; ++i) {
        table->objects[i] = NULL;
        table->generations[i] = 1;
        table->types[i] = KERNEL_OBJECT_NONE;
    }
}

void kernel_objects_init(void)
{
    kernel_object_table_init(&global_objects);
}

struct kernel_object_table *kernel_objects(void)
{
    return &global_objects;
}

uint32_t kernel_object_insert(struct kernel_object_table *table, void *object,
                              enum kernel_object_type type)
{
    uint64_t flags;
    uint32_t handle = KERNEL_HANDLE_INVALID;
    if (!table || !object || type == KERNEL_OBJECT_NONE) {
        return KERNEL_HANDLE_INVALID;
    }
    kernel_spin_lock_irqsave(&table->lock, &flags);
    for (uint32_t i = 0; i < KERNEL_OBJECT_MAX; ++i) {
        if (!table->objects[i]) {
            uint16_t generation = ++table->generations[i];
            if (!generation) {
                generation = ++table->generations[i];
            }
            table->objects[i] = object;
            table->types[i] = (uint16_t)type;
            handle = make_handle(i, generation);
            break;
        }
    }
    kernel_spin_unlock_irqrestore(&table->lock, flags);
    return handle;
}

void *kernel_object_lookup(struct kernel_object_table *table, uint32_t handle,
                           enum kernel_object_type type)
{
    uint64_t flags;
    uint32_t index = handle_index(handle);
    void *object = NULL;
    if (!table || index >= KERNEL_OBJECT_MAX || !handle) {
        return NULL;
    }
    kernel_spin_lock_irqsave(&table->lock, &flags);
    if (table->objects[index] && table->generations[index] == (uint16_t)(handle >> 16) &&
        (type == KERNEL_OBJECT_NONE || table->types[index] == (uint16_t)type)) {
        object = table->objects[index];
    }
    kernel_spin_unlock_irqrestore(&table->lock, flags);
    return object;
}

int kernel_object_remove(struct kernel_object_table *table, uint32_t handle,
                         enum kernel_object_type type, void **object)
{
    uint64_t flags;
    uint32_t index = handle_index(handle);
    if (object) {
        *object = NULL;
    }
    if (!table || index >= KERNEL_OBJECT_MAX || !handle) {
        return -1;
    }
    kernel_spin_lock_irqsave(&table->lock, &flags);
    if (!table->objects[index] || table->generations[index] != (uint16_t)(handle >> 16) ||
        (type != KERNEL_OBJECT_NONE && table->types[index] != (uint16_t)type)) {
        kernel_spin_unlock_irqrestore(&table->lock, flags);
        return -1;
    }
    if (object) {
        *object = table->objects[index];
    }
    table->objects[index] = NULL;
    table->types[index] = KERNEL_OBJECT_NONE;
    kernel_spin_unlock_irqrestore(&table->lock, flags);
    return 0;
}
