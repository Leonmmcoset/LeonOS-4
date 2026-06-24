#ifndef NTCLKS_USERLAND_H
#define NTCLKS_USERLAND_H

#include <leonos/fs.h>
#include <ntclks/multiboot2.h>
#include <ntclks/trap.h>
#include <ntclks/types.h>

struct task;

void userland_init(const struct boot_info *boot);
void userland_enter_first(void) __attribute__((noreturn));
void userland_process_exit(uint64_t code);
int64_t userland_spawn_path(const char *path);
int64_t userland_spawn_path_with_pty(const char *path, uint32_t pty_id);
void userland_yield_if_runnable(void);
struct task *userland_schedule_from_frame(struct trap_frame *frame);
int userland_list_dir(const char *path, struct leonos_dir_entry *entries,
                      uint32_t capacity, uint32_t *out_count);

#endif
