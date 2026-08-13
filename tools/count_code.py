#!/usr/bin/env python3
"""Summarize LeonOS source code with cloc or scc.

The tool builds an explicit source-file list before invoking the selected
counter. This keeps build products and temporary files out of the report and
makes path-based exclusions predictable on both Windows and Linux/WSL.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import html
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


DEFAULT_CONFIG = Path(__file__).with_name("codecount.json")
SCC_LOCOMO_PRESETS = {
    "large": {"input_price_per_million": 10.0, "output_price_per_million": 30.0,
              "output_tokens_per_second": 30.0},
    "medium": {"input_price_per_million": 3.0, "output_price_per_million": 15.0,
               "output_tokens_per_second": 50.0},
    "small": {"input_price_per_million": 0.5, "output_price_per_million": 2.0,
              "output_tokens_per_second": 100.0},
    "local": {"input_price_per_million": 0.0, "output_price_per_million": 0.0,
              "output_tokens_per_second": 15.0},
}
SCC_COCOMO_COEFFICIENTS = {
    "organic": (2.4, 1.05, 2.5, 0.38),
    "semi-detached": (3.0, 1.12, 2.5, 0.35),
    "embedded": (3.6, 1.20, 2.5, 0.32),
}
DEFAULT_EXCLUDE_DIRS = {
    ".git",
    ".hg",
    ".svn",
    "*.egg-info",
    ".venv",
    "__pycache__",
    "build",
    "dist",
    "node_modules",
    "target",
    ".mypy_cache",
    ".pytest_cache",
    "coverage",
    "out",
    "tmp",
    "venv",
}
DEFAULT_EXCLUDE_PATHS = {
    "buildsystem/config",
    "buildsystem/deps",
    "buildsystem/logs",
    "buildsystem/state",
    "buildsystem/tmp",
}
DEFAULT_EXCLUDE_FILE_PATTERNS = {
    "*.a",
    "*.bin",
    "*.d",
    "*.dll",
    "*.elf",
    "*.exe",
    "*.fat",
    "*.img",
    "*.iso",
    "*.log",
    "*.ninja",
    "*.o",
    "*.obj",
    "*.so",
    "*.stamp",
    "*.tmp",
    "*.vmdk",
    "*.zip",
    "*.pyc",
}


def _as_list(value: Any, name: str) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ValueError(f"config field {name!r} must be an array of strings")
    return value


def load_config(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {}
    if not path.is_file():
        raise SystemExit(f"code-count config does not exist: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read code-count config {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise SystemExit(f"code-count config must contain a JSON object: {path}")
    for field in ("exclude", "exclude_dirs", "exclude_files", "exclude_languages", "include_languages"):
        _as_list(value.get(field), field)
    return value


def normalize_relative(path: str) -> str:
    normalized = path.replace("\\", "/")
    while normalized.startswith("./"):
        normalized = normalized[2:]
    return normalized.lstrip("/")


def path_matches(relative: str, pattern: str) -> bool:
    """Match a relative path and all descendants against a config pattern."""
    relative = normalize_relative(relative)
    pattern = normalize_relative(pattern).rstrip("/")
    if not pattern:
        return False
    if relative == pattern or relative.startswith(pattern + "/"):
        return True
    return fnmatch.fnmatchcase(relative, pattern)


def file_is_excluded(relative: str, config: dict[str, Any]) -> bool:
    path = normalize_relative(relative)
    name = PurePosixPath(path).name
    if any(path_matches(path, pattern) for pattern in config["exclude_paths"]):
        return True
    if any(fnmatch.fnmatchcase(name, pattern) or fnmatch.fnmatchcase(path, pattern)
           for pattern in config["exclude_files"]):
        return True
    return False


def collect_source_files(root: Path, config: dict[str, Any]) -> list[Path]:
    files: list[Path] = []
    for current, directories, names in os.walk(root):
        current_path = Path(current)
        relative_dir = normalize_relative(os.path.relpath(current_path, root))
        if relative_dir == ".":
            relative_dir = ""
        kept_directories: list[str] = []
        for directory in directories:
            candidate = normalize_relative(f"{relative_dir}/{directory}".strip("/"))
            if (directory in config["exclude_dirs"] or
                any(fnmatch.fnmatchcase(directory, pattern)
                    for pattern in config["exclude_dirs"]) or any(
                path_matches(candidate, pattern) for pattern in config["exclude_paths"]
            )):
                continue
            kept_directories.append(directory)
        directories[:] = kept_directories
        for name in names:
            relative = normalize_relative(f"{relative_dir}/{name}".strip("/"))
            if not file_is_excluded(relative, config):
                files.append(current_path / name)
    return sorted(files, key=lambda path: normalize_relative(os.path.relpath(path, root)))


def build_config(raw: dict[str, Any], args: argparse.Namespace) -> dict[str, Any]:
    exclude_dirs = set(DEFAULT_EXCLUDE_DIRS)
    exclude_paths = set(DEFAULT_EXCLUDE_PATHS)
    exclude_files = set(DEFAULT_EXCLUDE_FILE_PATTERNS)
    exclude_dirs.update(_as_list(raw.get("exclude_dirs"), "exclude_dirs"))
    exclude_paths.update(_as_list(raw.get("exclude"), "exclude"))
    exclude_files.update(_as_list(raw.get("exclude_files"), "exclude_files"))
    if args.no_default_excludes:
        exclude_dirs.clear()
        exclude_paths.clear()
        exclude_files.clear()
    exclude_dirs.update(args.exclude_dir)
    exclude_paths.update(args.exclude)
    exclude_files.update(args.exclude_file)
    return {
        "exclude_dirs": exclude_dirs,
        "exclude_paths": exclude_paths,
        "exclude_files": exclude_files,
        "exclude_languages": _as_list(raw.get("exclude_languages"), "exclude_languages"),
        "include_languages": _as_list(raw.get("include_languages"), "include_languages"),
    }


def run_cloc(cloc: str, root: Path, files: Iterable[Path], config: dict[str, Any]) -> dict[str, Any]:
    file_list = list(files)
    if not file_list:
        return {"header": {"n_files": 0}, "SUM": {"blank": 0, "comment": 0, "code": 0, "nFiles": 0}}
    list_file: Path | None = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="\n", delete=False,
                                         prefix="leonos-cloc-", suffix=".txt") as handle:
            list_file = Path(handle.name)
            for path in file_list:
                handle.write(normalize_relative(os.path.relpath(path, root)))
                handle.write("\n")
        # Count every source file, including generated-looking copies with
        # identical contents. This also makes independent parallel shards
        # mathematically equivalent to one serial cloc invocation.
        command = [cloc, "--quiet", "--skip-uniqueness", "--json", "--by-file",
                   f"--list-file={list_file}"]
        if config["exclude_languages"]:
            command.append("--exclude-lang=" + ",".join(config["exclude_languages"]))
        if config["include_languages"]:
            command.append("--include-lang=" + ",".join(config["include_languages"]))
        result = subprocess.run(command, cwd=root, check=False, text=True, capture_output=True)
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip()
            raise SystemExit(f"cloc failed with exit code {result.returncode}: {detail}")
        try:
            value = json.loads(result.stdout)
        except json.JSONDecodeError as exc:
            raise SystemExit(f"cloc returned invalid JSON: {exc}") from exc
        if not isinstance(value, dict):
            raise SystemExit("cloc returned a JSON value other than an object")
        return value
    finally:
        if list_file is not None:
            list_file.unlink(missing_ok=True)


def scc_file_batches(files: Iterable[Path], root: Path) -> list[list[str]]:
    """Split explicit scc inputs before Windows' command-line limit is reached."""
    batches: list[list[str]] = []
    current: list[str] = []
    current_length = 0
    for path in files:
        relative = normalize_relative(os.path.relpath(path, root))
        # Keep a large margin for executable and scc flag text. POSIX accepts
        # far longer command lines, while Windows limits CreateProcess to 32K.
        if current and current_length + len(relative) + 1 > 24_000:
            batches.append(current)
            current = []
            current_length = 0
        current.append(relative)
        current_length += len(relative) + 1
    if current:
        batches.append(current)
    return batches


