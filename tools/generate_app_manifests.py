#!/usr/bin/env python3
"""Generate runtime application manifests from the component manifest."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from buildsystem.components import load_components


def read_terminal(source: Path) -> int:
    if not source.is_file():
        return 0
    for line in source.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if key.strip() == "terminal" and separator:
            return 1 if value.strip().lower() in {"1", "true", "yes"} else 0
    return 0


def app_path(app: str, system_apps: set[str]) -> tuple[str, str]:
    root = "system/apps" if app in system_apps else "programs"
    return root, f"{root}/{app}"


def generate(root: Path, components_path: Path, out_dir: Path,
             apps: list[str], system_apps: set[str]) -> list[Path]:
    components = {component.id: component for component in load_components(components_path)}
    if out_dir.exists():
        shutil.rmtree(out_dir)
    outputs: list[Path] = []
    for app in apps:
        component = components.get(app)
        if component is None:
            raise ValueError(f"unknown application component: {app}")
        root_name, relative_dir = app_path(app, system_apps)
        destination = out_dir / relative_dir / "manifest.ini"
        destination.parent.mkdir(parents=True, exist_ok=True)
        source = root / "userland/apps" / app / f"{app}.app.ini"
        if not source.is_file():
            source = root / "userland" / app / f"{app}.app.ini"
        terminal = read_terminal(source)
        icon = f"{app}.bmp" if component.entry else ""
        text = "\n".join([
            "[app]",
            f"id={component.id}",
            f"name={component.label}",
            "version=system",
            f"category={component.category}",
            f"exec={app}.elf",
            f"icon={icon}",
            f"entry={1 if component.entry else 0}",
            f"terminal={terminal}",
            f"system={1 if root_name == 'system/apps' else 0}",
            "hidden=0",
            f"open_with={1 if component.open_with else 0}",
            f"extensions={','.join(component.extensions)}",
            f"commands={component.id}",
            "",
        ])
        destination.write_text(text, encoding="utf-8", newline="\n")
        outputs.append(destination)
    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--components", default="configs/components.toml", type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--apps", nargs="+", required=True)
    parser.add_argument("--system-apps", nargs="*", default=[])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    generate(root, root / args.components, root / args.out_dir,
             args.apps, set(args.system_apps))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
