# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Renders a release registry as a table.

    .. release-registry:: deprecations.yaml

A registry is a YAML list of entries next to the page that shows it. The file name selects the
columns. An entry with a missing or unknown key, or an id that is used twice, fails the build.
"""

from pathlib import Path

import yaml
from docutils import nodes
from docutils.parsers.rst import directives
from docutils.statemachine import StringList
from sphinx.errors import ExtensionError
from sphinx.util.docutils import SphinxDirective

# Registry file name -> (column title, entry key) per column, and the column widths.
REGISTRIES = {
    "deprecations.yaml": (
        (
            ("Component", "component"),
            ("Deprecated in", "deprecated_in"),
            ("Earliest removal", "earliest_removal"),
            ("Migration guide", "migration"),
        ),
        "30 20 20 30",
    ),
    "experimental.yaml": (
        (("Component", "component"), ("Experimental since", "since"), ("Notes", "notes")),
        "30 20 50",
    ),
}
# Keys that are not shown in the table.
OTHER_KEYS = {"id", "covers"}


def load_registry(path: Path) -> list:
    """The entries of a registry file, checked against the keys its table needs."""
    if path.name not in REGISTRIES:
        raise ExtensionError(f"{path}: not a release registry, expected one of {sorted(REGISTRIES)}")
    columns, _ = REGISTRIES[path.name]
    required = {key for _, key in columns} | {"id"}
    entries = yaml.safe_load(path.read_text()) or []
    if not isinstance(entries, list):
        raise ExtensionError(f"{path}: a registry is a list of entries")
    seen = set()
    for entry in entries:
        if not isinstance(entry, dict) or "id" not in entry:
            raise ExtensionError(f"{path}: every entry needs an id: {entry!r}")
        missing, unknown = required - entry.keys(), entry.keys() - required - OTHER_KEYS
        if missing or unknown:
            raise ExtensionError(
                f"{path}: entry {entry['id']!r}: missing keys {sorted(missing)}, unknown keys {sorted(unknown)}"
            )
        if entry["id"] in seen:
            raise ExtensionError(f"{path}: id {entry['id']!r} is used twice")
        seen.add(entry["id"])
    return entries


def _cell(prefix: str, text: str) -> list:
    lines = str(text).splitlines() or [""]
    return [prefix + lines[0]] + [(" " * len(prefix) + line).rstrip() for line in lines[1:]]


def table_lines(path: Path) -> list:
    """The registry as the lines of a reStructuredText list-table."""
    columns, widths = REGISTRIES[path.name]
    lines = [".. list-table::", "   :header-rows: 1", f"   :widths: {widths}", ""]
    rows = [[title for title, _ in columns]] + [[entry[key] for _, key in columns] for entry in load_registry(path)]
    for row in rows:
        for index, cell in enumerate(row):
            lines += _cell("   * - " if index == 0 else "     - ", cell)
    return lines


class ReleaseRegistry(SphinxDirective):
    required_arguments = 1
    final_argument_whitespace = True
    option_spec = {"class": directives.class_option}

    def run(self):
        _, absolute = self.env.relfn2path(self.arguments[0])
        path = Path(absolute)
        self.env.note_dependency(absolute)
        container = nodes.Element()
        self.state.nested_parse(StringList(table_lines(path), source=str(path)), self.content_offset, container)
        return container.children


def setup(app):
    app.add_directive("release-registry", ReleaseRegistry)
    return {"version": "1.0", "parallel_read_safe": True, "parallel_write_safe": True}