def scc_language_allowed(language: str, config: dict[str, Any]) -> bool:
    """Apply the shared language filters to scc's language labels."""
    normalized = language.casefold()
    included = {item.casefold() for item in config["include_languages"]}
    excluded = {item.casefold() for item in config["exclude_languages"]}
    return (not included or normalized in included) and normalized not in excluded


def run_scc(scc: str, root: Path, files: Iterable[Path], config: dict[str, Any]) -> list[dict[str, Any]]:
    """Run scc for one bounded shard and return its per-file JSON rows."""
    file_list = list(files)
    if not file_list:
        return []
    rows: list[dict[str, Any]] = []
    for batch in scc_file_batches(file_list, root):
        command = [scc, "--format", "json", "--by-file", "--no-scc-ignore",
                   "--no-gitignore", "--no-ignore", "--no-gitmodule", *batch]
        result = subprocess.run(command, cwd=root, check=False, text=True,
                                encoding="utf-8", errors="replace", capture_output=True)
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip()
            raise SystemExit(f"scc failed with exit code {result.returncode}: {detail}")
        try:
            value = json.loads(result.stdout)
        except json.JSONDecodeError as exc:
            raise SystemExit(f"scc returned invalid JSON: {exc}") from exc
        if not isinstance(value, list):
            raise SystemExit("scc JSON output was not an array")
        for row in value:
            if isinstance(row, dict) and isinstance(row.get("Files"), list):
                rows.extend(file_row for file_row in row["Files"]
                            if isinstance(file_row, dict) and scc_language_allowed(
                                str(file_row.get("Language", "Unknown")), config))
    return rows


