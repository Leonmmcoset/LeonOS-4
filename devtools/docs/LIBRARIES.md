# 公共库参考

LeonOS ABI v1 的基础运行库是 `lib/libleonos.so.1`。SDK 默认构建动态 PIE；
`STATIC=1` 时使用 `lib/leonos.a` 和下列静态归档。头文件是唯一稳定入口；未列出的
内部符号不属于应用 ABI。

## 文件、配置和文本

| 头文件 | 主要能力 | 重要限制 |
| --- | --- | --- |
| `leonos/fs.h` | 目录枚举、`stat/fstat`、ACL、安装器磁盘操作 | 路径最长 256 字节；目录使用 `leonos_readdir` |
| `leonos/ini.h` | 宽松/严格 INI 读取、段和键枚举 | 文件最大 64 KiB；最多 32 段、每段 64 键 |
| `leonos/tar.h` | 创建、追加、列出和解包 tar | 单文件最大 32 MiB；解包应使用目标目录和进度回调 |
| `leonos/text.h` | UTF-8 字形布局和宽度计算 | 输出数组由调用者提供，容量不足必须处理 |
| `leonos/png.h` | 有界 PNG 文件解码 | 文件最多 16 MiB、像素最多 1024x1024；输出需 `leonos_png_free` |
| `leonos/i18n.h` | 当前语言和双语文本选择 | 当前语言为英文或简体中文 |

## 网络和安全传输

`leonos/net.h` 提供静态/DHCP 配置、IPv4 ping、DNS、HTTP GET 以及最多
`LEONOS_NET_SOCKET_MAX` 个 TCP socket。超时应限制在 10 秒以内；HTTP 便捷
接口响应体最多 4096 字节。

`leonos/http.h` 提供带请求头、请求体、重定向和下载进度的 HTTP API。默认超时
为 10 秒、最多跟随 5 次重定向，响应头和响应体容量由调用者提供。需要 HTTPS
时使用系统 TLS 支持，验证网络状态码和证书错误，不要把下载内容直接当作可执行
文件运行。

`leonos/auth.h` 提供当前用户、登录、创建用户、角色更新和改密；
`leonos/admin.h` 的 `leonos_admin_elevate()` 会显示系统授权流程。密码必须通过
安全输入框收集，不能写入日志或普通配置文件。

## GUI、终端和启动

- `leonos/gui.h`、`leonos/ui.h`、`leonos/mouse.h`：参见 [GUI.md](GUI.md)
  和 [UI.md](UI.md)。
- `leonos/pty.h`：创建 PTY、读输出、写输入、启动子进程、读写 termios 和窗口
  大小。终端程序应处理短读短写，并在退出前关闭 PTY。
- `leonos/launch.h`：命令行拆分、文件关联、快捷方式和统一启动；
  `leonos_spawn_argv()` 启动子进程，`leonos_launch_argv()` 额外处理 Terminal 与
  文件关联，返回值可用 `leonos_launch_error_text()` 转换为用户可读文本。
- `leonos/startup.h`：按当前 UID 申请、查询、批准/拒绝和管理登录自启动项。
- `leonos/system.h`：系统版本、性能计数、时间、NTP 同步、机器身份以及重启/关机。

## 音频和安装包

`leonos/audio.h` 只提供线性 PCM 播放配置、写入和设备状态查询；单次写入最多
64 KiB，格式不匹配时返回 `LEONOS_AUDIO_STATUS_BAD_FORMAT`。

`leonos/api.h` 解析和安装 `.api` tar 包，支持进度回调、管理员要求、桌面入口、
虚拟终端标记和输入法元数据。安装路径必须由系统安装器选择，应用不要把包内容
直接解包到 `0:/tools` 以冒充已安装程序。

## 第三方库

- `lib/libc.a`：Picolibc 运行时，和 SDK 头文件、链接脚本成套使用。
- `lib/libz.a`、`lib/libpng.a`：压缩和 PNG；应用仍需设置输入大小上限。
- `lib/libstardustui.a`：启用 `USE_STARDUSTUI=1` 时链接，且只能使用 SDK
  中随附的上游公共头文件。
- `lib/libmagic.so.1` 与 `lib/libmagic.a`：file 5.48 的文件类型识别库；公共头
  文件为 `include/magic.h`，运行时数据库为 `0:/system/share/misc/magic.mgc`。
- `lib/liblua.so.5` 与 `lib/liblua.a`：Lua 5.4.8 C API；公共头文件为
  `include/lua5.4/`。动态 C 模块加载仍未开放。
- `lib/sqlite.so.3` 与 `lib/sqlite.a`：SQLite 3.46.1 C API；公共头文件为
  `include/sqlite3.h`。LeonOS 使用自定义 VFS，当前关闭 WAL、扩展加载和跨进程锁。

`libmagic.so.1` 和 `liblua.so.5` 与 `libleonos.so.1` 都要求 LeonOS ABI v1，
运行时从 `0:/system/lib` 解析。它们不提供宿主机 ABI 兼容层。

在 SDK 默认动态构建中，使用 `USE_LIBMAGIC=1` 或 `USE_LUA=1` 会自动写入
相应 `DT_NEEDED` 项。例如：

```sh
make APP=examples/typecheck APP_NAME=typecheck USE_LIBMAGIC=1
make APP=examples/lua_embed APP_NAME=lua_embed USE_LUA=1
make APP=examples/sqlite_demo APP_NAME=sqlite_demo USE_SQLITE=1
```

与基础运行库相同，这些共享库由目标 LeonOS 系统提供；不要把宿主机的 `.so`
放进应用目录。`STATIC=1` 时相同开关改用 `.a` 归档。
