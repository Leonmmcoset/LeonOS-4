# LeonOS-4 桌面遗留缺陷排查任务书（installer 窗口不显示 + 开始菜单卡顿）

> 交付对象：执行 Agent。工作目录 /home/xiaobai/Projects/Projects/LeonOS-4。
> 基线：分支 rewrite/unix-ipc @ d3afe34（已推送，PR Leonmmcoset/LeonOS-4#16）。
> 前置阅读：UNIXIFICATION_PROMPT.md（UNIX 化任务书，含整体架构）+ 本文件。

## 0. 硬约束

1. 禁止运行用户交互式 QEMU，但**必须**用本文件 §3 的 headless QEMU 方案自行复现与验证（串口日志 + QMP 截屏/发键），这是本轮调试的主手段。
2. 三构建全绿才算完成：`python3 build.py run kernel`、`python3 build.py run userland`、`python3 build.py run installer`；每阶段 `python3 tools/check_abi_migration.py --strict` 保持通过。
3. 不得回退既有修复（见 §4 清单）；不改动 Terminal/PTY 路径（另一工作线）。
4. 调试插桩用完即删（失败路径的一次性诊断打印可保留，但不得刷屏）；内核 rewind 插桩已移除，勿恢复。
5. 提交：中文 conventional 风格，每个缺陷一个 commit；完成后 push 到 rewrite/unix-ipc。
6. 决策点选任务书推荐项并记录理由，不停下来等确认。

## 1. 已确认可用的事实（不要重复排查，更不要"修复"它们）

上一轮已修复并验证（QEMU vmware-svga 设备实测截图确认）：
- clock_gettime 系统调用路由（syscall.c legacy 组）→ 所有用户态 deadline 有效
- fb0 mmap 页取整边界（syscall_mm.c）；wind_fb_map 过滤 MAP_FAILED
- SCM_RIGHTS fd 传递：空环 + 挂起描述符时 recvmsg 仍上报 cmsg（syscall_socket.c unix_recvmsg）
- wind_windows[] 首用前 fd=-1 初始化
- FBIOPAN_DISPLAY(0x4606) ioctl → framebuffer_present_region（VMware SVGA FIFO 刷新）
- leonos_fb_blit / leonos_gui_present_window 源 stride 为像素（×4 转字节）

当前运行状态（用户 VMware 1024x768 bpp=32 + 本地 QEMU vmware-svga 均确认）：
- 桌面进程存活、心跳 `window server alive` 每 5s、零 page fault
- windowd 正确接受 desktop(pid=4)/installer(pid=5) 连接并打印 `client pid=`
- 壁纸/taskbar/光标/字体渲染正确，鼠标键盘事件到达 desktop
- 本地 QEMU 最终截屏：壁纸 + taskbar 正常，**但没有 installer 向导窗口** → 问题 A 可本地复现

## 2. 问题 A：installer 向导窗口不显示

### 症状
串口：`[installer.elf] starting installer wizard` 后无失败无退出（create 链路成功），windowd 日志有 `client pid=5`，`create window failed` 已不再出现——但桌面上看不到向导窗口。desktop 心跳正常。

### 数据流（windowd 模型）
1. installer：`wind_app_ensure()` 连接 → HELLO → `leonos_gui_create_app_window_ex`（wind.c ~478）发送 CREATE → windowd `create_window`（apps/windowd/main.c，含失败诊断打印）→ open /dev/shm0 → ftruncate → mmap → `leonos_ipc_send_fd(CREATE_ACK, ..., shm_fd)`（SCM_RIGHTS）→ `notify_window_msg(1)`（WINDOW_NOTIFY 发给 policy_slot=desktop）。
2. installer 收 ACK+fd → mmap shm → 此后绘制进 shm 并 `leonos_gui_present_window` → PRESENT 消息 → windowd `notify_window_msg(2)`。
3. desktop（policy 端）：主循环 `desktop_run.c:114` `leonos_gui_poll_window` → `open_app_window_from_msg`（desktop/input.c:208）登记窗口 → 合成器对每个窗口 `leonos_gui_fetch_window`（wind.c ~620：FETCH → FETCH_ACK+fd → mmap → memcpy 进 screen[]）→ `flush_pixels` → `leonos_fb_blit`。

### 排查方向（按优先级）
a. **desktop 是否收到 WINDOW_NOTIFY**：wind.c `wind_pump_fd`/`wind_wait_type` 把 WINDOW_NOTIFY 排入 `wind_msgs`（容量 64，`WIND_MSG_QUEUE`）；检查 installer CREATE 时 desktop 端的 pump 是否运行（desktop 主循环每轮调用 poll_window）。可临时在 wind.c/poll_window 处加打印验证。
b. **open_app_window_from_msg（input.c:208）**：读实现，确认窗口登记条件（geometry/type/flags 过滤）、窗口列表容量。
c. **installer 是否 PRESENT**：installer 在 create 之后是否进入绘制循环并调 present（grep installer/main.c 的 present 调用与绘制循环；可能卡在 authd/inputm 查询上）。
d. **FETCH 路径**：desktop FETCH → windowd FETCH handler（要求 ROLE_POLICY）→ FETCH_ACK+shm fd → desktop mmap（大小 ack.stride*ack.height）。检查 wind.c `leonos_gui_fetch_window` 的 mmap/长度计算与 `wind_wait_type` 的 fd 接收（同 CREATE_ACK 路径，已修过一次）。
e. **合成器绘制条件**：desktop 窗口绘制可能被 z-order/visible/dirty 标志拦住（desktop 的窗口结构、redraw_all 的窗口循环）。

### 验收
本地 QEMU 截屏出现 installer 向导窗口（内容可辨），串口无 page fault/异常退出。