def execution_subgroup(path: Path, root: Path, report_group: str, depth: int) -> str:
    relative = normalize_relative(os.path.relpath(path, root))
    parts = PurePosixPath(relative).parts
    if report_group == "(root)" or len(parts) <= depth:
        return report_group
    return "/".join(parts[:depth + 1])


def partition_files(files: Iterable[Path], root: Path, depth: int,
                    shard_threshold: int, shard_size: int) -> dict[str, list[Path]]:
    report_groups: dict[str, list[Path]] = {}
    for path in files:
        relative = normalize_relative(os.path.relpath(path, root))
        group = group_for_path(relative, depth)
        report_groups.setdefault(group, []).append(path)

    # Keep the report grouping stable while splitting expensive cloc calls into
    # bounded batches. A large third_party tree and the userland tree no longer
    # monopolize one worker for the whole scan.
    shards: dict[str, list[Path]] = {}
    for group, group_files in sorted(report_groups.items()):
        if len(group_files) <= shard_threshold:
            shards[group] = group_files
            continue
        subgroups: dict[str, list[Path]] = {}
        for path in group_files:
            subgroup = execution_subgroup(path, root, group, depth)
            subgroups.setdefault(subgroup, []).append(path)
        for subgroup, subgroup_files in sorted(subgroups.items()):
            for offset in range(0, len(subgroup_files), shard_size):
                batch = subgroup_files[offset:offset + shard_size]
                suffix = "" if len(subgroup_files) <= shard_size else (
                    f" [{offset // shard_size + 1}/"
                    f"{(len(subgroup_files) + shard_size - 1) // shard_size}]"
                )
                shards[subgroup + suffix] = batch
    return shards


def merge_cloc_results(results: Iterable[dict[str, Any]]) -> dict[str, Any]:
    merged: dict[str, Any] = {"header": {"n_files": 0}}
    for result in results:
        header = result.get("header")
        if isinstance(header, dict):
            merged["header"]["n_files"] += int(header.get("n_files", 0))
        for key, value in result.items():
            if key not in ("header", "SUM"):
                merged[key] = value
    merged["SUM"] = {"nFiles": sum(
        1 for key, value in merged.items()
        if key not in ("header", "SUM") and isinstance(value, dict) and "language" in value
    )}
    return merged


def run_parallel_cloc(cloc: str, root: Path, groups: dict[str, list[Path]],
                      config: dict[str, Any], jobs: int, progress: bool) -> dict[str, Any]:
    total = len(groups)
    if total == 0:
        return merge_cloc_results(())
    completed = 0
    results: list[dict[str, Any]] = []

    def report(group: str) -> None:
        nonlocal completed
        completed += 1
        if progress:
            print(f"[code-count] cloc {completed}/{total}: {group}",
                  file=sys.stderr, flush=True)

    if jobs <= 1 or total == 1:
        for group, files in groups.items():
            result = run_cloc(cloc, root, files, config)
            results.append(result)
            report(group)
        return merge_cloc_results(results)

    with ThreadPoolExecutor(max_workers=min(jobs, total),
                            thread_name_prefix="code-count") as executor:
        pending = {
            executor.submit(run_cloc, cloc, root, files, config): group
            for group, files in groups.items()
        }
        for future in as_completed(pending):
            group = pending[future]
            results.append(future.result())
            report(group)
    return merge_cloc_results(results)


