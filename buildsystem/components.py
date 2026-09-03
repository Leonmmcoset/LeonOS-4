"""Shared component-manifest and Kconfig selection support."""

from __future__ import annotations

import json
import os
import re
import tomllib
from dataclasses import asdict, dataclass
from pathlib import Path


ID_RE = re.compile(r"^[a-z][a-z0-9_-]*$")
SYMBOL_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
STAGE_PATH_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._/-]*$")
VALID_KINDS = frozenset({
    "system-app", "program-app", "package-app", "tool", "library",
})
SOURCE_SUFFIXES = frozenset({".c", ".S", ".cpp"})


class ComponentError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class Component:
    id: str
    symbol: str
    label: str
    category: str
    kind: str
    default: bool
    required: bool
    stage: bool
    entry: bool
    sdk: bool
    api: bool
    api_stage_path: str = ""
    depends: tuple[str, ...] = ()
    api_requires: tuple[str, ...] = ()
    open_with: bool = False
    extensions: tuple[str, ...] = ()

    @property
    def build_symbol(self) -> str:
        return f"CONFIG_LEON_COMPONENT_{self.symbol}_BUILD"

    def option_symbol(self, name: str) -> str:
        return f"CONFIG_LEON_COMPONENT_{self.symbol}_{name}"


def _boolean(raw: dict[str, object], component_id: str, field: str) -> bool:
    value = raw.get(field)
    if not isinstance(value, bool):
        raise ComponentError(f"component {component_id} needs boolean {field}")
    return value


def _stage_path(raw: dict[str, object], component_id: str) -> str:
    value = raw.get("api_stage_path", "")
    if not isinstance(value, str) or (value and (
        not STAGE_PATH_RE.fullmatch(value) or value.startswith("/") or ".." in value.split("/")
    )):
        raise ComponentError(f"component {component_id} has invalid api_stage_path")
    return value


def _extensions(raw: dict[str, object], component_id: str) -> tuple[str, ...]:
    value = raw.get("extensions", [])
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ComponentError(f"component {component_id} has invalid extensions")
    result: list[str] = []
    for extension in value:
        if not re.fullmatch(r"\.[A-Za-z0-9][A-Za-z0-9._-]*", extension):
            raise ComponentError(f"component {component_id} has invalid extension {extension!r}")
        result.append(extension.lower())
    return tuple(result)


def load_components(path: Path) -> tuple[Component, ...]:
    try:
        document = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ComponentError(f"cannot read component manifest {path}: {exc}") from exc
    if document.get("version") != 1:
        raise ComponentError("component manifest version must be 1")
    raw_components = document.get("components")
    if not isinstance(raw_components, list) or not raw_components:
        raise ComponentError("component manifest must contain [[components]] entries")

    components: list[Component] = []
    ids: set[str] = set()
    symbols: set[str] = set()
    for index, raw in enumerate(raw_components, 1):
        if not isinstance(raw, dict):
            raise ComponentError(f"component {index} is not a table")
        component_id = raw.get("id")
        symbol = raw.get("symbol")
        if not isinstance(component_id, str) or not ID_RE.fullmatch(component_id):
            raise ComponentError(f"component {index} has invalid id: {component_id!r}")
        if not isinstance(symbol, str) or not SYMBOL_RE.fullmatch(symbol):
            raise ComponentError(f"component {component_id} has invalid symbol: {symbol!r}")
        if component_id in ids:
            raise ComponentError(f"duplicate component id: {component_id}")
        if symbol in symbols:
            raise ComponentError(f"duplicate component symbol: {symbol}")
        ids.add(component_id)
        symbols.add(symbol)
        text: dict[str, str] = {}
        for field in ("label", "category", "kind"):
            value = raw.get(field)
            if not isinstance(value, str) or not value:
                raise ComponentError(f"component {component_id} needs a non-empty {field}")
            text[field] = value
        if text["kind"] not in VALID_KINDS:
            raise ComponentError(
                f"component {component_id} has unsupported kind {text['kind']!r}; "
                f"expected one of {', '.join(sorted(VALID_KINDS))}"
            )
        depends = raw.get("depends", [])
        if not isinstance(depends, list) or not all(isinstance(item, str) for item in depends):
            raise ComponentError(f"component {component_id} has invalid depends")
        if component_id in depends:
            raise ComponentError(f"component {component_id} cannot depend on itself")
        api_requires = raw.get("api_requires", [])
        if not isinstance(api_requires, list) or not all(isinstance(item, str) for item in api_requires):
            raise ComponentError(f"component {component_id} has invalid api_requires")
        if component_id in api_requires:
            raise ComponentError(f"component {component_id} cannot require itself for an API package")
        open_with = raw.get("open_with", False)
        if not isinstance(open_with, bool):
            raise ComponentError(f"component {component_id} has invalid open_with")
        components.append(Component(
            id=component_id,
            symbol=symbol,
            label=text["label"],
            category=text["category"],
            kind=text["kind"],
            default=_boolean(raw, component_id, "default"),
            required=_boolean(raw, component_id, "required"),
            stage=_boolean(raw, component_id, "stage"),
            entry=_boolean(raw, component_id, "entry"),
            sdk=_boolean(raw, component_id, "sdk"),
            api=_boolean(raw, component_id, "api"),
            api_stage_path=_stage_path(raw, component_id),
            depends=tuple(depends),
            api_requires=tuple(api_requires),
            open_with=open_with,
            extensions=_extensions(raw, component_id),
        ))

    by_id = {component.id: component for component in components}
    for component in components:
        for dependency in component.depends:
            if dependency not in by_id:
                raise ComponentError(
                    f"component {component.id} depends on unknown {dependency}"
                )
        for dependency in component.api_requires:
            if dependency not in by_id:
                raise ComponentError(
                    f"component {component.id} API requires unknown {dependency}"
                )

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(component_id: str) -> None:
        if component_id in visiting:
            raise ComponentError(f"component dependency cycle includes {component_id}")
        if component_id in visited:
            return
        visiting.add(component_id)
        for dependency in by_id[component_id].depends:
            visit(dependency)
        visiting.remove(component_id)
        visited.add(component_id)

    for component in components:
        visit(component.id)

    visiting.clear()
    visited.clear()

    def visit_api(component_id: str) -> None:
        if component_id in visiting:
            raise ComponentError(f"component API dependency cycle includes {component_id}")
        if component_id in visited:
            return
        visiting.add(component_id)
        for dependency in by_id[component_id].api_requires:
            visit_api(dependency)
        visiting.remove(component_id)
        visited.add(component_id)

    for component in components:
        visit_api(component.id)
    return tuple(components)


