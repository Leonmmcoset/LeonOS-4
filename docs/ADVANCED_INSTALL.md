# LeonOS 4 高级安装教程

高级模式提供一个直接进入 BusyBox 的 TTY shell，允许手动完成分区、格式化、挂载、复制系统文件和安装启动文件。它不会启动图形安装器，也不会自动分区。

## 磁盘布局

新安装推荐使用以下布局：

| 分区 | 文件系统 | GPT 类型 | GPT 名称 | 用途 |
| --- | --- | --- | --- | --- |
| 1 | FAT32 | EFI System Partition | `LeonOS 4 ESP` | UEFI、GRUB 和内核文件 |
| 2 | exFAT | Microsoft Basic Data | `LEONOS4_ROOT` | LeonOS 4 根文件系统 |

ESP 至少需要 128 MiB。根分区必须能容纳 `/install/root` 的全部内容，并留出用户数据空间。

## 重要警告

- 删除分区和执行 `mkfs.*` 会破坏目标分区中的数据。
- 反复确认 `lsblk` 显示的磁盘编号，不要把 ISO 所在设备当作目标磁盘。
- LeonOS 使用 `/dev/disk0`、`/dev/disk0p1` 这样的设备路径，不使用 `/dev/sda`。
- `fdisk` 用于编辑 GPT；完全空白磁盘应先使用本 ISO 专用的 `gptinit` 初始化 GPT。`gptinit` 只存在于安装 ISO，不会安装到目标系统。
- 安装 ISO 的根文件系统运行在内存中的临时 ramdisk，重启后修改会丢失；写入目标磁盘的内容会保留。

## 1. 启动高级模式

从安装 ISO 的 GRUB 菜单选择：

```text
Install LeonOS 4 (Advanced mode, TTY shell)
```

进入 shell 后，确认安装 payload 已加载：

```sh
ls /install/root
ls /install/esp
```

高级 shell 通常以管理员身份运行。存储管理操作需要管理员权限。

## 2. 查看磁盘

```sh
lsblk
blkid
fdisk -l /dev/disk0
```

根据 `lsblk` 的容量、控制器和分区信息确定目标磁盘。下面示例使用 `/dev/disk0`，如果实际编号不同，请整体替换。

## 3. 初始化、创建或整理分区

如果 `fdisk -l` 显示目标磁盘没有有效 GPT，先执行：

```sh
gptinit /dev/disk0
```

在提示中输入：

```text
YES
```

`gptinit` 会清除磁盘开头的旧分区元数据、保护性 MBR 和末尾的旧备份 GPT，并写入一个空的主/备 GPT。它不会创建分区，也不会格式化文件系统。若确认磁盘中已有有效 GPT 仍要覆盖，使用：

```sh
gptinit --force /dev/disk0
```

`--force` 不再询问确认，使用前必须再次核对磁盘编号。

初始化完成或目标磁盘已经有有效 GPT 后，使用 LeonOS 的 `fdisk`：

```sh
fdisk /dev/disk0
```

交互操作如下：

```text
p                       查看当前分区
d                       删除分区，按提示输入分区号（需要时重复）
n                       创建分区
128                     ESP 大小，单位 MiB
LeonOS 4 ESP            ESP 名称

n                       创建根分区
<根分区大小>             例如 800，单位 MiB
LEONOS4_ROOT            根分区名称

w                       保存 GPT 并退出
```

创建分区时不需要指定文件系统；后面的 `mkfs.*` 会格式化分区。分区编号应为 1 和 2。如果磁盘容量较大，根分区可以使用剩余空间。

## 4. 格式化分区

标准 exFAT 根分区方案：

```sh
mkfs.fat32 /dev/disk0p1
mkfs.exfat /dev/disk0p2
```

`mkfs.fat32` 会把 GPT 类型设置为 Basic Data，因此格式化后必须重新设置 ESP 类型。

## 5. 设置 GPT 类型和名称

```sh
fdisk /dev/disk0
```

输入：

```text
t
1
esp

t
2
basic

r
1
LeonOS 4 ESP

r
2
LEONOS4_ROOT

w
```

建议将根分区名称设置为 `LEONOS4_ROOT`（区分大小写）。启动检测要求
ESP 使用 ESP GPT 类型；根分区使用 Basic Data GPT 类型并包含有效的
exFAT 文件系统。即使名称被其他分区工具改写，内核也会通过 exFAT
签名识别该根分区。

