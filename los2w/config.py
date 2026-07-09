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


@dataclass
class HostConfig:
    root_dir: str = ""
    last_elf: str = ""
    language: str = "en"
    guest_username: str = "los2w"
    guest_home: str = "0:/users/los2w"
    guest_admin: bool = True


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
        return cfg

    def save(self, cfg: HostConfig) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(asdict(cfg), ensure_ascii=False, indent=2), encoding="utf-8")