def run_parallel_scc(scc: str, root: Path, groups: dict[str, list[Path]],
                     config: dict[str, Any], jobs: int, progress: bool) -> list[dict[str, Any]]:
    """Run scc shards concurrently, preserving the same progress contract as cloc."""
    total = len(groups)
    if total == 0:
        return []
    completed = 0
    results: list[dict[str, Any]] = []

    def report(group: str) -> None:
        nonlocal completed
        completed += 1
        if progress:
            print(f"[code-count] scc {completed}/{total}: {group}",
                  file=sys.stderr, flush=True)

    if jobs <= 1 or total == 1:
        for group, files in groups.items():
            results.extend(run_scc(scc, root, files, config))
            report(group)
        return results

    with ThreadPoolExecutor(max_workers=min(jobs, total),
                            thread_name_prefix="code-count") as executor:
        pending = {
            executor.submit(run_scc, scc, root, files, config): group
            for group, files in groups.items()
        }
        for future in as_completed(pending):
            group = pending[future]
            results.extend(future.result())
            report(group)
    return results


def cloc_path(entry: str) -> str:
    return normalize_relative(entry)


def relative_cloc_path(entry: str, root: Path) -> str:
    path = cloc_path(entry)
    root_path = normalize_relative(str(root.resolve())).rstrip("/")
    if path == root_path:
        return ""
    if path.startswith(root_path + "/"):
        return path[len(root_path) + 1:]
    return path


def group_for_path(path: str, depth: int) -> str:
    parts = PurePosixPath(cloc_path(path)).parts
    if not parts:
        return "(root)"
    if len(parts) == 1:
        return "(root)"
    return "/".join(parts[:depth]) if depth > 0 else "(all)"


def summarize(raw: dict[str, Any], root: Path, depth: int) -> dict[str, Any]:
    parts: dict[str, dict[str, int]] = {}
    for key, value in raw.items():
        if key in ("header", "SUM") or not isinstance(value, dict) or "language" not in value:
            continue
        relative = relative_cloc_path(key, root)
        group = group_for_path(relative, depth)
        row = parts.setdefault(group, {"files": 0, "blank": 0, "comment": 0, "code": 0})
        row["files"] += 1
        for field in ("blank", "comment", "code"):
            row[field] += int(value.get(field, 0))
    total = {field: sum(row[field] for row in parts.values())
             for field in ("files", "blank", "comment", "code")}
    return {
        "root": str(root),
        "engine": "cloc",
        "parts": dict(sorted(parts.items())),
        "total": total,
        "languages": summarize_languages(raw),
    }


def summarize_languages(raw: dict[str, Any]) -> dict[str, dict[str, int]]:
    languages: dict[str, dict[str, int]] = {}
    for key, value in raw.items():
        if key in ("header", "SUM") or not isinstance(value, dict) or "language" not in value:
            continue
        language = str(value["language"])
        row = languages.setdefault(language, {"files": 0, "blank": 0, "comment": 0, "code": 0})
        row["files"] += 1
        for field in ("blank", "comment", "code"):
            row[field] += int(value.get(field, 0))
    return dict(sorted(languages.items()))


