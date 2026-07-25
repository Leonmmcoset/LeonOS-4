from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING, Callable, Iterable

if TYPE_CHECKING:
    from .runner import ActionContext


Action = Callable[["ActionContext"], None]


@dataclass(slots=True)
class Target:
    """One reproducible graph node."""

    name: str
    outputs: tuple[Path, ...] = ()
    inputs: tuple[Path, ...] = ()
    implicit_inputs: tuple[Path, ...] = ()
    depends_on: tuple[str, ...] = ()
    kind: str = "generate"
    description: str = ""
    command: tuple[str, ...] | None = None
    action: Action | None = None
    action_key: str = ""
    depfile: Path | None = None
    cwd: Path | None = None
    group: bool = False
    always: bool = False
    source: Path | None = None
    environment: dict[str, str] = field(default_factory=dict)

    def all_inputs(self) -> tuple[Path, ...]:
        return self.inputs + self.implicit_inputs


class GraphError(RuntimeError):
    pass


class BuildGraph:
    """Target graph with stable lookup by task name, output, or source."""

    def __init__(self, root: Path) -> None:
        self.root = root.resolve()
        self.targets: dict[str, Target] = {}
        self._outputs: dict[Path, Target] = {}
        self._sources: dict[Path, list[Target]] = {}
        self._inputs: dict[Path, list[Target]] = {}

    def path(self, value: Path | str) -> Path:
        path = Path(value)
        if not path.is_absolute():
            path = self.root / path
        return Path(os.path.normpath(path))

    def relative(self, path: Path | str) -> str:
        path = self.path(path)
        try:
            return path.relative_to(self.root).as_posix()
        except ValueError:
            return str(path)

    def add(self, target: Target) -> Target:
        if target.name in self.targets:
            raise GraphError(f"duplicate target name: {target.name}")
        target.outputs = tuple(self.path(path) for path in target.outputs)
        target.inputs = tuple(self.path(path) for path in target.inputs)
        target.implicit_inputs = tuple(self.path(path) for path in target.implicit_inputs)
        if target.depfile is not None:
            target.depfile = self.path(target.depfile)
        if target.cwd is not None:
            target.cwd = self.path(target.cwd)
        if target.source is not None:
            target.source = self.path(target.source)
            self._sources.setdefault(target.source, []).append(target)
        for path in target.all_inputs():
            self._inputs.setdefault(path, []).append(target)
        for output in target.outputs:
            if output in self._outputs:
                owner = self._outputs[output].name
                raise GraphError(f"duplicate output {self.relative(output)}: {owner}, {target.name}")
            self._outputs[output] = target
        self.targets[target.name] = target
        return target

    def producer(self, path: Path | str) -> Target | None:
        return self._outputs.get(self.path(path))

    def dependencies(self, target: Target) -> tuple[Target, ...]:
        seen: set[str] = set()
        result: list[Target] = []
        for path in target.all_inputs():
            producer = self._outputs.get(path)
            if producer is not None and producer.name not in seen:
                seen.add(producer.name)
                result.append(producer)
        for name in target.depends_on:
            dependency = self.targets.get(name)
            if dependency is None:
                raise GraphError(f"target {target.name} depends on unknown target {name}")
            if dependency.name not in seen:
                seen.add(dependency.name)
                result.append(dependency)
        return tuple(result)

    def related_targets(self, value: Path | str) -> tuple[Target, ...]:
        path = self.path(value)
        result: dict[str, Target] = {}
        producer = self._outputs.get(path)
        if producer is not None:
            result[producer.name] = producer
        for target in self._sources.get(path, ()):
            result[target.name] = target
        for target in self._inputs.get(path, ()):
            result[target.name] = target
        return tuple(result[name] for name in sorted(result))

    def dependents(self, roots: Iterable[Target]) -> tuple[Target, ...]:
        reverse: dict[str, set[str]] = {name: set() for name in self.targets}
        for target in self.targets.values():
            for dependency in self.dependencies(target):
                reverse[dependency.name].add(target.name)
        pending = [target.name for target in roots]
        names: set[str] = set(pending)
        while pending:
            name = pending.pop()
            for dependent in reverse[name]:
                if dependent not in names:
                    names.add(dependent)
                    pending.append(dependent)
        return tuple(self.targets[name] for name in sorted(names))

    def resolve_target(self, value: str) -> Target:
        if value in self.targets:
            return self.targets[value]
        path = self.path(value)
        target = self._outputs.get(path)
        if target is not None:
            return target
        candidates = self._sources.get(path, [])
        if not candidates:
            raise GraphError(f"unknown task or file: {value}")
        if len(candidates) != 1:
            outputs = sorted(
                self.relative(candidate.outputs[0])
                for candidate in candidates
                if candidate.outputs
            )
            raise GraphError(
                f"source {self.relative(path)} maps to multiple outputs: "
                + ", ".join(outputs)
            )
        return candidates[0]

    def closure(self, roots: Iterable[Target]) -> tuple[Target, ...]:
        ordered: list[Target] = []
        visiting: set[str] = set()
        visited: set[str] = set()

        def visit(target: Target) -> None:
            if target.name in visited:
                return
            if target.name in visiting:
                raise GraphError(f"dependency cycle at {target.name}")
            visiting.add(target.name)
            for dependency in self.dependencies(target):
                visit(dependency)
            visiting.remove(target.name)
            visited.add(target.name)
            ordered.append(target)

        for root in roots:
            visit(root)
        return tuple(ordered)

    def map_lines(self) -> list[str]:
        lines: list[str] = []
        for name in sorted(self.targets):
            target = self.targets[name]
            outputs = ", ".join(self.relative(path) for path in target.outputs) or "(aggregate)"
            dependencies = ", ".join(dep.name for dep in self.dependencies(target)) or "(none)"
            lines.append(f"{name} [{target.kind}] -> {outputs}")
            lines.append(f"  depends on: {dependencies}")
        return lines