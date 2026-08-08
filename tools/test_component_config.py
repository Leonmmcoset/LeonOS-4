#!/usr/bin/env python3
"""Self-tests for the dynamic component configuration pipeline."""

from __future__ import annotations

import tempfile
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from buildsystem.components import (
    ComponentError,
    load_components,
    resolve_components,
    validate_component_targets,
)
from tools.generate_component_kconfig import generate
from tools.kconfig_sync import (
    normalize_values,
    parse_config,
    validate_configured_keys,
)


MANIFEST = ROOT / "configs/components.toml"


def minimal_manifest(*entries: str) -> str:
    return "version = 1\n\n" + "\n\n".join(entries) + "\n"


def component_entry(
    component_id: str, symbol: str, depends: str = "", kind: str = "program-app"
) -> str:
    suffix = f"depends = [{depends}]\n" if depends else ""
    return f"""[[components]]
id = "{component_id}"
symbol = "{symbol}"
label = "{component_id}"
category = "Tests"
kind = "{kind}"
default = true
required = false
stage = true
entry = false
sdk = false
api = false
{suffix}"""


def main() -> int:
    components = load_components(MANIFEST)
    validate_component_targets(components, ROOT)
    generated = generate(components)
    assert "config LEON_COMPONENT_APP_HELLO_BUILD" in generated
    assert "config LEON_COMPONENT_APP_HELLOWORLD_API" in generated
    assert "config LEON_COMPONENT_TOOL_TCC_API" not in generated
    assert "select LEON_COMPONENT_LIB_STARDUSTUI_BUILD" in generated

    defaults = {
        component.build_symbol: "n"
        for component in components
    }
    defaults["CONFIG_LEON_COMPONENT_APP_STARDUSTHELLO_BUILD"] = "y"
    defaults["CONFIG_LEON_COMPONENT_LIB_STARDUSTUI_BUILD"] = "n"
    selection = resolve_components(components, defaults)
    assert selection["stardusthello"]["build"]
    assert selection["stardustui"]["build"], "dependency must be auto-enabled"

    doom_api_values = {
        component.build_symbol: "n" for component in components
    }
    doom_api_values["CONFIG_LEON_COMPONENT_APP_DOOM_BUILD"] = "y"
    doom_api_values["CONFIG_LEON_COMPONENT_APP_DOOM_API"] = "y"
    doom_api_selection = resolve_components(components, doom_api_values)
    assert doom_api_selection["doomlauncher"]["build"]
    assert doom_api_selection["doomlauncher"]["api"]

    unsupported_api_values = {
        component.build_symbol: "n" for component in components
    }
    unsupported_api_values["CONFIG_LEON_COMPONENT_TOOL_TCC_BUILD"] = "y"
    unsupported_api_values["CONFIG_LEON_COMPONENT_TOOL_TCC_API"] = "y"
    unsupported_api_selection = resolve_components(components, unsupported_api_values)
    assert not unsupported_api_selection["tcc"]["api"], (
        "components without an API package must ignore stale API symbols"
    )

    stale_required_values = {
        "CONFIG_LEON_COMPONENT_APP_SHELL_ENTRY": "y",
    }
    stale_required_selection = resolve_components(components, stale_required_values)
    assert not stale_required_selection["shell"]["entry"]

    static_defaults = parse_config(ROOT / "configs/default.conf")
    release_values = normalize_values(
        static_defaults,
        {
            "CONFIG_BUILD_PRESET_DEVELOP": "n",
            "CONFIG_BUILD_PRESET_RELEASE": "y",
            "CONFIG_BUILD_USE_ADVANCED_OVERRIDES": "n",
            "CONFIG_BUILD_OPTIMIZATION_LEVEL": "0",
            "CONFIG_BUILD_DEBUG_SYMBOLS": "y",
            "CONFIG_BUILD_STRIP_BINARIES": "n",
        },
        {},
    )
    assert release_values["CONFIG_BUILD_OPTIMIZATION_LEVEL"] == "3"
    assert release_values["CONFIG_BUILD_DEBUG_SYMBOLS"] == "n"
    assert release_values["CONFIG_BUILD_STRIP_BINARIES"] == "y"
    try:
        validate_configured_keys({"CONFIG_NOT_A_REAL_OPTION": "y"}, static_defaults, components)
    except ValueError:
        pass
    else:
        raise AssertionError("unknown configuration symbols must be rejected")

    with tempfile.TemporaryDirectory(prefix="leonos-components-") as directory:
        root = Path(directory)
        duplicate = root / "duplicate.toml"
        duplicate.write_text(minimal_manifest(
            component_entry("one", "TEST_ONE"),
            component_entry("one", "TEST_TWO"),
        ), encoding="utf-8")
        try:
            load_components(duplicate)
        except ComponentError:
            pass
        else:
            raise AssertionError("duplicate component IDs must be rejected")

        cycle = root / "cycle.toml"
        cycle.write_text(minimal_manifest(
            component_entry("one", "TEST_ONE", '"two"'),
            component_entry("two", "TEST_TWO", '"one"'),
        ), encoding="utf-8")
        try:
            load_components(cycle)
        except ComponentError:
            pass
        else:
            raise AssertionError("dependency cycles must be rejected")

        invalid_kind = root / "invalid-kind.toml"
        invalid_kind.write_text(
            minimal_manifest(component_entry("one", "TEST_ONE", kind="invalid")),
            encoding="utf-8",
        )
        try:
            load_components(invalid_kind)
        except ComponentError:
            pass
        else:
            raise AssertionError("unsupported component kinds must be rejected")

        missing_target = root / "missing-target.toml"
        missing_target.write_text(
            minimal_manifest(component_entry("missing", "TEST_MISSING")),
            encoding="utf-8",
        )
        try:
            validate_component_targets(load_components(missing_target), root)
        except ComponentError:
            pass
        else:
            raise AssertionError("missing component source targets must be rejected")

    print("component configuration self-tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
