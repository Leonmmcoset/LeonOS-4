# GUI 窗口与事件

## 连接和创建窗口

```c
#include <leonos/gui.h>

int version = leonos_gui_connect();
int window = leonos_gui_create_app_window_ex(
    "My app", "", 640, 400, 0);
```

`leonos_gui_connect()` 返回 GUI 协议版本，失败时应停止继续绘制。窗口创建
失败返回负值。常用创建标志：

- `LEONOS_GUI_WINDOW_NO_RESIZE`：禁止用户调整大小。
- `LEONOS_GUI_WINDOW_FULLSCREEN`：请求全屏窗口。
- `LEONOS_GUI_WINDOW_BORDERLESS`：隐藏窗口边框和标题栏按钮。
- `LEONOS_GUI_WINDOW_HIDE_TASKBAR`：只隐藏该窗口在任务栏中的按钮。

窗口 ID 只对创建它的进程有效。销毁窗口后不要继续提交像素或发送窗口事件。

## 绘制和提交

推荐为窗口分配自己的 `0x00RRGGBB` 像素数组，绘制完成后一次提交：

```c
leonos_gui_present_window(window, width, height, width, pixels);
```

`stride` 是每行像素数，不是字节数。提交尺寸必须与缓冲区容量一致，避免整数
溢出。`leonos_gui_fetch_window()` 只用于读取自己窗口的服务端副本；它不是共享
窗口内存，也不能用来修改其他窗口。

低级 `leonos_fb_*` 接口适合诊断或全屏服务程序。普通应用应使用窗口提交，
不要假设物理 framebuffer 永远可用。

`leonos_fb_set_mode(width, height)` 仅供 Desktop 窗口服务器使用。调用前先查询
`leonos_fb_capabilities()`：只有 `capabilities & LEONOS_FB_CAP_MODE_SET` 非零时才可请求
模式切换；请求尺寸同时不能超过 `max_width`、`max_height`，且 32 位像素缓冲不得
超过 `max_bytes`。当前内核为 QEMU 标准 VGA/Bochs VBE 和 VMware SVGA II 提供该能力。

## 事件循环

```c
struct leonos_gui_app_event event;
while (leonos_gui_wait_app_event(&event, 100) >= 0) {
    switch (event.type) {
    case LEONOS_GUI_APP_EVENT_CLOSE:
        leonos_gui_destroy_app_window(event.window_id);
        return 0;
    case LEONOS_GUI_APP_EVENT_RESIZE:
        /* 重新分配与 event.width/event.height 匹配的像素缓冲区。 */
        break;
    case LEONOS_GUI_APP_EVENT_KEY_DOWN:
    case LEONOS_GUI_APP_EVENT_KEY_UP:
        /* 使用 event.keycode 和 event.pressed。 */
        break;
    }
}
```

事件类型包括关闭、获得/失去焦点、调整大小、鼠标移动/按钮/滚轮、键盘按下/释放
和 `LEONOS_GUI_APP_EVENT_THEME_CHANGED`。超时等待返回后应继续处理绘制和后台
任务，避免在 UI 进程中做长时间同步磁盘或网络操作。

窗口标题、边框和任务栏可运行时修改：

```c
leonos_gui_set_window_title(window, "Ready");
leonos_gui_set_window_borderless(window, 1);
leonos_gui_set_window_taskbar_visible(window, 0);
leonos_gui_set_taskbar_visible(window, 0);
```

最后一个函数控制整个桌面任务栏；应用应在退出前恢复它。窗口控制只允许操作
本进程创建的窗口。

## 鼠标

```c
leonos_mouse_hide(window);
leonos_mouse_set_position(window, 320, 200);
leonos_mouse_set_style(window, LEONOS_GUI_CURSOR_HAND);
leonos_mouse_show(window);
```

坐标是桌面逻辑像素。光标样式包括箭头、手形、文本、等待、十字和移动。隐藏
鼠标是按窗口记录的，窗口销毁或应用退出后桌面会恢复默认可见状态。

## 主题与显示

应用收到主题变更事件后应重新读取 `leonos_ui_color()` 返回的角色颜色并重绘。
显示缩放、壁纸和主题由桌面设置应用管理；应用不应把自己的主题配置写入系统
级文件，也不要假定 Metro 与 Win95 使用同一套配色值。