def summarize_scc(rows: Iterable[dict[str, Any]], root: Path, depth: int,
                  *, cocomo_type: str, avg_wage: int, overhead: float, eaf: float,
                  locomo_preset: str) -> dict[str, Any]:
    """Normalize scc's per-file JSON into the report schema used by cloc."""
    parts: dict[str, dict[str, int]] = {}
    languages: dict[str, dict[str, int]] = {}
    for value in rows:
        location = value.get("Location") or value.get("Filename")
        if not isinstance(location, str):
            continue
        relative = normalize_relative(location)
        group = group_for_path(relative, depth)
        code = int(value.get("Code", 0) or 0)
        row = parts.setdefault(group, {"files": 0, "blank": 0, "comment": 0,
                                       "code": 0, "complexity": 0})
        row["files"] += 1
        row["blank"] += int(value.get("Blank", 0) or 0)
        row["comment"] += int(value.get("Comment", 0) or 0)
        row["code"] += code
        row["complexity"] += int(value.get("Complexity", 0) or 0)
        language = str(value.get("Language", "Unknown"))
        lang = languages.setdefault(language, {"files": 0, "blank": 0,
                                                "comment": 0, "code": 0,
                                                "complexity": 0})
        for field in ("files",):
            lang[field] += 1
        lang["blank"] += int(value.get("Blank", 0) or 0)
        lang["comment"] += int(value.get("Comment", 0) or 0)
        lang["code"] += code
        lang["complexity"] += int(value.get("Complexity", 0) or 0)
    total = {field: sum(row[field] for row in parts.values())
             for field in ("files", "blank", "comment", "code", "complexity")}
    metrics = calculate_scc_metrics(total["code"], total["complexity"], cocomo_type,
                                    avg_wage, overhead, eaf, locomo_preset)
    return {"root": str(root), "engine": "scc", "parts": dict(sorted(parts.items())),
            "total": total, "languages": dict(sorted(languages.items())),
            "metrics": metrics}


def calculate_scc_metrics(code: int, complexity: int, cocomo_type: str,
                          avg_wage: int, overhead: float, eaf: float,
                          locomo_preset: str) -> dict[str, Any]:
    coefficients = SCC_COCOMO_COEFFICIENTS.get(cocomo_type,
                                                SCC_COCOMO_COEFFICIENTS["organic"])
    a, b, c, d = coefficients
    effort = a * math.pow(code / 1000, b) * eaf if code else 0.0
    schedule = c * math.pow(effort, d) if effort else 0.0
    people = effort / schedule if schedule else 0.0
    cost = effort * avg_wage / 12.0 * overhead

    preset_name = locomo_preset.lower()
    preset = SCC_LOCOMO_PRESETS.get(preset_name, SCC_LOCOMO_PRESETS["medium"])
    density = complexity / code if code else 0.0
    complexity_factor = 1.0 + math.sqrt(density) * 5.0
    iterations = 1.5 + math.sqrt(density) * 2.0
    output_tokens = code * 10.0 * iterations
    input_tokens = code * 20.0 * complexity_factor * iterations
    locomo_cost = (input_tokens / 1_000_000.0) * preset["input_price_per_million"]
    locomo_cost += (output_tokens / 1_000_000.0) * preset["output_price_per_million"]
    generation_seconds = output_tokens / preset["output_tokens_per_second"] if preset["output_tokens_per_second"] else 0.0
    return {
        "cocomo": {
            "project_type": cocomo_type if cocomo_type in SCC_COCOMO_COEFFICIENTS else "organic",
            "effort_person_months": effort, "schedule_months": schedule,
            "people_required": people, "estimated_cost": cost,
            "average_wage": avg_wage, "overhead": overhead, "eaf": eaf,
        },
        "locomo": {
            "preset": preset_name if preset_name in SCC_LOCOMO_PRESETS else "medium",
            "input_tokens": input_tokens, "output_tokens": output_tokens,
            "estimated_cost": locomo_cost, "generation_seconds": generation_seconds,
            "review_hours": code * 0.01 / 60.0, "iteration_factor": iterations,
            "complexity_multiplier": complexity_factor * iterations,
        },
    }


def format_number(value: int) -> str:
    return f"{value:,}"


