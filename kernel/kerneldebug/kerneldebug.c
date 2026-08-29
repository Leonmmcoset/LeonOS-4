/*
 * Staged kernel-debug module entry.
 * It proves the ET_REL loader and fixed API boundary without pulling libc or
 * arbitrary kernel symbols into the early diagnostic path.
 */
#include <ntclks/kernel_debug.h>

/**
 * @brief Keep the module contract in a real ELF note. The loader checks both the ABI and the entry-name hash before it maps any executable section.
 */
__asm__(
    ".pushsection .note.leonos.kerneldebug,\"a\",@note\n"
    ".balign 4\n"
    ".long 9\n"
    ".long 8\n"
    ".long 0x4c4b4447\n"
    ".asciz \"LEONKDBG\"\n"
    ".balign 4\n"
    ".long 1\n"
    ".long 0xac053713\n"
    ".balign 4\n"
    ".popsection\n");

static struct leonos_kernel_debug_benchmark debug_reports[192];

__attribute__((noinline, used))
int leonos_kernel_debug_module_entry(const struct leonos_kernel_debug_api *api)
{
    uint32_t selected = 0;
    static const char *items[] = {
        "[1] Run all syscall/ioctl diagnostics\n",
        "[2] Dangerous tests (disabled by default)\n",
        "[3] Reboot\n",
        "[4] Shutdown\n",
        "[5] Continue normal startup\n",
    };
    if (!api || api->version != LEONOS_KERNEL_DEBUG_MODULE_ABI ||
        !api->tui_init || !api->tui_clear || !api->tui_write || !api->tui_key) {
        return -22;
    }
    api->tui_init();
    for (;;) {
        api->tui_clear();
        api->tui_write("\033[1;36mLeonOS 4 Kernel Debugger\033[0m\n\n");
        api->tui_write("Running from kerneldebug.sys in Ring-0.\n");
        api->tui_write("Use Up/Down, Enter, or number keys 1-5.\n\n");
        for (uint32_t index = 0; index < 5U; ++index) {
            if (index == selected) api->tui_write("\033[7m");
            api->tui_write(items[index]);
            if (index == selected) api->tui_write("\033[0m");
        }
        for (;;) {
            int key = api->tui_key();
            if (!key) {
                if (api->yield) api->yield();
                continue;
            }
            if (key == 72 && selected) {
                --selected;
                break;
            }
            if (key == 80 && selected < 4U) {
                ++selected;
                break;
            }
            if (key != 28 && (key < 2 || key > 6)) continue;
            {
                uint32_t choice = key == 28 ? selected : (uint32_t)(key - 2);
                if (choice == 0U) {
                    uint32_t report_count = 0;
                    int ret = api->run_safe_benchmarks
                                  ? api->run_safe_benchmarks(debug_reports, 192U, &report_count)
                                  : -38;
                    uint32_t executed = 0;
                    uint32_t skipped = 0;
                    uint32_t failed = 0;
                    uint32_t page_lines = 0;
                    api->tui_write("\nSystem API diagnostics\n");
                    api->tui_write("Each safe entry is probed with bounded arguments;\n");
                    api->tui_write("dangerous entries are listed as skipped.\n\n");
                    if (ret < 0) {
                        api->tui_write("Diagnostic registry failed.\n");
                    } else {
                        for (uint32_t index = 0; index < report_count; ++index) {
                            struct leonos_kernel_debug_benchmark *report = &debug_reports[index];
                            if (page_lines >= 16U) {
                                api->tui_write("\nPress Enter for the next page.");
                                while (api->tui_key() != 28) {
                                    if (api->yield) api->yield();
                                }
                                api->tui_clear();
                                api->tui_write("System API diagnostics (continued)\n\n");
                                page_lines = 0;
                            }
                            api->tui_write(report->kind == LEONOS_KERNEL_DEBUG_BENCH_IOCTL
                                               ? "IOCTL  " : "SYSCALL ");
                            api->tui_write(report->name);
                            api->tui_write("  ");
                            if (report->status == LEONOS_KERNEL_DEBUG_BENCH_SKIPPED) {
                                api->tui_write("SKIPPED (side effects)\n");
                                ++skipped;
                                ++page_lines;
                                continue;
                            }
                            if (report->status == LEONOS_KERNEL_DEBUG_BENCH_FAILED) {
                                api->tui_write("FAILED\n");
                                ++failed;
                                ++page_lines;
                                continue;
                            }
                            api->tui_write(report->status == LEONOS_KERNEL_DEBUG_BENCH_OK
                                               ? "OK" : "EXPECTED ERROR");
                            api->tui_write("; min=");
                            if (api->tui_write_u64) api->tui_write_u64(report->minimum_cycles);
                            api->tui_write(" avg=");
                            if (api->tui_write_u64) api->tui_write_u64(report->average_cycles);
                            api->tui_write(" max=");
                            if (api->tui_write_u64) api->tui_write_u64(report->maximum_cycles);
                            api->tui_write(" cycles\n");
                            ++executed;
                            ++page_lines;
                        }
                        api->tui_write("\nSummary: executed=");
                        if (api->tui_write_u64) api->tui_write_u64(executed);
                        api->tui_write(" skipped=");
                        if (api->tui_write_u64) api->tui_write_u64(skipped);
                        api->tui_write(" failed=");
                        if (api->tui_write_u64) api->tui_write_u64(failed);
                        api->tui_write(" total=");
                        if (api->tui_write_u64) api->tui_write_u64(report_count);
                        api->tui_write("\n");
                    }
                    api->tui_write("\nPress Enter to return.");
                } else if (choice == 1U) {
                    api->tui_write("\nDangerous tests require explicit per-test confirmation.\nPress Enter to return.");
                } else if (choice == 2U) {
                    if (api->reboot) api->reboot();
                } else if (choice == 3U) {
                    if (api->shutdown) api->shutdown();
                } else {
                    if (api->clear_state) (void)api->clear_state();
                    return 0;
                }
                while (api->tui_key() != 28) {
                    if (api->yield) api->yield();
                }
                break;
            }
        }
    }
}
