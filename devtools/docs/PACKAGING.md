# 应用、虚拟终端与 `.api` 包

## 普通应用布局

建议每个程序使用独立目录：

```text
0:/programs/myapp/myapp.elf
0:/programs/myapp/myapp.app.ini
0:/programs/myapp/icon.bmp        (可选)
```

桌面入口和文件关联由系统启动服务管理。应用自身不应写入其他用户的主目录，
也不要依赖当前工作目录；启动时使用绝对的 `0:/...` 路径或先检查 `getcwd()`。

## 虚拟终端 manifest

需要 Terminal 承载标准输入、输出和 ANSI 渲染的 ELF，在同目录放置同名
`.app.ini`：

```ini
[app]
terminal=1
```

`terminal=0` 或缺少 manifest 表示普通 GUI/后台程序。该标记只由桌面、文件管理器、
运行框、快捷方式和 `leonos_launch_argv()` 读取；底层 `execve()` 会替换当前进程，
但不会自动创建 Terminal。构建 API 包时可使用 `tools/build_api.py --virtual-terminal`
自动加入 manifest。

## `.api` 包结构

`.api` 是受限 tar，根目录必须包含 `install.ini`。最小示例：

```ini
[package]
format=leonos-api
version=1

[app]
name=Example
version=1.0.0
main_exe=example.elf
default_path=0:/programs/example
requires_admin=0
desktop_shortcut=1
icon=example.bmp
terminal=0
```

`main_exe`、`icon` 和输入法设置文件必须是包成员，路径不能为绝对路径，不能含
`..`。安装器会把程序放到共享程序目录（通常是 `0:/programs/<name>`），而不是
把 `0:/tools/<package>.api` 当成运行目录。`requires_admin=1` 或受保护目标路径
会触发管理员授权。

## 输入法包扩展

输入法包在 `install.ini` 追加：

```ini
[input_method]
type=input-method
id=oschinpt
abbreviation=中
startup_mode=on-demand
launch_after_install=0
settings_schema=settings.ini
settings_app=settings.elf
```

`startup_mode` 可为 `manual`、`login` 或 `on-demand`。`settings_schema` 是包内
INI 文件，设置应用按当前用户保存配置；`settings_app` 必须是包内 ELF。安装后
可自动登记输入法，但当前活动项仍由用户配置决定。

## 打包命令和检查

使用仓库里的 `tools/build_api.py` 生成包时，它会检查成员路径、输入法 ID、设置
文件和 ELF 后缀。发布前应检查：

- `install.ini` 的 `main_exe` 和所有声明文件都存在；
- 包内包含许可证、归属和必要的词库/资源；
- 程序使用目标用户可访问的安装路径；
- 终端程序有 `terminal=1`，GUI 程序没有误设；
- 输入法 provider 的 ID、缩写和启动模式与 `leonos/inputm.h` 一致。

安装失败时保留进度回调中的错误并允许重试；不要把部分解包目录当作安装成功。
