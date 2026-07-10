"""PySide6 backed LeonOS GUI ioctl emulation."""

from __future__ import annotations

import struct
import time
from collections import deque

from PySide6.QtCore import Qt
from PySide6.QtGui import QCloseEvent, QImage, QKeyEvent, QMouseEvent, QPixmap, QWheelEvent
from PySide6.QtWidgets import QApplication, QLabel

from . import constants as C
from . import structs
from .errors import EACCES, EINVAL, neg


QT_KEY_TO_LEONOS = {
    Qt.Key.Key_Escape: C.KEY_ESCAPE,
    Qt.Key.Key_Backspace: C.KEY_BACKSPACE,
    Qt.Key.Key_Tab: C.KEY_TAB,
    Qt.Key.Key_Return: C.KEY_ENTER,
    Qt.Key.Key_Enter: C.KEY_ENTER,
    Qt.Key.Key_Shift: C.KEY_LEFT_SHIFT,
    Qt.Key.Key_Control: C.KEY_LEFT_CTRL,
    Qt.Key.Key_Alt: C.KEY_LEFT_ALT,
    Qt.Key.Key_Space: C.KEY_SPACE,
    Qt.Key.Key_Home: C.KEY_HOME,
    Qt.Key.Key_PageUp: C.KEY_PAGE_UP,
    Qt.Key.Key_Left: C.KEY_LEFT,
    Qt.Key.Key_Right: C.KEY_RIGHT,
    Qt.Key.Key_End: C.KEY_END,
    Qt.Key.Key_Down: C.KEY_DOWN,
    Qt.Key.Key_PageDown: C.KEY_PAGE_DOWN,
    Qt.Key.Key_Insert: C.KEY_INSERT,
    Qt.Key.Key_Delete: C.KEY_DELETE,
    Qt.Key.Key_Up: C.KEY_UP,
    Qt.Key.Key_1: 2,
    Qt.Key.Key_2: 3,
    Qt.Key.Key_3: 4,
    Qt.Key.Key_4: 5,
    Qt.Key.Key_5: 6,
    Qt.Key.Key_6: 7,
    Qt.Key.Key_7: 8,
    Qt.Key.Key_8: 9,
    Qt.Key.Key_9: 10,
    Qt.Key.Key_0: 11,
    Qt.Key.Key_Minus: 12,
    Qt.Key.Key_Equal: 13,
    Qt.Key.Key_Q: 16,
    Qt.Key.Key_W: 17,
    Qt.Key.Key_E: 18,
    Qt.Key.Key_R: 19,
    Qt.Key.Key_T: 20,
    Qt.Key.Key_Y: 21,
    Qt.Key.Key_U: 22,
    Qt.Key.Key_I: 23,
    Qt.Key.Key_O: 24,
    Qt.Key.Key_P: 25,
    Qt.Key.Key_BracketLeft: 26,
    Qt.Key.Key_BracketRight: 27,
    Qt.Key.Key_A: 30,
    Qt.Key.Key_S: 31,
    Qt.Key.Key_D: 32,
    Qt.Key.Key_F: 33,
    Qt.Key.Key_G: 34,
    Qt.Key.Key_H: 35,
    Qt.Key.Key_J: 36,
    Qt.Key.Key_K: 37,
    Qt.Key.Key_L: 38,
    Qt.Key.Key_Semicolon: 39,
    Qt.Key.Key_Apostrophe: 40,
    Qt.Key.Key_QuoteLeft: 41,
    Qt.Key.Key_Backslash: 43,
    Qt.Key.Key_Z: 44,
    Qt.Key.Key_X: 45,
    Qt.Key.Key_C: 46,
    Qt.Key.Key_V: 47,
    Qt.Key.Key_B: 48,
    Qt.Key.Key_N: 49,
    Qt.Key.Key_M: 50,
    Qt.Key.Key_Comma: 51,
    Qt.Key.Key_Period: 52,
    Qt.Key.Key_Slash: 53,
}


def _buttons(mask: Qt.MouseButton | Qt.MouseButtons) -> int:
    value = 0
    if mask & Qt.MouseButton.LeftButton:
        value |= 1
    if mask & Qt.MouseButton.RightButton:
        value |= 2
    if mask & Qt.MouseButton.MiddleButton:
        value |= 4
    return value


