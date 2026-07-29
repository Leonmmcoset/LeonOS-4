#ifndef LEONOS_TAR_H
#define LEONOS_TAR_H

#include <stdint.h>

#define LEONOS_TAR_BLOCK_SIZE 512U
#define LEONOS_TAR_NAME_LEN 100U
#define LEONOS_TAR_MAX_FILE_SIZE (8U * 1024U * 1024U)

#define LEONOS_TAR_TYPE_FILE '0'
#define LEONOS_TAR_TYPE_DIR '5'

int leonos_tar_create(const char *tar_path);
int leonos_tar_pack_file(const char *tar_path, const char *file_path,
                         const char *stored_name);
int leonos_tar_pack_file_append(int tar_fd, const char *file_path,
                                const char *stored_name);
int leonos_tar_finalize(int tar_fd);
int leonos_tar_pack_dir(const char *tar_path, const char *dir_path);
int leonos_tar_pack_dir_append(int tar_fd, const char *dir_path);
int leonos_tar_extract_file(const char *tar_path, const char *stored_name,
                            const char *dest_path);
int leonos_tar_extract_all(const char *tar_path, const char *dest_dir);
int leonos_tar_list(const char *tar_path, char *output, uint32_t capacity);

#endif
