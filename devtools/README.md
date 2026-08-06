# LeonOS 4 Developer SDK

这个目录是 LeonOS 4 用户态应用开发套件。它可以单独复制到没有
LeonOS 4 源码的开发环境中，用来构建可以由 LeonOS 4 启动的 x86_64
ELF 应用程序。

## 内容

- `include/`: LeonOS 4 用户态 C 标准库兼容头文件和公开系统 API。
- `lib/libc.a`: 与本 SDK 头文件匹配的 freestanding 用户态 C 库。
- `lib/libz.a` 与 `lib/libpng.a`: zlib 1.3.2 和 libpng 1.6.58 静态库；对应
  的公开头文件与许可证也包含在 SDK 中。
- `linker.ld`: LeonOS 4 用户态 ELF 的链接布局，入口为 `_start`。
- `examples/helloworld/`: 最小可构建的 HelloWorld 应用。
- `examples/inputm_provider/`: 注册 InputM 提供者并提交/透传键盘事件的示例。
- `Makefile`: 独立构建入口，不会引用 LeonOS 4 源码目录。
- `README-tcc.md`: 镜像内置设备端 TinyCC 的静态编译工作流与限制。

## 前置条件

需要一个可以生成 freestanding x86_64 ELF 的交叉工具链。默认工具名为：

```text
x86_64-elf-gcc
x86_64-elf-ld
```

Makefile 也可使用其他前缀。例如工具名是 `x86_64-unknown-elf-gcc` 时：

```sh
make CROSS=x86_64-unknown-elf-
```

在 Windows 上建议从 WSL、MSYS2 或其他提供 GNU Make 与交叉工具链的
环境执行构建。

## 构建示例

在本目录执行：

```sh
make
```

输出文件为：

```text
build/helloworld.elf
```

清理构建结果：

```sh
make clean
```

## 创建应用

1. 复制 `examples/helloworld` 为你的应用目录，例如 `examples/myapp`。
2. 修改其中的 `main.c`。程序入口是普通的 `int main(void)`；SDK 的
   `libc.a` 会提供 `_start`、系统调用封装和常用 C 库函数。
3. 构建：

```sh
make APP=examples/myapp APP_NAME=myapp
```

结果是 `build/myapp.elf`。将其复制到 LeonOS 分区中的可执行位置，例如：

```text
0:/programs/myapp/myapp.elf
```

可由桌面、文件管理器或 `execve()` 启动。

### 虚拟终端应用

不创建 GUI 窗口、而是需要标准输入输出渲染的应用，可在 ELF 同目录放置同名
sidecar manifest。例如 `0:/programs/myapp/myapp.elf` 的标记文件是
`0:/programs/myapp/myapp.app.ini`：

```ini
[app]
terminal=1
```

通过桌面、文件管理器、运行框、快捷方式或 `leonos_launch_argv()` 启动时，系统会
创建 Terminal 窗口并将该程序的标准输入、标准输出和标准错误绑定到同一个 PTY。
直接调用 `execve()` 不会读取该标记。使用 `tools/build_api.py` 打包应用时，传入
`--virtual-terminal` 会自动生成并安装这个 manifest。

## API 使用

使用 SDK 携带的头文件，而不是宿主操作系统的 API：

```c
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/gui.h>
#include <leonos/mouse.h>
#include <leonos/ui.h>
```

`leonos/stdio.h` 提供 `puts()` 和 `printf()`。`leonos/syscall.h` 提供
文件、进程、内存和调度相关接口。`leonos/gui.h` 与 `leonos/ui.h` 提供
窗口、事件与软件绘制接口。文件路径使用 LeonOS 驱动器格式，例如
`0:/programs/myapp/data.txt`。

### 窗口与鼠标控制

`leonos/gui.h` 还提供窗口运行时控制接口：

```c
leonos_gui_set_window_title(window_id, "Download complete");
leonos_gui_set_window_borderless(window_id, 1);
leonos_gui_set_window_taskbar_visible(window_id, 0);
leonos_gui_set_taskbar_visible(window_id, 0);
```

