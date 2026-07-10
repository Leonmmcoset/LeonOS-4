"""los2w command line and PySide6 launcher."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from .config import ConfigStore, HostConfig
from .diagnostics import default_report_path, write_report
from .emulator import LeonOSEmulator
from .errors import GuestFault
from .i18n import t
from .logging import LogBuffer
from .selftest import run_self_tests


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run LeonOS 4 ELF applications on Windows Python.")
    parser.add_argument("--elf", help="LeonOS ELF application to run")
    parser.add_argument("--root", help="Host directory mapped as LeonOS 0:/")
    parser.add_argument("--arg", action="append", default=[], help="Guest argv item after argv[0]")
    parser.add_argument("--smoke", action="store_true", help="Stop successfully after the first presented frame")
    parser.add_argument("--self-test", action="store_true", help="Run los2w self-tests")
    parser.add_argument("--lang", choices=("en", "zh"), help="los2w UI and virtual guest locale")
    parser.add_argument("--report", help="Write a diagnostic JSON report to this path")
    parser.add_argument("--compat-report", action="store_true", help="Write a compatibility report to the los2w reports directory")
    return parser.parse_args(argv)


class MainWindowMixin:
    def _setup_los2w_ui(self):
        from PySide6.QtCore import Qt, QTimer
        from PySide6.QtWidgets import (
            QFileDialog,
            QComboBox,
            QGridLayout,
            QHBoxLayout,
            QLabel,
            QLineEdit,
            QMessageBox,
            QPushButton,
            QTextEdit,
            QWidget,
        )

        self.Qt = Qt
        self.QFileDialog = QFileDialog
        self.QMessageBox = QMessageBox
        self.store = ConfigStore()
        self.cfg = self.store.load()
        self.current_emulator = None
        self.current_logger = None
        self._stepping_guest = False
        self.run_timer = QTimer(self)
        self.run_timer.setInterval(0)
        self.run_timer.timeout.connect(self._step_guest)

        self.elf_edit = QComboBox()
        self.elf_edit.setEditable(True)
        self.elf_edit.addItems(self.cfg.recent_elfs or [])
        self.elf_edit.setCurrentText(self.cfg.last_elf)
        self.root_edit = QComboBox()
        self.root_edit.setEditable(True)
        self.root_edit.addItems(self.cfg.recent_roots or [])
        self.root_edit.setCurrentText(self.cfg.root_dir)
        self.argv_edit = QLineEdit("")
        self.lang_combo = QComboBox()
        self.lang_combo.addItem(t("english", "en"), "en")
        self.lang_combo.addItem(t("chinese", "zh"), "zh")
        idx = 1 if self.cfg.language == "zh" else 0
        self.lang_combo.setCurrentIndex(idx)
        self.log_box = QTextEdit()
        self.log_box.setReadOnly(True)
        self.run_button = QPushButton()
        self.stop_button = QPushButton()
        self.stop_button.setEnabled(False)
        self.report_button = QPushButton()

        elf_button = QPushButton()
        root_button = QPushButton()
        elf_button.clicked.connect(self._browse_elf)
        root_button.clicked.connect(self._browse_root)
        self.run_button.clicked.connect(self._run_guest)
        self.stop_button.clicked.connect(self._stop_guest)
        self.report_button.clicked.connect(self._export_diagnostics)
        self.lang_combo.currentIndexChanged.connect(self._refresh_text)

        grid = QGridLayout()
        self.elf_label = QLabel()
        self.root_label = QLabel()
        self.argv_label = QLabel()
        self.lang_label = QLabel()
        grid.addWidget(self.elf_label, 0, 0)
        grid.addWidget(self.elf_edit, 0, 1)
        grid.addWidget(elf_button, 0, 2)
        grid.addWidget(self.root_label, 1, 0)
        grid.addWidget(self.root_edit, 1, 1)
        grid.addWidget(root_button, 1, 2)
        grid.addWidget(self.argv_label, 2, 0)
        grid.addWidget(self.argv_edit, 2, 1, 1, 2)
        grid.addWidget(self.lang_label, 3, 0)
        grid.addWidget(self.lang_combo, 3, 1, 1, 2)
        buttons = QHBoxLayout()
        buttons.addStretch(1)
        buttons.addWidget(self.run_button)
        buttons.addWidget(self.stop_button)
        buttons.addWidget(self.report_button)
        grid.addLayout(buttons, 4, 0, 1, 3)
        self.log_label = QLabel()
        grid.addWidget(self.log_label, 5, 0, 1, 3)
        grid.addWidget(self.log_box, 6, 0, 1, 3)
        grid.setColumnStretch(1, 1)
        root = QWidget()
        root.setLayout(grid)
        self.setCentralWidget(root)
        self._elf_button = elf_button
        self._root_button = root_button
        self._refresh_text()

    def _lang(self) -> str:
        return self.lang_combo.currentData() or "en"

    def _refresh_text(self) -> None:
        lang = self._lang()
        self.setWindowTitle(t("app_title", lang))
        self.elf_label.setText(t("elf", lang))
        self.root_label.setText(t("root", lang))
        self.argv_label.setText(t("argv", lang))
        self.lang_label.setText(t("language", lang))
        self.log_label.setText(t("log", lang))
        self.run_button.setText(t("run", lang))
        self.stop_button.setText(t("stop", lang))
        self.report_button.setText(t("export_report", lang))
        self._elf_button.setText(t("browse", lang))
        self._root_button.setText(t("browse", lang))

    def _browse_elf(self) -> None:
        path, _ = self.QFileDialog.getOpenFileName(self, t("select_elf", self._lang()), self.elf_edit.currentText() or ".", "ELF (*.elf);;All files (*)")
        if path:
            self.elf_edit.setCurrentText(path)

    def _browse_root(self) -> None:
        path = self.QFileDialog.getExistingDirectory(self, t("select_root", self._lang()), self.root_edit.currentText() or ".")
        if path:
            self.root_edit.setCurrentText(path)

    def _append_log(self, line: str) -> None:
        self.log_box.append(line)

    def _save_cfg(self) -> HostConfig:
        self.cfg.last_elf = self.elf_edit.currentText().strip()
        self.cfg.root_dir = self.root_edit.currentText().strip()
        self.cfg.language = self._lang()
        self.store.save(self.cfg)
        return self.cfg

    def _run_guest(self) -> None:
        elf = self.elf_edit.currentText().strip()
        root = self.root_edit.currentText().strip()
        lang = self._lang()
        if not elf:
            self.QMessageBox.warning(self, t("app_title", lang), t("missing_elf", lang))
            return
        if not root:
            self.QMessageBox.warning(self, t("app_title", lang), t("missing_root", lang))
            return
        self.run_button.setEnabled(False)
        self.stop_button.setEnabled(True)
        cfg = self._save_cfg()
        logger = LogBuffer(self._append_log)
        try:
            from .gui import GUIManager

            gui = GUIManager(logger=logger)
            argv = [a for a in self.argv_edit.text().split(" ") if a]
            self.current_emulator = LeonOSEmulator(elf, root, argv, config=cfg, gui=gui, logger=logger)
            self.current_logger = logger
            self._stepping_guest = False
            self.run_timer.start()
        except GuestFault as exc:
            logger.write(f"[los2w] failed: {exc}")
            self._export_diagnostics(auto=True, reason=str(exc))
            self.QMessageBox.critical(self, t("failed", lang), str(exc))
            self._finish_guest()
        except Exception as exc:
            logger.write(f"[los2w] internal failure: {exc}")
            self._export_diagnostics(auto=True, reason=f"internal failure: {exc}")
            self.QMessageBox.critical(self, t("failed", lang), str(exc))
            self._finish_guest()

    def _step_guest(self) -> None:
        if self._stepping_guest:
            return
        emulator = self.current_emulator
        if not emulator:
            self.run_timer.stop()
            return
        lang = self._lang()
        self._stepping_guest = True
        try:
            self._run_guest_budgeted(emulator)
        except GuestFault as exc:
            if self.current_logger:
                self.current_logger.write(f"[los2w] failed: {exc}")
            self._export_diagnostics(auto=True, reason=str(exc))
            self.QMessageBox.critical(self, t("failed", lang), str(exc))
            self._finish_guest()
        finally:
            self._stepping_guest = False

    def _run_guest_budgeted(self, emulator: LeonOSEmulator) -> None:
        gui = emulator.gui
        present_before = getattr(gui, "present_count", 0)
        event_count = gui.pending_event_count() if hasattr(gui, "pending_event_count") else 0
        booting = present_before == 0
        budget = 0.050 if booting else (0.012 if event_count else 0.003)
        deadline = time.perf_counter() + budget
        ran_once = False
        while self.current_emulator is emulator:
            present_count = getattr(gui, "present_count", 0)
            event_count = gui.pending_event_count() if hasattr(gui, "pending_event_count") else 0
            if present_count == 0:
                insns = 1000000
                timeout_us = 0
            elif event_count:
                insns = 200000
                timeout_us = 0
            else:
                insns = 50000
                timeout_us = 0
            running = emulator.run_step(insns, timeout_us=timeout_us, pump_events=False)
            ran_once = True
            if self.current_emulator is not emulator:
                return
            if not running:
                code = emulator.exit_code
                if self.current_logger:
                    self.current_logger.write(f"[los2w] exit code={code}")
                self._finish_guest()
                return
            if booting and getattr(gui, "present_count", 0) == 0 and time.perf_counter() < deadline:
                continue
            if event_count and time.perf_counter() < deadline:
                if gui.pending_event_count() or getattr(gui, "present_count", 0) == present_before:
                    continue
            break
        if not ran_once and self.current_emulator is emulator:
            self.run_timer.start()

    def _finish_guest(self) -> None:
        self.run_timer.stop()
        self.current_emulator = None
        self.current_logger = None
        self._stepping_guest = False
        self.stop_button.setEnabled(False)
        self.run_button.setEnabled(True)

    def _export_diagnostics(self, checked: bool = False, *, auto: bool = False,
                            reason: str | None = None) -> None:
        del checked
        emulator = self.current_emulator
        logger = self.current_logger
        if not emulator or not logger:
            if not auto:
                self.QMessageBox.information(self, t("app_title", self._lang()),
                                              t("no_report", self._lang()))
            return
        if auto:
            target = default_report_path(self.store.reports_dir(), "los2w-crash")
        else:
            default = str(default_report_path(self.store.reports_dir()))
            selected, _ = self.QFileDialog.getSaveFileName(
                self, t("export_report", self._lang()), default,
                "JSON (*.json)",
            )
            if not selected:
                return
            target = Path(selected)
        saved = write_report(target, logger, emulator, reason)
        logger.write(f"[los2w] {t('report_saved', self._lang())} {saved}")

    def _stop_guest(self) -> None:
        if self.current_emulator:
            self.current_emulator.stop()
        self._finish_guest()


def run_gui() -> int:
    from PySide6.QtWidgets import QApplication, QMainWindow

    class MainWindow(MainWindowMixin, QMainWindow):
        def __init__(self):
            super().__init__()
            self._setup_los2w_ui()

    app = QApplication.instance() or QApplication(sys.argv)
    win = MainWindow()
    win.resize(780, 520)
    win.show()
    return app.exec()


def run_elf_cli(args: argparse.Namespace) -> int:
    from PySide6.QtWidgets import QApplication

    app = QApplication.instance() or QApplication(sys.argv)
    cfg = HostConfig(root_dir=args.root or "", last_elf=args.elf or "", language=args.lang or "en")
    from .gui import GUIManager

    logger = LogBuffer(lambda line: print(line, flush=True))
    gui = GUIManager(logger=logger)
    emu = LeonOSEmulator(args.elf, args.root, args.arg, config=cfg, gui=gui, logger=logger)
    code = None
    fault: Exception | None = None
    try:
        code = emu.run(max_seconds=5.0 if args.smoke else None, smoke=args.smoke)
    except Exception as exc:
        fault = exc
        logger.write(f"[los2w] failed: {exc}")
    finally:
        app.processEvents()
        report_target = Path(args.report) if args.report else None
        if report_target is None and (args.compat_report or fault is not None):
            report_target = default_report_path(ConfigStore().reports_dir(),
                                                "los2w-crash" if fault else "los2w-compat")
        if report_target is not None:
            saved = write_report(report_target, logger, emu,
                                 str(fault) if fault else "command line report")
            print(f"[los2w] report saved: {saved}")
    if fault is not None:
        return 1
    if args.smoke:
        if gui.present_count > 0:
            print(t("smoke_ok", cfg.language))
            return 0
        print(t("smoke_timeout", cfg.language))
        return 2
    return int(code or 0)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.self_test:
        for line in run_self_tests():
            print(line)
        return 0
    if args.elf or args.root:
        if not args.elf or not args.root:
            print("--elf and --root must be used together", file=sys.stderr)
            return 2
        return run_elf_cli(args)
    return run_gui()


if __name__ == "__main__":
    raise SystemExit(main())
