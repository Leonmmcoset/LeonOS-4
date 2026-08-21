# UI 像素控件库

`leonos/ui.h` 是建立在窗口像素缓冲之上的无状态绘制函数和少量交互状态结构。
它不创建窗口、不保存应用数据，也不替应用运行事件循环。

## 基本流程

```c
struct leonos_ui_surface ui;
leonos_ui_bind(&ui, pixels, width, height, width);
leonos_ui_rect(&ui, 0, 0, width, height, LEONOS_UI_DESKTOP);
struct leonos_ui_window_parts parts;
leonos_ui_window(&ui, 0, 0, width, height, "Demo", 0, &parts);
leonos_ui_text(&ui, 16, 48, "Hello", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
leonos_gui_present_window(window, width, height, width, pixels);
```

`stride` 以像素为单位。所有绘制函数都要求矩形位于应用自己的缓冲区内；库会
裁剪部分边界，但应用仍应先验证外部尺寸。

## 主题

```c
leonos_ui_theme_load_system();
uint32_t theme = leonos_ui_theme();
uint32_t accent = leonos_ui_color(LEONOS_UI_COLOR_ACCENT);
```

样式为 `LEONOS_UI_THEME_WIN95` 或 `LEONOS_UI_THEME_METRO`。基础配色为
`BLUE`、`TEAL`、`GREEN`、`PURPLE`、`RED` 和 `GRAPHITE`。Metro 与 Win95 的
配色分别保存；调用 `leonos_ui_theme_set_appearance()` 时必须同时传入两套
选择，不能把一套主题的设置解释为另一套主题的设置。

颜色角色包括文本、内容、表面、微妙、弱化、强调、非活动标题、桌面、边框和
选中项。使用角色而不是硬编码 RGB，才能响应用户的个性化设置。

## 控件分类

- 基础绘制：`pixel`、`rect`、`text*`、`bevel`、`inset`、`panel`。
- 窗口和桌面：`window*`、`taskbar`、`taskbar_button`、`statusbar`、`toolbar`。
- 选择与导航：菜单、上下文菜单、下拉框、菜单栏、Tab、滚动条。
- 数据视图：列表、列表头/行、真正的树视图、属性网格、可拖动分栏。
- 编辑输入：单行编辑框、多行文本区、复选框、单选框、颜色输入、日期输入、
  滑块和步进器。
- 对话框和反馈：消息框、确认框、输入/密码框、打开/保存文件框、Toast、进度条。

交互控件分为绘制函数和 `*_state_*` 状态处理函数。应用应为每个编辑框、列表
或树保存独立状态，并把键盘、鼠标、滚轮事件交给对应的处理函数；返回值表示
事件是否被控件消费。

## 文本输入和安全字段

单行编辑状态通过 `leonos_ui_edit_state_init/sync/handle_key/handle_mouse` 管理，
多行编辑使用 `leonos_ui_text_area_state_*`。获得焦点时要调用
`leonos_inputm_set_current_context(LEONOS_INPUTM_CONTEXT_FOCUSED, ...)`；密码、
登录凭据和其他敏感字段必须加 `LEONOS_INPUTM_CONTEXT_SECURE` 或
`LEONOS_UI_EDIT_SECURE`，这样第三方输入法不会收到按键。

## 文件和常用对话框

```c
char path[256] = "/users/admin";
if (leonos_ui_show_open_dialog("Open", path, sizeof(path),
                               "Text files", ".txt") == 0) {
    /* path 已由对话框填充。 */
}
```

同一组 API 还提供保存、打开方式、输入、密码、确认和消息框。对话框返回后仍
应检查路径长度、文件类型和 ACL；UI 选择文件不等于应用已经获得读写权限。

## 字体

`leonos_ui_set_font_path()` 和 `leonos_ui_set_font_fallback_path()` 设置当前
进程的字体文件。路径必须指向可读的 LeonOS 文件；失败时保留现有字体。中文等
非 ASCII 文本应使用 UTF-8，并可用 `leonos_text_layout_utf8()` 计算字形宽度。