class GuestWindow(QLabel):
    def __init__(self, manager: "GUIManager", window_id: int, title: str, width: int, height: int):
        super().__init__()
        self.manager = manager
        self.window_id = window_id
        self.setWindowTitle(title or f"LeonOS window {window_id}")
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self._suppress_initial_events = True
        self.resize(max(1, width), max(1, height))

    def enqueue(self, event: structs.GuiAppEvent) -> None:
        event.window_id = self.window_id
        self.manager.enqueue(event)

    def closeEvent(self, event: QCloseEvent) -> None:
        self.enqueue(structs.GuiAppEvent(type=C.APP_EVENT_CLOSE))
        event.accept()

    def resizeEvent(self, event) -> None:
        if self._suppress_initial_events:
            super().resizeEvent(event)
            return
        size = event.size()
        self.enqueue(structs.GuiAppEvent(type=C.APP_EVENT_RESIZE, width=size.width(), height=size.height()))
        super().resizeEvent(event)

    def focusInEvent(self, event) -> None:
        if self._suppress_initial_events:
            super().focusInEvent(event)
            return
        self.enqueue(structs.GuiAppEvent(type=C.APP_EVENT_FOCUS, width=self.width(), height=self.height()))
        super().focusInEvent(event)

    def focusOutEvent(self, event) -> None:
        if self._suppress_initial_events:
            super().focusOutEvent(event)
            return
        self.enqueue(structs.GuiAppEvent(type=C.APP_EVENT_BLUR, width=self.width(), height=self.height()))
        super().focusOutEvent(event)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        pos = event.position().toPoint()
        self.enqueue(
            structs.GuiAppEvent(
                type=C.APP_EVENT_MOUSE_MOVE,
                x=pos.x(),
                y=pos.y(),
                buttons=_buttons(event.buttons()),
            )
        )
        super().mouseMoveEvent(event)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        pos = event.position().toPoint()
        self.enqueue(
            structs.GuiAppEvent(
                type=C.APP_EVENT_MOUSE_BUTTON,
                x=pos.x(),
                y=pos.y(),
                buttons=_buttons(event.buttons() | event.button()),
                pressed=1,
            )
        )
        super().mousePressEvent(event)

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        pos = event.position().toPoint()
        self.enqueue(
            structs.GuiAppEvent(
                type=C.APP_EVENT_MOUSE_BUTTON,
                x=pos.x(),
                y=pos.y(),
                buttons=_buttons(event.buttons()),
                pressed=0,
            )
        )
        super().mouseReleaseEvent(event)

    def wheelEvent(self, event: QWheelEvent) -> None:
        delta = event.angleDelta()
        pos = event.position().toPoint()
        self.enqueue(
            structs.GuiAppEvent(
                type=C.APP_EVENT_MOUSE_WHEEL,
                x=pos.x(),
                y=pos.y(),
                dx=delta.x() // 120,
                dy=delta.y() // 120,
                buttons=_buttons(event.buttons()),
            )
        )
        super().wheelEvent(event)

    def keyPressEvent(self, event: QKeyEvent) -> None:
        self._key(event, True)
        super().keyPressEvent(event)

    def keyReleaseEvent(self, event: QKeyEvent) -> None:
        self._key(event, False)
        super().keyReleaseEvent(event)

    def _key(self, event: QKeyEvent, pressed: bool) -> None:
        keycode = QT_KEY_TO_LEONOS.get(event.key())
        if not keycode:
            return
        self.enqueue(structs.GuiAppEvent(type=C.APP_EVENT_KEY_DOWN if pressed else C.APP_EVENT_KEY_UP, keycode=keycode, pressed=1 if pressed else 0))


