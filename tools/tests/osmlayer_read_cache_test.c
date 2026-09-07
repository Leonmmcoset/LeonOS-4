#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../kernel/ntclks/osmlayer_bridge.c"

static uint64_t generation = 1;
static unsigned reads;
static int read_error;
static const char *contents = "initial ACL";

uint64_t storage_metadata_generation(void) { return generation; }
int storage_read_file(const char *path, const void **data, size_t *length)
{
    assert(strstr(path, "LEONACL.SYS"));
    ++reads;
    if (read_error) return read_error;
    *length = strlen(contents);
    void *copy = malloc(4096);
    assert(copy);
    memcpy(copy, contents, *length);
    *data = copy;
    return 0;
}
void mm_free_pages(uint64_t phys, uint32_t pages)
{ assert(pages == 1); free((void *)(uintptr_t)phys); }

int main(void)
{
    char buffer[128];
    uint32_t length;
    const char *path = "/system/LEONACL.SYS";
    for (unsigned i = 0; i < 40; ++i) {
        assert(osmlayer_read_file_service(path, buffer, sizeof(buffer), &length) == 0);
        assert(length == strlen(contents) && !memcmp(buffer, contents, length));
    }
    assert(reads == 1);
    assert(osmlayer_read_file_service(path, buffer, 1, &length) == -7);
    assert(length == strlen(contents) && reads == 1);

    /* A write, deletion or mount change invalidates positive and negative entries. */
    ++generation;
    contents = "updated ACL";
    assert(osmlayer_read_file_service(path, buffer, sizeof(buffer), &length) == 0);
    assert(reads == 2 && !memcmp(buffer, contents, length));
    ++generation;
    read_error = -2;
    for (unsigned i = 0; i < 40; ++i)
        assert(osmlayer_read_file_service(path, buffer, sizeof(buffer), &length) == -2);
    assert(reads == 3);
    ++generation;
    read_error = -11;
    assert(osmlayer_read_file_service(path, buffer, sizeof(buffer), &length) == -11);
    read_error = 0;
    assert(osmlayer_read_file_service(path, buffer, sizeof(buffer), &length) == 0);
    assert(reads == 5);
    assert(osmlayer_read_file_service("/users/LEONACL.SYS", buffer,
                                      sizeof(buffer), &length) == 0);
    assert(reads == 6);
    puts("Metadata cache: 40 repeated reads use one I/O; mutations and transient errors stay visible");
    return 0;
}
