# InputM 输入法接口

InputM 把编辑控件和输入法提供者解耦。默认英文输入法由系统提供；第三方输入法
通常是独立进程，通过异步队列接收按键并返回组词、候选或提交结果。

## 提供者注册

```c
struct leonos_inputm_provider provider = {0};
/* 填充 id、name、abbreviation、startup_mode、render_flags。 */
leonos_inputm_register(&provider);
```

系统最多同时登记 `LEONOS_INPUTM_MAX_PROVIDERS` 个提供者。`id` 只能使用稳定的
短标识，`abbreviation` 用于状态栏显示。启动方式为：

- `LEONOS_INPUTM_START_MANUAL`：仅用户或设置页启动。
- `LEONOS_INPUTM_START_LOGIN`：当前用户登录时启动。
- `LEONOS_INPUTM_START_ON_DEMAND`：切换到该输入法时再启动。

`LEONOS_INPUTM_RENDER_CONTROLS` 表示使用系统候选控件，
`LEONOS_INPUTM_RENDER_PIXELS` 表示提供者自己绘制候选窗口。注册失败时不要继续
读取提供者队列；退出前调用 `leonos_inputm_unregister()`。

## 事件与结果

提供者循环调用 `leonos_inputm_provider_next()`：

```c
struct leonos_inputm_key_event key;
while (leonos_inputm_provider_next(&key) > 0) {
    struct leonos_inputm_result result = {0};
    result.sequence = key.sequence;
    result.client_pid = key.client_pid;
    result.window_id = key.window_id;
    result.type = LEONOS_INPUTM_RESULT_PASSTHROUGH;
    result.keycode = key.keycode;
    result.pressed = key.pressed;
    leonos_inputm_provider_result(&result);
}
```

`COMPOSITION` 更新预编辑文本和最多五个候选；`COMMIT` 提交 UTF-8 文本；
`CANCEL` 清空组词；`PASSTHROUGH` 把导航、删除和未处理的修饰键交还应用。
结果必须带回事件的 `sequence`、客户端进程和窗口 ID，避免串到其他编辑控件。

候选状态可通过 `leonos_inputm_get_state()` 读取，提供者配置变化用
`leonos_inputm_notify_config()` 通知。应用侧的 GUI 事件读取会自动处理已提交
文本；自绘编辑控件可使用 `leonos_inputm_submit_key()`、
`leonos_inputm_poll_result()`、`leonos_inputm_take_text()` 和
`leonos_inputm_take_key()`。

## 编辑上下文与安全

```c
leonos_inputm_set_current_context(
    LEONOS_INPUTM_CONTEXT_FOCUSED, caret_x, caret_y, caret_w, caret_h);
```

密码、登录、OOBE 凭据和浏览器密码框必须声明
`LEONOS_INPUTM_CONTEXT_SECURE`。安全上下文的按键不会发送给第三方提供者，
应用应在焦点切换时及时更新窗口 ID、光标位置和上下文标志。

## 用户配置和故障回退

提供者启用顺序、活动输入法和配置属于当前用户。`Win + Space` 的全局切换由
桌面优先消费，不会把 Space 继续传给编辑控件或打开开始菜单。提供者无响应或
崩溃时系统回退到英文；提供者不要依赖同步磁盘或网络操作来处理每个按键。

## API 包声明

输入法包的 `install.ini` 使用 `[input_method]`，字段包括 `type=input-method`、
`id`、`abbreviation`、`startup_mode`、`launch_after_install`、
`settings_schema` 和可选 `settings_app`。设置 schema 由设置应用读取并按用户
保存；详见 [PACKAGING.md](PACKAGING.md)。
