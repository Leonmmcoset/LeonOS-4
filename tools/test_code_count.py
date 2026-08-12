#!/usr/bin/env python3
"""Small host-side regression tests for tools/count_code.py."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "count_code.py"


def run(*arguments: str) -> dict:
    result = subprocess.run(
        [sys.executable, str(TOOL), *arguments, "--format", "json"],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(result.stdout)


def git(root: Path, *arguments: str) -> None:
    subprocess.run(["git", "-C", str(root), *arguments], check=True,
                   text=True, capture_output=True)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="leonos-code-count-") as temporary:
        root = Path(temporary)
        (root / "src").mkdir()
        (root / "build").mkdir()
        (root / "skip").mkdir()
        (root / "third_party" / "cmd").mkdir(parents=True)
        (root / "third_party" / "keep").mkdir(parents=True)
        (root / "src" / "main.c").write_text("int main(void) { return 0; }\n", encoding="utf-8")
        (root / "build" / "generated.c").write_text("int generated;\n", encoding="utf-8")
        (root / "skip" / "ignored.py").write_text("print(1)\n", encoding="utf-8")
        (root / "third_party" / "cmd" / "cmd.c").write_text("int cmd;\n", encoding="utf-8")
        (root / "third_party" / "keep" / "keep.c").write_text("int keep;\n", encoding="utf-8")

        summary = run(str(root), "--no-config", "--exclude", "third_party",
                      "--exclude-dir", "skip", "--jobs", "2",
                      "--shard-threshold", "1", "--shard-size", "1")
        assert summary["total"]["files"] == 1
        assert summary["parts"]["src"]["code"] == 1
        assert "build" not in summary["parts"]
        assert "skip" not in summary["parts"]

        config_path = root / ".codecount-config.json"
        config_path.write_text(json.dumps({
            "exclude": ["third_party/cmd"],
            "exclude_dirs": ["skip"],
            "exclude_files": [".codecount-config.json"],
            "exclude_languages": [],
            "include_languages": [],
        }), encoding="utf-8")
        configured = run(str(root), "--config", str(config_path), "--jobs", "1")
        assert configured["total"]["files"] == 2
        assert "third_party/cmd" not in configured["parts"]
        assert "third_party" in configured["parts"]
        assert configured["parts"]["third_party"]["files"] == 1

        history_root = root / "history"
        history_root.mkdir()
        git(history_root, "init", "-q")
        git(history_root, "config", "user.email", "test@example.invalid")
        git(history_root, "config", "user.name", "Code Count Test")
        (history_root / "src").mkdir()
        (history_root / "third_party" / "cmd").mkdir(parents=True)
        (history_root / "src" / "main.c").write_text("int main;\n", encoding="utf-8")
        (history_root / "third_party" / "cmd" / "cmd.c").write_text("int cmd;\n", encoding="utf-8")
        git(history_root, "add", ".")
        git(history_root, "commit", "-q", "-m", "initial")
        (history_root / "src" / "main.c").write_text("int main;\nint next;\n", encoding="utf-8")
        git(history_root, "add", ".")
        git(history_root, "commit", "-q", "-m", "grow")
        history = run(str(history_root), "--history", "--no-config",
                      "--exclude", "third_party/cmd", "--no-progress")
        assert history["method"] == "git-numstat"
        assert history["total_commits"] == 2
        assert history["commits"][0]["lines"] == 1
        assert history["commits"][1]["lines"] == 2
        assert history["final_lines"] == 2

    print("test_code_count: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
