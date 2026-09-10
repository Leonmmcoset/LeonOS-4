#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "../../userland/libc/src/blockdev.c"
/* Back the real sector reader with a regular temporary disk image. */
int ioctl(int fd, unsigned long command, ...)
{
    struct stat st; assert(fstat(fd,&st)==0);
    va_list args; va_start(args,command);
    if (command==BLKGETSIZE64) *va_arg(args,uint64_t *)=st.st_size;
    else if (command==BLKSSZGET) *va_arg(args,int *)=512;
    else { va_end(args); errno=ENOTTY; return -1; }
    va_end(args); return 0;
}
int main(int argc,char **argv)
{
    assert(argc==4);
    char uuid[37];
    assert(leonos_block_partition_uuid(argv[1],0,uuid)==0 && !strcmp(uuid,argv[2]));
    assert(leonos_block_partition_uuid(argv[1],1,uuid)==0 && !strcmp(uuid,argv[3]));
    assert(leonos_block_partition_uuid(argv[1],2,uuid)==-ENOENT);
    assert(leonos_block_partition_uuid(argv[1],UINT32_MAX,uuid)==-ENOENT);
    assert(leonos_block_partition_uuid(argv[1],0,NULL)==-EINVAL);
    puts("PASS installer UUID reader against independently generated primary/backup GPT");
}
