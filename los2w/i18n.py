"""Small bilingual text helper for the los2w host UI."""

from __future__ import annotations

TEXT = {
    "app_title": ("los2w - LeonOS 4 Runner", "los2w - LeonOS 4 运行器"),
    "elf": ("ELF application", "ELF 应用"),
    "root": ("/ root directory", "/ 根目录"),
    "argv": ("Arguments", "参数"),
    "browse": ("Browse", "浏览"),
    "run": ("Run", "运行"),
    "stop": ("Stop", "停止"),
    "export_report": ("Export report", "导出报告"),
    "language": ("Language", "语言"),
    "english": ("English", "英文"),
    "chinese": ("Chinese", "中文"),
    "ui_style": ("App UI style", "应用界面样式"),
    "metro": ("Metro (modern)", "Metro（新版）"),
    "win95": ("Win95 (classic)", "Win95（旧版）"),
    "log": ("Log", "日志"),
    "missing_root": ("Choose a / root directory first.", "请先选择 / 根目录。"),
    "missing_elf": ("Choose a LeonOS ELF application first.", "请先选择 LeonOS ELF 应用。"),
    "stopped": ("Guest stopped.", "Guest 已停止。"),
    "failed": ("Guest failed", "Guest 失败"),
    "select_elf": ("Select LeonOS ELF", "选择 LeonOS ELF"),
    "select_root": ("Select / root", "选择 / 根目录"),
    "smoke_ok": ("Smoke test reached a presented window.", "烟测已显示窗口。"),
    "smoke_timeout": ("Smoke timeout reached.", "烟测超时结束。"),
    "report_saved": ("Diagnostic report saved:", "诊断报告已保存："),
    "no_report": ("Run a guest application before exporting a report.", "请先运行一个来宾应用再导出报告。"),
}


def t(key: str, lang: str = "en") -> str:
    en, zh = TEXT.get(key, (key, key))
    return zh if lang == "zh" else en