def component_defaults(components: tuple[Component, ...]) -> dict[str, str]:
    values: dict[str, str] = {}
    for component in components:
        enabled = component.required or component.default
        values[component.build_symbol] = "y" if enabled else "n"
        values[component.option_symbol("IMAGE")] = "y" if enabled and component.stage else "n"
        values[component.option_symbol("ENTRY")] = "y" if enabled and component.entry else "n"
        values[component.option_symbol("SDK")] = "y" if enabled and component.sdk else "n"
        values[component.option_symbol("API")] = "y" if enabled and component.api else "n"
    return values


def _enabled(values: dict[str, str], symbol: str, default: bool) -> bool:
    return values.get(symbol, "y" if default else "n") == "y"


def resolve_components(
    components: tuple[Component, ...], values: dict[str, str]
) -> dict[str, dict[str, object]]:
    by_id = {component.id: component for component in components}
    enabled: dict[str, bool] = {
        component.id: component.required or _enabled(
            values, component.build_symbol, component.default
        )
        for component in components
    }

    def enable_dependencies(component_id: str) -> None:
        for dependency in by_id[component_id].depends:
            if not enabled[dependency]:
                enabled[dependency] = True
            enable_dependencies(dependency)

    for component in components:
        if enabled[component.id]:
            enable_dependencies(component.id)

    api_enabled: dict[str, bool] = {
        component.id: component.api and enabled[component.id] and (
            component.api if component.required else _enabled(
                values, component.option_symbol("API"), component.api
            )
        )
        for component in components
    }

    def enable_api_requirements(component_id: str) -> None:
        for dependency in by_id[component_id].api_requires:
            if not enabled[dependency]:
                enabled[dependency] = True
                enable_dependencies(dependency)
            if not api_enabled[dependency]:
                api_enabled[dependency] = True
            enable_api_requirements(dependency)

    for component in components:
        if api_enabled[component.id]:
            enable_api_requirements(component.id)

    selection: dict[str, dict[str, object]] = {}
    for component in components:
        build = enabled[component.id]
        # Required components have no generated Kconfig controls. Their
        # manifest defaults are authoritative even when an old profile still
        # contains a stale generated symbol from an earlier revision.
        image = build and (component.stage if component.required else _enabled(
            values, component.option_symbol("IMAGE"), component.stage
        ))
        entry = image and (component.entry if component.required else _enabled(
            values, component.option_symbol("ENTRY"), component.entry
        ))
        sdk = build and (component.sdk if component.required else _enabled(
            values, component.option_symbol("SDK"), component.sdk
        ))
        api = build and api_enabled[component.id]
        selection[component.id] = {
            "id": component.id,
            "symbol": component.symbol,
            "kind": component.kind,
            "required": component.required,
            "build": build,
            "image": image,
            "entry": entry,
            "sdk": sdk,
            "api": api,
            "depends": list(component.depends),
            "api_requires": list(component.api_requires),
        }
    return selection


def component_json(components: tuple[Component, ...]) -> str:
    return json.dumps([asdict(component) for component in components], ensure_ascii=False, indent=2)


def component_config_symbols(components: tuple[Component, ...]) -> set[str]:
    """Return every CONFIG symbol generated from the component manifest."""
    symbols: set[str] = set()
    for component in components:
        symbols.add(component.build_symbol)
        for suffix in ("IMAGE", "ENTRY", "SDK", "API"):
            symbols.add(component.option_symbol(suffix))
    return symbols


def _has_source_file(path: Path) -> bool:
    if not path.is_dir():
        return False
    for _, _, filenames in os.walk(path):
        if any(Path(filename).suffix in SOURCE_SUFFIXES for filename in filenames):
            return True
    return False


def validate_component_targets(components: tuple[Component, ...], root: Path) -> None:
    """Reject manifest entries which cannot map to a maintained build target.

    Application identifiers intentionally map to their source directories; the
    specialized target aliases in build.py (nano, PL Editor, and package-only
    applications) still retain that same source-of-truth directory.
    """
    for component in components:
        if component.kind in {"system-app", "program-app", "package-app"}:
            candidates = (
                root / "userland" / "apps" / component.id,
                root / "userland" / component.id,
            )
            source = next(
                (
                    candidate for candidate in candidates
                    if _has_source_file(candidate)
                ),
                None,
            )
            if source is None:
                raise ComponentError(
                    f"component {component.id} has no application source target under userland"
                )
        elif component.kind == "tool":
            source = root / "userland" / component.id
            if not source.is_dir():
                raise ComponentError(
                    f"tool component {component.id} has no port source target at {source}"
                )
        elif component.kind == "library":
            source = root / "third_party" / component.id
            if not source.is_dir():
                raise ComponentError(
                    f"library component {component.id} has no third-party source target at {source}"
                )