def format_text(summary: dict[str, Any], *, show_languages: bool) -> str:
    lines = [f"Code statistics: {summary['root']} ({summary.get('engine', 'cloc')})", "",
             f"{'Part':<32} {'Files':>8} {'Blank':>12} {'Comment':>12} {'Code':>12}",
             "-" * 80]
    for part, row in summary["parts"].items():
        lines.append(f"{part:<32} {format_number(row['files']):>8} "
                     f"{format_number(row['blank']):>12} {format_number(row['comment']):>12} "
                     f"{format_number(row['code']):>12}")
    total = summary["total"]
    lines.extend(["-" * 80,
                  f"{'TOTAL':<32} {format_number(total['files']):>8} "
                  f"{format_number(total['blank']):>12} {format_number(total['comment']):>12} "
                  f"{format_number(total['code']):>12}"])
    if show_languages:
        lines.extend(["", "Languages:", f"{'Language':<24} {'Files':>8} {'Code':>12}", "-" * 48])
        for language, row in summary["languages"].items():
            lines.append(f"{language:<24} {format_number(row['files']):>8} {format_number(row['code']):>12}")
    if summary.get("engine") == "scc":
        cocomo = summary["metrics"]["cocomo"]
        locomo = summary["metrics"]["locomo"]
        lines.extend(["", "scc estimates (rough estimates):",
                      f"COCOMO ({cocomo['project_type']}): {cocomo['effort_person_months']:.2f} person-months, "
                      f"{cocomo['schedule_months']:.2f} months, ${cocomo['estimated_cost']:,.0f}, "
                      f"{cocomo['people_required']:.2f} developers",
                      f"LOCOMO ({locomo['preset']}): {locomo['input_tokens'] / 1_000_000:.2f}M input / "
                      f"{locomo['output_tokens'] / 1_000_000:.2f}M output tokens, ${locomo['estimated_cost']:,.2f}, "
                      f"{locomo['generation_seconds'] / 60:.1f} minutes generation, "
                      f"{locomo['review_hours']:.1f} review hours"])
    return "\n".join(lines)


def find_counter(requested: str | None, engine: str) -> str:
    if requested:
        return requested
    found = shutil.which(engine)
    if not found:
        raise SystemExit(f"{engine} was not found in PATH; install {engine} or pass --{engine} PATH")
    return found


def run_git_history(root: Path, config: dict[str, Any], *, all_branches: bool,
                    progress: bool) -> dict[str, Any]:
    """Build a fast line-growth history from Git's per-commit numstat data.

    This intentionally performs one Git invocation and never checks out a
    commit or invokes cloc repeatedly.  The resulting metric is physical text
    lines changed in paths accepted by the configured exclusions.
    """
    command = ["git", "-C", str(root), "-c", "core.quotepath=false", "log",
               "--reverse", "--numstat", "--no-renames",
               "--format=commit:%H\t%cI\t%s"]
    if all_branches:
        command.extend(["--all", "--no-merges"])
    else:
        command.append("--first-parent")
    try:
        result = subprocess.run(command, check=False, text=True,
                                encoding="utf-8", errors="replace",
                                capture_output=True)
    except OSError as exc:
        raise SystemExit(f"cannot run git: {exc}") from exc
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise SystemExit(f"git history scan failed with exit code {result.returncode}: {detail}")

    points: list[dict[str, Any]] = []
    current_lines = 0
    current: dict[str, Any] | None = None
    commits_seen = 0
    for raw_line in result.stdout.splitlines():
        line = raw_line.rstrip("\r")
        if line.startswith("commit:"):
            if current is not None:
                current["lines"] = current_lines
                points.append(current)
            fields = line[7:].split("\t", 2)
            if len(fields) != 3:
                continue
            current = {"hash": fields[0], "date": fields[1], "subject": fields[2],
                       "added": 0, "deleted": 0, "lines": current_lines}
            commits_seen += 1
            continue
        if current is None or not line or line.startswith(" "):
            continue
        fields = line.split("\t", 2)
        if len(fields) != 3:
            continue
        added_text, deleted_text, path = fields
        # Git reports binary changes as '-' and does not provide line counts.
        if added_text == "-" or deleted_text == "-":
            continue
        try:
            added = int(added_text)
            deleted = int(deleted_text)
        except ValueError:
            continue
        path = normalize_relative(path)
        if file_is_excluded(path, config):
            continue
        current["added"] += added
        current["deleted"] += deleted
        current_lines += added - deleted
    if current is not None:
        current["lines"] = current_lines
        points.append(current)
    if progress:
        print(f"[code-count] git history: {commits_seen:,} commits, {len(points):,} points",
              file=sys.stderr, flush=True)
    return {"root": str(root), "metric": "physical_lines",
            "method": "git-numstat", "first_parent": not all_branches,
            "commits": points, "total_commits": len(points),
            "final_lines": current_lines}