## 6. 检查文件系统

```sh
blkid
lsblk

fsck.fat32 /dev/disk0p1
fsck.exfat /dev/disk0p2
```

当前 `fsck.*` 工具执行只读超级块检查，不负责修复损坏的数据。支持的别名包括 `fsck.fat`、`fsck.vfat` 和通用的 `fsck`。

## 7. 挂载目标分区

手动挂载使用绝对路径。高级模式下不要占用 `/boot`、`/target` 或 `/dev`，这些路径由系统或安装器保留：

```sh
mkdir -p /mnt
mkdir /mnt/esp
mkdir /mnt/root

mount -t fat32 /dev/disk0p1 /mnt/esp
mount -t exfat /dev/disk0p2 /mnt/root
mount
```

省略 `-t` 也可以让内核检测文件系统：

```sh
mount /dev/disk0p1 /mnt/esp
mount /dev/disk0p2 /mnt/root
```

## 8. 复制 LeonOS 根文件

必须复制目录内容而不是把 `root` 目录再套一层；使用 `/.` 也能包含隐藏文件：

```sh
cp -r /install/root/. /mnt/root/
```

确认核心桌面程序已经复制到目标根分区：

```sh
ls /mnt/root/system/apps/desktop/desktop.elf
```

确保状态目录存在。若目录已经存在，提示已存在可以忽略：

```sh
mkdir /mnt/root/system/state
```

## 9. 安装 GRUB 和启动文件

确认 ESP 已挂载到 `/mnt/esp` 后执行：

```sh
leonos-grub-installer /mnt/esp
```

该命令从 `/install/esp` 复制以下内容：

- `EFI/BOOT/BOOTX64.EFI`
- `loader.elf`
- `system/kernel.sys`
- `system/middlelayer.sys`
- `grub/` 目录及其配置、字体和主题

工具安装的是 UEFI fallback 路径。如果固件没有自动建立启动项，请在固件启动菜单中手动选择目标磁盘的 `EFI/BOOT/BOOTX64.EFI`。

## 10. 同步、卸载和重启

卸载前不要让 shell 的当前目录位于挂载点中，也不要有程序正在打开目标文件：

```sh
sync
cd /
umount /mnt/root
umount /mnt/esp
reboot
```

重启前移除安装 ISO，或在固件启动菜单中选择目标磁盘。

## ext2 根分区兼容方案

LeonOS 仍支持经典 ext2 根分区。ESP 的步骤不变，只替换根分区的格式化、检查、类型和挂载命令：

```sh
mkfs.ext2 /dev/disk0p2

fdisk /dev/disk0
```

在 `fdisk` 中设置：

```text
t
2
linux

r
2
LEONOS4_ROOT

w
```

检查和挂载：

```sh
fsck.ext2 /dev/disk0p2
mount -t ext2 /dev/disk0p2 /mnt/root
```

之后仍然执行根文件复制、`leonos-grub-installer`、`sync`、卸载和重启步骤。新安装优先推荐 exFAT；ext2 主要用于兼容现有系统或特定需求。

## 可用工具

高级 shell 内置或可直接调用：

```text
fdisk
gptinit  (仅安装 ISO)
mkfs.fat  mkfs.fat32  mkfs.ext2  mkfs.exfat
fsck  fsck.fat  fsck.fat32  fsck.vfat  fsck.ext2  fsck.exfat
blkid  lsblk  mount  umount  sync
leonos-grub-installer
```

这些工具使用 LeonOS 的存储接口和 `/dev/disk*` 设备节点，不依赖 Linux block-device ioctl。

## 常见失败原因

- `fdisk` 无法读取 GPT：对空白磁盘先执行 `gptinit /dev/diskN`；如果初始化本身失败，检查磁盘容量、连接状态和管理员权限。
- `mkfs.*` 失败：确认目标是分区路径（例如 `/dev/disk0p2`），而不是整盘，并确认分区没有挂载。
- `mount` 失败：确认挂载点已创建、使用绝对路径且文件系统类型匹配。
- `umount` 失败：先执行 `cd /`，关闭正在访问该挂载点的程序。
- 系统找不到根分区：检查 ESP GPT 类型、ESP 是否 FAT32、根分区是否为
  Basic Data GPT 类型以及 exFAT 超级引导区是否有效；`LEONOS4_ROOT` 是推荐名称。
