#include <leonos/fs.h>
#include <stdio.h>
#include <string.h>

#include <stdlib.h>

static int parse_disk(const char *path, uint32_t *disk_id)
{
    const char *digits;
    char *end;
    unsigned long value;
    if (!path || !disk_id || strncmp(path, "/dev/disk", 9) != 0) {
        return -1;
    }
    digits = path + 9;
    if (!digits[0] || strchr(digits, 'p')) {
        return -1;
    }
    value = strtoul(digits, &end, 10);
    if (!end || *end || value >= LEONOS_INSTALL_MAX_DISKS) {
        return -1;
    }
    *disk_id = (uint32_t)value;
    return 0;
}

static int confirm_initialization(const char *path)
{
    char line[16];
    printf("This will replace GPT metadata on %s. Type YES to continue: ", path);
    if (!fgets(line, sizeof(line), stdin)) {
        return 0;
    }
    line[strcspn(line, "\r\n")] = 0;
    return strcmp(line, "YES") == 0;
}

int main(int argc, char **argv)
{
    const char *path;
    uint32_t disk_id;
    uint32_t flags = 0;
    struct leonos_disk_gpt_initialize request;
    int ret;

    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--force") == 0) {
        path = argv[2];
        flags = LEONOS_DISK_GPT_INITIALIZE_FORCE;
    } else {
        puts("usage: gptinit [--force] /dev/diskN");
        return 2;
    }
    if (parse_disk(path, &disk_id) < 0) {
        puts("gptinit: expected a whole disk such as /dev/disk0");
        return 2;
    }
    if (!(flags & LEONOS_DISK_GPT_INITIALIZE_FORCE) && !confirm_initialization(path)) {
        puts("gptinit: cancelled");
        return 1;
    }
    request.disk_id = disk_id;
    request.flags = flags;
    ret = leonos_disk_initialize_gpt(&request);
    if (ret < 0) {
        printf("gptinit: GPT initialization failed for %s (ret=%d)\n", path, ret);
        if (ret == -17) {
            puts("gptinit: disk already has a valid GPT; use --force after checking the device");
        }
        return 1;
    }
    printf("gptinit: initialized an empty GPT on %s\n", path);
    puts("Create partitions next with fdisk.");
    return 0;
}
