from __future__ import annotations

import json
import os
import re
import secrets
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
TARGET_INDEX_VERSION = 1


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def strip_ansi(value: str) -> str:
    return ANSI_RE.sub("", value)


def atomic_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", newline="\n", dir=path.parent, delete=False
    ) as handle:
        json.dump(value, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")
        temporary = Path(handle.name)
    os.replace(temporary, path)


def read_json(path: Path, default: dict[str, Any] | None = None) -> dict[str, Any]:
    if not path.exists():
        return {} if default is None else default
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {} if default is None else default
    return value if isinstance(value, dict) else ({} if default is None else default)


@dataclass(frozen=True, slots=True)
class BuildPaths:
    root: Path

    @property
    def home(self) -> Path:
        return self.root / "buildsystem"

    @property
    def out(self) -> Path:
        return self.root / "build"

    @property
    def legacy_out(self) -> Path:
        return self.home / "out"

    @property
    def objects(self) -> Path:
        return self.out / "obj"

    @property
    def generated_include(self) -> Path:
        return self.out / "include" / "generated"

    @property
    def staging(self) -> Path:
        return self.out / "esp"

    @property
    def images(self) -> Path:
        return self.out / "images"

    @property
    def state(self) -> Path:
        return self.home / "state"

    @property
    def target_state(self) -> Path:
        return self.state / "targets"

    @property
    def target_index(self) -> Path:
        return self.state / "targets.json"

    @property
    def tasks(self) -> Path:
        return self.state / "tasks"

    @property
    def logs(self) -> Path:
        return self.home / "logs"

    @property
    def tmp(self) -> Path:
        return self.home / "tmp"

    @property
    def config(self) -> Path:
        return self.home / "config"

    @property
    def settings(self) -> Path:
        return self.config / "settings.toml"

    @property
    def kconfig(self) -> Path:
        return self.config / "leonos.conf"

    @property
    def deps(self) -> Path:
        return self.home / "deps"

    def ensure(self) -> None:
        for path in (
            self.out,
            self.objects,
            self.generated_include,
            self.staging,
            self.images,
            self.target_state,
            self.tasks,
            self.logs,
            self.tmp,
            self.config,
            self.deps,
        ):
            path.mkdir(parents=True, exist_ok=True)


class TaskStore:
    def __init__(self, paths: BuildPaths) -> None:
        self.paths = paths
        self._target_states: dict[str, dict[str, Any]] | None = None
        self._target_states_dirty = False

    def task_path(self, task_id: str) -> Path:
        return self.paths.tasks / f"{task_id}.json"

    def log_path(self, task_id: str) -> Path:
        return self.paths.logs / f"{task_id}.log"

    def new_id(self, command: list[str]) -> str:
        self.paths.ensure()
        for _ in range(256):
            task_id = str(secrets.randbelow(900_000_000) + 100_000_000)
            path = self.task_path(task_id)
            try:
                with path.open("x", encoding="utf-8", newline="\n") as handle:
                    json.dump(
                        {
                            "id": task_id,
                            "command": command,
                            "status": "created",
                            "created_at": utc_now(),
                            "log": str(self.log_path(task_id).relative_to(self.paths.root)),
                        },
                        handle,
                        ensure_ascii=False,
                        indent=2,
                    )
                    handle.write("\n")
                return task_id
            except FileExistsError:
                continue
        raise RuntimeError("unable to allocate a unique nine-digit task ID")

    def read(self, task_id: str) -> dict[str, Any]:
        if not re.fullmatch(r"\d{9}", task_id):
            raise ValueError("task ID must contain exactly nine digits")
        path = self.task_path(task_id)
        if not path.exists():
            raise FileNotFoundError(f"unknown task ID: {task_id}")
        return read_json(path)

    def update(self, task_id: str, **changes: Any) -> dict[str, Any]:
        record = self.read(task_id)
        record.update(changes)
        atomic_json(self.task_path(task_id), record)
        return record

    def _load_target_states(self) -> dict[str, dict[str, Any]]:
        if self._target_states is not None:
            return self._target_states
        indexed = read_json(self.paths.target_index)
        raw_states = indexed.get("targets")
        if isinstance(raw_states, dict):
            self._target_states = {
                name: value for name, value in raw_states.items()
                if isinstance(name, str) and isinstance(value, dict)
            }
            return self._target_states
        migrated: dict[str, dict[str, Any]] = {}
        if self.paths.target_state.exists():
            for path in self.paths.target_state.glob("*.json"):
                value = read_json(path)
                name = value.get("target")
                if isinstance(name, str):
                    migrated[name] = value
        self._target_states = migrated
        self._target_states_dirty = bool(migrated)
        return migrated

    def read_target(self, target_name: str) -> dict[str, Any]:
        return self._load_target_states().get(target_name, {})

    def write_target(self, target_name: str, state: dict[str, Any]) -> None:
        self._load_target_states()[target_name] = state
        self._target_states_dirty = True

    def target_states(self) -> dict[str, dict[str, Any]]:
        return dict(self._load_target_states())

    def flush_target_states(self) -> None:
        if not self._target_states_dirty:
            return
        atomic_json(
            self.paths.target_index,
            {"version": TARGET_INDEX_VERSION, "targets": self._load_target_states()},
        )
        self._target_states_dirty = False

    def clear_target_states(self) -> None:
        self._target_states = {}
        self._target_states_dirty = False
        self.paths.target_index.unlink(missing_ok=True)
        if self.paths.target_state.exists():
            for path in self.paths.target_state.glob("*.json"):
                path.unlink(missing_ok=True)

    def prune_target_states(self) -> dict[str, int]:
        states = self._load_target_states()
        stale: list[str] = []
        for name, state in states.items():
            outputs = state.get("outputs")
            if not isinstance(outputs, list) or not outputs:
                stale.append(name)
                continue
            for raw in outputs:
                if not isinstance(raw, str):
                    stale.append(name)
                    break
                output = Path(raw)
                if not output.is_absolute():
                    output = self.paths.root / output
                if not output.exists():
                    stale.append(name)
                    break
        for name in stale:
            states.pop(name, None)
        if stale:
            self._target_states_dirty = True
            self.flush_target_states()
        legacy_count = 0
        if self.paths.target_state.exists():
            for path in self.paths.target_state.glob("*.json"):
                path.unlink(missing_ok=True)
                legacy_count += 1
        return {"removed_target_states": len(stale), "removed_legacy_state_files": legacy_count}
