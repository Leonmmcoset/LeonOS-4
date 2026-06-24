# Filesystem

LeonOS 4 uses numbered drives and slash-separated paths:

- `0:/`
- `1:/`

The boot disk is `0:/` and maps to a GPT ESP FAT32 partition in the v1 VMDK.

FAT32 v1 scope:

- directory listing
- reading files
- creating or overwriting small files
- appending logs

Deletion, rename, crash recovery, and full long-file-name edge cases are future
work.