## 3. 问题 B：开始菜单卡顿后无动画出现

### 症状
点开始按钮（或按键）后桌面卡住数秒，菜单才出现，且无出现动画。

### 排查方向
a. **连接重试循环是头号嫌疑**：所有服务客户端都有 5s 连接重试（grep `RETRY_MS`）：inputm.c:22、sessiond_client.c:17、devmand_client.c:17、authd_client.c:19、net_service.c。**installer 模式下 serviced/netmand 等守护不运行**——若开始菜单打开路径触发了对不存在服务的查询，`leonos_ipc_connect` 立即失败但重试循环烧满 5s → 正是"卡住数秒"的量级。grep 开始菜单路径（desktop/input.c、desktop_items.c、start_menu 相关）调用了哪些客户端。
b. **动画缺失**：菜单出现动画由 desktop 主循环的动画步进驱动（desktop_update_window_animations / 菜单展开状态机）。打开路径若同步阻塞数秒，动画时间窗已过 → 直接画终态。修好 a 后动画应恢复；若仍无动画，检查动画状态机是否被跳过（menu_open 标志与 dirty 逻辑）。
c. **/proc 未挂载**：installer 模式 mount policy 无 /proc，`leonos_task_snapshot`（procsys.c）open("/proc") 立即失败——确认它不在开始菜单路径上且失败是快路径。
d. app 列表扫描（app_registry / desktop_items）是文件枚举，若菜单打开触发了对大量 .app.ini 的 stat/read，量级应为毫秒；用串口时间戳量化确认真正卡点（在打开路径前后加 ticks 打印最快）。

### 验收
本地 QEMU：通过 QMP `send-key`（qcode `meta_l` 或点击 Start 坐标）打开菜单，从串口时间戳/截屏对比确认打开延迟 < 300ms 且无阻塞调用；动画按设计播放（逐帧截屏 2-3 张对比）。

## 4. 本地复现环境（已验证可用，直接照抄）

```bash
# 构建
python3 build.py run kernel && python3 build.py run userland && python3 build.py run installer

# 启动（注意：-device vmware-svga 不接受 xres/yres 属性；不要用 pkill -f 杀进程，
# 会匹配到自己的 shell 命令行文本自杀，用 pkill qemu-system-x86）
rm -f /tmp/leonos-dbg-serial.log /tmp/leonos-qmp.sock
setsid nohup qemu-system-x86_64 -cpu max -machine q35 -m 1024M -smp 1 \
  -bios /usr/share/ovmf/x64/OVMF.4m.fd -display none \
  -serial file:/tmp/leonos-dbg-serial.log \
  -device vmware-svga \
  -netdev user,id=net0 -device e1000,netdev=net0 \
  -audiodev none,id=snd0 -device AC97,audiodev=snd0 \
  -cdrom build/images/leonos4-installer.iso \
  -qmp unix:/tmp/leonos-qmp.sock,server,nowait -no-reboot -no-shutdown \
  -gdb tcp::1234 > /tmp/qemu-stdout.log 2>&1 < /dev/null & disown

# QMP：截屏 / 发键（python socket 直连 /tmp/leonos-qmp.sock）
#   screendump: {"execute":"screendump","arguments":{"filename":"/tmp/s.ppm"}}
#   send-key:   {"execute":"send-key","arguments":{"keys":[{"type":"qcode","data":"meta_l"}]}}
#   启动 guest：{"execute":"cont"}（若以 -S 启动）
```

内核态调试（已验证的技巧）：
- GDB：`target remote :1234` + `file build/system/kernel.unstripped`。符号地址与运行内核一致；但 nm 只有 symtab 无 DWARF——**不要用探针头文件的 offsetof**（会解析错 struct），用手工偏移或 clang `-fdump-record-layouts`。
- 内核虚地址 = 0xffffff8000000000 + 物理地址；当 CPU 停在用户态时低映射不可读，用 QMP `human-monitor-command` 的 `xp /1xg <物理地址>` 读物理内存。
- struct task 关键偏移（以当前构建 nm/layout 实测为准，每次改头文件后会变）：state=0x68、flags=0x70、cpu_ticks=0x78、affinity=0x88、wake_tick=0x50、poll_deadline=0x58、name_storage=0x10、name ptr=0x30、frame=44576、running_cpu=46288；`tasks` 是 `struct task **`（二级指针，BSS 地址从 nm 取）。
- 断点只对 int 0x80 路径可靠；区分"没被调用"与"断点失效"时，用 QMP 截屏 + 串口时间戳交叉验证。
- 构建号在 buildsystem/state/build_number.txt，串口首行 `LeonOS 4 ntclks 4.6.2-XXXX booting` 可确认 ISO 新旧。

## 5. 交付

1. 两个缺陷的修复 commit（可含临时诊断，最终版不得刷屏），push 到 rewrite/unix-ipc。
2. 串口日志 + 截屏证据：installer 向导可见；开始菜单打开延迟量化（<300ms）且动画正常。
3. 阶段报告：根因、改动文件、验证结果、用户手测清单。

## 6. 已知坑（前人踩过，勿再踩）
- `pkill -f qemu-system` 会自杀（匹配自己 shell 文本）→ 用 `pkill qemu-system-x86`。
- 后台长命令会被工具会话回收：QEMU 用 `setsid nohup ... & disown`，构建放前台。
- poll 的 timeout 由回卷机制实现（syscall_poll_park，每 tick 重扫）——不要改成忙等。
- task_socket_write 是全或无原子写、send_fd 是单次原子 sendmsg——勿改回分片。
- unix socket 非阻塞 EAGAIN 白名单（syscall_eagain_is_nonblocking_device）已含 socket 调用族——READ/WRITE/POLL 的 EAGAIN 回卷重试是设计行为。
