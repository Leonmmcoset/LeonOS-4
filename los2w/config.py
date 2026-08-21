"""Host-side los2w configuration."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, asdict
from pathlib import Path


def _config_dir() -> Path:
    base = os.environ.get("APPDATA") or os.environ.get("LOCALAPPDATA")
    if base:
        return Path(base) / "los2w"
    return Path.home() / ".los2w"


def _recent(values: object, current: str, limit: int = 10) -> list[str]:
    ordered: list[str] = []
    if current:
        ordered.append(current)
    if isinstance(values, list):
        ordered.extend(value for value in values if isinstance(value, str) and value)
    result: list[str] = []
    for value in ordered:
        if value not in result:
            result.append(value)
        if len(result) >= limit:
            break
    return result


@dataclass
class HostConfig:
    root_dir: str = ""
    last_elf: str = ""
    language: str = "en"
    ui_theme: str = "metro"
    guest_username: str = "los2w"
    guest_home: str = "/users/los2w"
    guest_admin: bool = True
    recent_elfs: list[str] | None = None
    recent_roots: list[str] | None = None


class ConfigStore:
    def __init__(self, path: Path | None = None):
        self.path = path or (_config_dir() / "config.json")

    def load(self) -> HostConfig:
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return HostConfig()
        cfg = HostConfig()
        for key in asdict(cfg):
            if key in data:
                setattr(cfg, key, data[key])
        if cfg.language not in ("en", "zh"):
            cfg.language = "en"
        if cfg.ui_theme not in ("metro", "win95"):
            cfg.ui_theme = "metro"
        cfg.recent_elfs = _recent(cfg.recent_elfs, cfg.last_elf)
        cfg.recent_roots = _recent(cfg.recent_roots, cfg.root_dir)
        return cfg

    def save(self, cfg: HostConfig) -> None:
        cfg.recent_elfs = _recent(cfg.recent_elfs, cfg.last_elf)
        cfg.recent_roots = _recent(cfg.recent_roots, cfg.root_dir)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(asdict(cfg), ensure_ascii=False, indent=2), encoding="utf-8")

    def reports_dir(self) -> Path:
        path = self.path.parent / "reports"
        path.mkdir(parents=True, exist_ok=True)
        return path