def format_history_text(history: dict[str, Any]) -> str:
    lines = [f"Code history: {history['root']}",
             "Metric: physical lines (Git numstat; configured exclusions applied)", "",
             f"{'Date':<25} {'Commit':<10} {'Added':>10} {'Deleted':>10} {'Lines':>12}",
             "-" * 73]
    for point in history["commits"]:
        date = str(point["date"])[:25]
        lines.append(f"{date:<25} {point['hash'][:8]:<10} {format_number(point['added']):>10} "
                     f"{format_number(point['deleted']):>10} {format_number(point['lines']):>12}")
    lines.extend(["-" * 73, f"Final lines: {format_number(history['final_lines'])}"])
    return "\n".join(lines)


def write_history_chart(history: dict[str, Any], output: Path) -> None:
    """Write a dependency-free SVG line chart for the history points."""
    points = history["commits"]
    width, height = 1200, 620
    left, right, top, bottom = 82, 28, 42, 76
    plot_width, plot_height = width - left - right, height - top - bottom
    values = [int(point["lines"]) for point in points] or [0]
    minimum, maximum = min(0, min(values)), max(1, max(values))
    if maximum == minimum:
        maximum += 1

    def x_at(index: int) -> float:
        return left if len(values) == 1 else left + index * plot_width / (len(values) - 1)

    def y_at(value: int) -> float:
        return top + (maximum - value) * plot_height / (maximum - minimum)

    svg: list[str] = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
                      f'viewBox="0 0 {width} {height}">',
                      '<rect width="100%" height="100%" fill="#ffffff"/>',
                      '<style>text{font-family:Arial,sans-serif;fill:#263238} .grid{stroke:#d9e1e5;stroke-width:1} '
                      '.axis{stroke:#607d8b;stroke-width:1.5} .line{fill:none;stroke:#1565c0;stroke-width:2.5}</style>',
                      '<text x="82" y="25" font-size="18" font-weight="bold">LeonOS code growth</text>']
    for step in range(6):
        value = minimum + (maximum - minimum) * step / 5
        y = y_at(round(value))
        svg.append(f'<line class="grid" x1="{left}" y1="{y:.1f}" x2="{width-right}" y2="{y:.1f}"/>')
        svg.append(f'<text x="{left-10}" y="{y+4:.1f}" text-anchor="end" font-size="12">{format_number(round(value))}</text>')
    svg.extend([f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}"/>',
                f'<line class="axis" x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}"/>'])
    coords = " ".join(f"{x_at(i):.1f},{y_at(value):.1f}" for i, value in enumerate(values))
    svg.append(f'<polyline class="line" points="{coords}"/>')
    if points:
        label_count = min(8, len(points))
        for index in sorted({round(i * (len(points)-1) / max(1, label_count-1)) for i in range(label_count)}):
            point = points[index]
            x = x_at(index)
            label = html.escape(str(point["date"])[:10])
            svg.append(f'<text x="{x:.1f}" y="{height-bottom+24}" text-anchor="middle" font-size="11">{label}</text>')
    svg.extend([f'<text x="{width/2:.0f}" y="{height-14}" text-anchor="middle" font-size="12">Commit history (first-parent)</text>',
                '</svg>'])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(svg) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Count and summarize project source code with cloc or scc.")
    parser.add_argument("root", nargs="?", type=Path, default=Path("."), help="project root (default: current directory)")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG,
                        help="JSON exclusion config (default: tools/codecount.json)")
    parser.add_argument("--no-config", action="store_true", help="do not load the default config")
    parser.add_argument("--no-default-excludes", action="store_true", help="also scan standard build/temp directories")
    parser.add_argument("--exclude", action="append", default=[], metavar="PATH/GLOB",
                        help="exclude a relative path or glob; repeatable")
    parser.add_argument("--exclude-dir", action="append", default=[], metavar="NAME",
                        help="exclude directories by name; repeatable")
    parser.add_argument("--exclude-file", action="append", default=[], metavar="GLOB",
                        help="exclude files by name/path glob; repeatable")
    parser.add_argument("--group-depth", type=int, default=1, metavar="N",
                        help="number of path components used for part summaries (default: 1)")
    parser.add_argument("--engine", choices=("cloc", "scc"), default="cloc",
                        help="code counter to use (default: cloc)")
    parser.add_argument("--cloc", help="cloc executable path")
    parser.add_argument("--scc", help="scc executable path")
    parser.add_argument("--cocomo-project-type", choices=tuple(SCC_COCOMO_COEFFICIENTS),
                        default="organic", help="scc COCOMO model (only used with --engine scc)")
    parser.add_argument("--avg-wage", type=int, default=56286,
                        help="scc COCOMO average annual wage (default: 56286)")
    parser.add_argument("--overhead", type=float, default=2.4,
                        help="scc COCOMO overhead multiplier (default: 2.4)")
    parser.add_argument("--eaf", type=float, default=1.0,
                        help="scc COCOMO effort adjustment factor (default: 1.0)")
    parser.add_argument("--locomo-preset", choices=tuple(SCC_LOCOMO_PRESETS), default="medium",
                        help="scc LOCOMO pricing/throughput preset (default: medium)")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--output", type=Path, help="write output to a file instead of stdout")
    parser.add_argument("--languages", action="store_true", help="include a language summary in text output")
    parser.add_argument("--jobs", type=int, default=0, metavar="N",
                        help="parallel cloc workers (default: up to 4; 1 disables parallelism)")
    parser.add_argument("--shard-threshold", type=int, default=192, metavar="N",
                        help="split a report part by its child directories above N files (default: 192)")
    parser.add_argument("--shard-size", type=int, default=512, metavar="N",
                        help="maximum files per cloc batch after directory splitting (default: 512)")
    parser.add_argument("--no-progress", action="store_true",
                        help="do not print scan progress to stderr")
    parser.add_argument("--history", action="store_true",
                        help="scan Git history and report cumulative physical-line growth")
    parser.add_argument("--history-chart", type=Path, metavar="SVG",
                        help="write the --history growth chart as a dependency-free SVG")
    parser.add_argument("--history-all-branches", action="store_true",
                        help="include non-merge commits from all refs (default: first-parent)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.group_depth < 0:
        raise SystemExit("--group-depth must be zero or greater")
    if args.jobs < 0:
        raise SystemExit("--jobs must be zero or greater")
    if args.shard_threshold < 1 or args.shard_size < 1:
        raise SystemExit("--shard-threshold and --shard-size must be positive")
    if args.avg_wage < 0:
        raise SystemExit("--avg-wage must not be negative")
    if args.overhead < 0 or args.eaf <= 0:
        raise SystemExit("--overhead must not be negative and --eaf must be positive")
    root = args.root.resolve()
    if not root.is_dir():
        raise SystemExit(f"project root is not a directory: {root}")
    config_path = None if args.no_config else args.config
    raw_config = load_config(config_path)
    config = build_config(raw_config, args)
    if args.history:
        history = run_git_history(root, config,
                                  all_branches=args.history_all_branches,
                                  progress=not args.no_progress)
        output = json.dumps(history, ensure_ascii=False, indent=2) if args.format == "json" else format_history_text(history)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(output + "\n", encoding="utf-8")
        else:
            print(output)
        if args.history_chart:
            write_history_chart(history, args.history_chart)
            if not args.no_progress:
                print(f"[code-count] wrote history chart: {args.history_chart}",
                      file=sys.stderr, flush=True)
        return 0
    files = collect_source_files(root, config)
    if not args.no_progress:
        print(f"[code-count] scanned {len(files):,} files; preparing cloc parts",
              file=sys.stderr, flush=True)
    groups = partition_files(files, root, args.group_depth,
                             args.shard_threshold, args.shard_size)
    jobs = args.jobs or min(4, max(1, os.cpu_count() or 1))
    if args.engine == "cloc":
        raw = run_parallel_cloc(find_counter(args.cloc, "cloc"), root, groups, config, jobs,
                                not args.no_progress)
        summary = summarize(raw, root, args.group_depth)
    else:
        rows = run_parallel_scc(find_counter(args.scc, "scc"), root, groups, config, jobs,
                                not args.no_progress)
        summary = summarize_scc(rows, root, args.group_depth,
                                cocomo_type=args.cocomo_project_type,
                                avg_wage=args.avg_wage, overhead=args.overhead,
                                eaf=args.eaf, locomo_preset=args.locomo_preset)
    output = json.dumps(summary, ensure_ascii=False, indent=2) if args.format == "json" else format_text(summary, show_languages=args.languages)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output + "\n", encoding="utf-8")
    else:
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