class GUIManager:
    def __init__(self, logger=None, *, ui_theme: str = "metro", allow_theme_changes: bool = True):
        self.logger = logger
        self.windows: dict[int, GuestWindow] = {}
        self.frames: dict[int, tuple[int, int, int, bytes]] = {}
        self.events: deque[structs.GuiAppEvent] = deque(maxlen=32)
        self.next_window_id = 1
        self.start_time = time.monotonic()
        self.present_count = 0
        self._processing_events = False
        self._present_callback = None
        self._theme_change_callback = None
        self.appearance_theme = self._appearance_theme_value(ui_theme)
        self.allow_theme_changes = allow_theme_changes

    @staticmethod
    def _appearance_theme_value(ui_theme: str) -> int:
        return C.UI_THEME_WIN95 if ui_theme == "win95" else C.UI_THEME_METRO

    @staticmethod
    def _appearance_theme_name(theme: int) -> str:
        return "win95" if theme == C.UI_THEME_WIN95 else "metro"

    def log(self, text: str) -> None:
        if self.logger:
            self.logger.write(text)

    def set_present_callback(self, callback) -> None:
        self._present_callback = callback

    def set_theme_change_callback(self, callback) -> None:
        self._theme_change_callback = callback

    def configure_appearance(self, ui_theme: str, allow_theme_changes: bool) -> None:
        self.appearance_theme = self._appearance_theme_value(ui_theme)
        self.allow_theme_changes = allow_theme_changes

    def _set_appearance_theme(self, theme: int, *, notify: bool) -> int:
        if theme not in (C.UI_THEME_WIN95, C.UI_THEME_METRO):
            return neg(EINVAL)
        changed = self.appearance_theme != theme
        self.appearance_theme = theme
        if self._theme_change_callback:
            self._theme_change_callback(self._appearance_theme_name(theme))
        if changed and notify:
            for window_id in self.windows:
                self.enqueue(structs.GuiAppEvent(window_id=window_id,
                                                  type=C.APP_EVENT_THEME_CHANGED,
                                                  x=theme))
        return 1

    def enqueue(self, event: structs.GuiAppEvent) -> None:
        if event.type in (C.APP_EVENT_FOCUS, C.APP_EVENT_RESIZE):
            for idx in range(len(self.events) - 1, -1, -1):
                old = self.events[idx]
                if old.window_id == event.window_id and old.type == event.type:
                    self.events[idx] = event
                    return
        if event.type == C.APP_EVENT_MOUSE_MOVE:
            for idx in range(len(self.events) - 1, -1, -1):
                old = self.events[idx]
                if old.window_id == event.window_id and old.type == event.type:
                    self.events[idx] = event
                    return
        self.events.append(event)

    def pending_event_count(self) -> int:
        self.process_events()
        return len(self.events)

    def process_events(self) -> None:
        if self._processing_events:
            return
        app = QApplication.instance()
        if app:
            self._processing_events = True
            try:
                app.processEvents()
            finally:
                self._processing_events = False

    def has_pending_guest_events(self) -> bool:
        self.process_events()
        return bool(self.events)

    def idle_sleep(self, ms: int) -> None:
        self.process_events()
        if self.events:
            return
        delay = min(max(int(ms), 0), 1) / 1000.0
        if delay > 0:
            time.sleep(delay)
        self.process_events()

    def ioctl(self, memory, request: int, arg: int) -> int | None:
        if request == C.GUI_IOCTL_VERSION:
            return 1
        if request == C.GUI_IOCTL_PATH_TEST:
            return 0
        if request == C.GUI_IOCTL_UPTIME_MS:
            return int((time.monotonic() - self.start_time) * 1000)
        if request == C.GUI_IOCTL_FB_INFO:
            memory.write(arg, struct.pack("<IIIb3x", 1024, 768, 1024, 32))
            return 0
        if request == C.GUI_IOCTL_CREATE_WINDOW:
            return self._create_window(memory, arg)
        if request == C.GUI_IOCTL_PRESENT_WINDOW:
            return self._present_window(memory, arg)
        if request == C.GUI_IOCTL_FETCH_WINDOW:
            return self._fetch_window(memory, arg)
        if request == C.GUI_IOCTL_WINDOW_EVENT:
            return self._window_event(memory, arg)
        if request == C.GUI_IOCTL_WAIT_WINDOW_EVENT:
            return self._window_event(memory, arg)
        if request == C.GUI_IOCTL_SEND_WINDOW_EVENT:
            event = structs.GuiAppEvent.unpack(memory.read(arg, structs.GuiAppEvent.SIZE))
            self.enqueue(event)
            return 0
        if request == C.GUI_IOCTL_DESTROY_WINDOW:
            return self._destroy_window(arg)
        if request == C.GUI_IOCTL_EVENT:
            return 0
        if request == C.GUI_IOCTL_POLL_WINDOW:
            return 0
        if request == C.GUI_IOCTL_TASKS:
            memory.write_u32(arg + 4, 0)
            return 0
        if request == C.GUI_IOCTL_DISPLAY_STATE:
            memory.write(arg, struct.pack("<IIIIIIIII", 1024, 768, 1024, 768, 1, 0, 0, 0, 0))
            return 0
        if request == C.GUI_IOCTL_APPEARANCE_STATE:
            memory.write_u32(arg, self.appearance_theme)
            return 1
        if request == C.GUI_IOCTL_APPEARANCE_REQUEST:
            if not self.allow_theme_changes:
                return neg(EACCES)
            return self._set_appearance_theme(memory.read_u32(arg), notify=True)
        if request == C.GUI_IOCTL_POLL_APPEARANCE_REQUEST:
            return 0
        if request == C.GUI_IOCTL_PUBLISH_APPEARANCE_STATE:
            return self._set_appearance_theme(memory.read_u32(arg), notify=False)
        return None

    def _create_window(self, memory, arg: int) -> int:
        query = structs.GuiCreate.unpack(memory.read(arg, structs.GuiCreate.SIZE))
        title = memory.read_cstr(query.title_ptr, 256)
        window_id = self.next_window_id
        self.next_window_id += 1
        window = GuestWindow(self, window_id, title, query.width, query.height)
        self.windows[window_id] = window
        window.show()
        window.raise_()
        window.activateWindow()
        self.process_events()
        window._suppress_initial_events = False
        self.log(f"[gui] create window id={window_id} title={title!r} size={query.width}x{query.height}")
        return window_id

    def _present_window(self, memory, arg: int) -> int:
        query = structs.GuiPresent.unpack(memory.read(arg, structs.GuiPresent.SIZE))
        window = self.windows.get(query.window_id)
        if not window or not query.width or not query.height or not query.stride:
            return neg(EINVAL)
        raw = bytearray(memory.read(query.pixels_ptr, query.stride * query.height * 4))
        raw[3::4] = b"\xff" * (len(raw) // 4)
        frame_data = bytes(raw)
        image = QImage(frame_data, query.width, query.height, query.stride * 4, QImage.Format.Format_RGB32).copy()
        window.setPixmap(QPixmap.fromImage(image))
        if window.width() != query.width or window.height() != query.height:
            window.resize(query.width, query.height)
        self.frames[query.window_id] = (query.width, query.height, query.stride, frame_data)
        self.present_count += 1
        if self._present_callback:
            self._present_callback()
        self.process_events()
        return 0

    def _fetch_window(self, memory, arg: int) -> int:
        query = structs.GuiFetch.unpack(memory.read(arg, structs.GuiFetch.SIZE))
        frame = self.frames.get(query.window_id)
        if not frame:
            return neg(EINVAL)
        width, height, stride, data = frame
        copy_w = min(width, query.capacity_width)
        copy_h = min(height, query.capacity_height)
        for row in range(copy_h):
            start = row * stride * 4
            memory.write(query.pixels_ptr + row * query.stride * 4, data[start : start + copy_w * 4])
        query.out_width = copy_w
        query.out_height = copy_h
        memory.write(arg, query.pack())
        return 0

    def _window_event(self, memory, arg: int) -> int:
        self.process_events()
        requested = 0
        if arg:
            try:
                requested = structs.GuiAppEvent.unpack(memory.read(arg, structs.GuiAppEvent.SIZE)).window_id
            except Exception:
                requested = 0
        event = None
        for idx, candidate in enumerate(self.events):
            if not requested or candidate.window_id == requested:
                event = candidate
                del self.events[idx]
                break
        if not event:
            return 0
        memory.write(arg, event.pack())
        return 1

    def _destroy_window(self, window_id: int) -> int:
        window = self.windows.pop(window_id, None)
        if window:
            window.close()
            self.process_events()
        return 0
