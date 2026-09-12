/* Each operation invokes upstream sudo and re-evaluates its command policy. */
#include "fileman.h"
#include <leonos/sudo.h>

char fileman_elevated_path[LEONOS_FS_PATH_LEN];

void fileman_forget_elevation(void) { fileman_elevated_path[0] = 0; }
int fileman_elevation_applies(const char *path)
{
    return path && path[0] && text_eq(path, fileman_elevated_path);
}
int fileman_prompt_elevation(const char *path)
{
    uint32_t count;
    if (leonos_fileop(LEONOS_FILEOP_LIST, path, NULL, NULL, NULL, entries,
                       FILEMAN_MAX_ENTRIES, &count) < 0) {
        set_status(T("Operation denied or canceled", "操作被拒绝或已取消"));
        return -1;
    }
    copy_text(fileman_elevated_path, sizeof(fileman_elevated_path), path);
    return 0;
}
int fileman_list_elevated(const char *path, struct leonos_dir_entry *out,
                          uint32_t capacity, uint32_t *count)
{
    return leonos_fileop(LEONOS_FILEOP_LIST, path, NULL, NULL, NULL, out, capacity, count);
}
int fileman_mkdir_elevated(const char *parent, const char *name,
                           struct leonos_dir_entry *out, uint32_t capacity, uint32_t *count)
{
    return leonos_fileop(LEONOS_FILEOP_MKDIR, parent, name, NULL, NULL, out, capacity, count);
}
int fileman_rename_elevated(const char *from, const char *to,
                            struct leonos_dir_entry *out, uint32_t capacity, uint32_t *count)
{
    return leonos_fileop(LEONOS_FILEOP_RENAME, from, to, NULL, NULL, out, capacity, count);
}
int fileman_delete_elevated(const char *path, uint8_t is_dir,
                            struct leonos_dir_entry *out, uint32_t capacity, uint32_t *count)
{
    return leonos_fileop(LEONOS_FILEOP_UNLINK, path, is_dir ? LEONOS_FILEOP_CONFIRM : NULL,
                         NULL, NULL, out, capacity, count);
}