可在创建窗口时使用 `LEONOS_GUI_WINDOW_BORDERLESS` 或
`LEONOS_GUI_WINDOW_HIDE_TASKBAR` 标志。前者移除窗口边框，后者仅隐藏该
窗口在桌面底部任务栏中的按钮；`leonos_gui_set_taskbar_visible()` 则隐藏或
显示整个桌面任务栏。

`leonos/mouse.h` 提供鼠标可见性、位置与样式控制：

```c
leonos_mouse_set_position(window_id, 320, 240);
leonos_mouse_set_style(window_id, LEONOS_GUI_CURSOR_HAND);
```

位置使用桌面的逻辑像素坐标。所有这类接口都只允许应用控制自己创建的
`window_id`；窗口销毁后，任务栏和光标样式会自动恢复。

程序必须是 freestanding：不要依赖宿主系统的动态链接器、POSIX 运行时或
宿主系统的库。请只链接本 SDK 的 `lib/libc.a`，并保留 Makefile 的编译、
链接参数与 `linker.ld`。

### PNG 图像

SDK 带有上游的 `<zlib.h>`、`<png.h>` 和完整静态库。默认 Makefile 已把
`libpng.a` 与 `libz.a` 放在链接组中，因此应用可以直接调用 libpng，或使用
LeonOS 的受限文件解码接口：

```c
#include <leonos/png.h>

uint32_t *pixels;
uint32_t width;
uint32_t height;
if (leonos_png_decode_file("0:/programs/demo/image.png", &pixels, &width, &height) == 0) {
    /* pixels are 0x00RRGGBB and alpha has been composited on white. */
    leonos_png_free(pixels);
}
```

`leonos_png_decode_file()` 最多接受 16 MiB 的文件和 1,024 x 1,024 像素，
以避免应用因损坏图像耗尽内存。直接使用 libpng 时仍应自行设置合理的大小限制。

### 输入法提供者

`leonos/inputm.h` 提供异步输入法接口。提供者进程调用
`leonos_inputm_register()` 注册后，循环调用 `leonos_inputm_provider_next()`
获取由活动编辑控件提交的按键，并必须针对每个事件调用
`leonos_inputm_provider_result()`。提交 UTF-8 文本使用
`LEONOS_INPUTM_RESULT_COMMIT`，组词和候选使用
`LEONOS_INPUTM_RESULT_COMPOSITION`，未处理的导航、删除和修饰键必须使用
`LEONOS_INPUTM_RESULT_PASSTHROUGH`，以保留应用原有按键语义。

标准 UI 编辑控件会自动声明焦点和候选位置。自绘控件应使用
`leonos_inputm_set_context()` 声明 `LEONOS_INPUTM_CONTEXT_FOCUSED`；密码、
凭据和其他安全字段必须额外声明 `LEONOS_INPUTM_CONTEXT_SECURE`，系统不会将
此类字段的按键发送给第三方提供者。候选可由桌面主题控件绘制，提供者也可声明
`LEONOS_INPUTM_RENDER_PIXELS` 后使用自己的像素窗口绘制。

使用 `make APP=examples/inputm_provider APP_NAME=inputm_provider` 构建示例。

输入法 API 包可以在 `[input_method]` 中提供 `settings_schema`。该文件由设置
应用直接读取，连续的 `[setting]` 节定义一个当前用户配置项；目前支持安全的布尔
项：

```ini
[setting]
key=myime_prediction
type=bool
default=1
label=Prediction
label_zh=联想输入
```

设置会立即写入该用户的 `.inputm.conf` 并通知已运行的提供者。包还可声明一个
可选的 `settings_app=provider-settings.elf`，由设置页启动提供者自己的设置程序；
此程序和 schema 都必须作为 API 包成员安装。

## 兼容性

`include/`、`lib/libc.a` 与运行时 LeonOS 版本必须匹配。升级 LeonOS 4
后，请使用同版本发布的 SDK 重新构建应用。SDK 仅面向 x86_64 LeonOS 4
用户态，不可用于内核、驱动或宿主系统程序。
