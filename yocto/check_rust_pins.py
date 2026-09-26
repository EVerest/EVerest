#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Checks that the git crates of the Rust modules are fetched by the Yocto recipe at the
revision cargo pins them to.

The Yocto build has no network access: the recipe fetches each git dependency at its
SRCREV and patches it into cargo, which replaces whatever revision cargo asked for. The
pins therefore have to agree. Takes a Cargo.toml or Cargo.lock, the recipe and the names
of the git crates.
"""

import re
import sys
from pathlib import Path

RECIPE_SRC_URI = re.compile(r'git://(?P<hostpath>[^;"\s]+)(?P<params>(;[^;"\s]+)*)')
RECIPE_SRCREV = re.compile(r'^\s*SRCREV(?:_(?P<name>\w+))?\s*=\s*"(?P<rev>[0-9a-f]+)"')


def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def cargo_pin(cargo_file, crate):
    """Returns the git revision a crate is pinned to in a Cargo.toml or Cargo.lock."""
    text = cargo_file.read_text()
    if cargo_file.name == "Cargo.lock":
        pattern = rf'^name = "{re.escape(crate)}"\n(?:(?!\[\[package\]\]).*\n)*?source = "git\+[^"#]*#(?P<rev>[0-9a-f]+)"'
    else:
        pattern = rf'^\s*{re.escape(crate)}\s*=\s*\{{.*\brev\s*=\s*"(?P<rev>[0-9a-f]+)"'
    match = re.search(pattern, text, re.M)
    if not match:
        fail(f"{cargo_file}: no git revision for crate {crate}")
    return match.group("rev")


def recipe_git_pins(recipe):
    """Returns {url: rev} for the git sources the recipe fetches."""
    urls = {}
    srcrevs = {}
    for line in recipe.read_text().splitlines():
        for match in RECIPE_SRC_URI.finditer(line):
            params = dict(p.split("=", 1) for p in match.group("params").split(";") if "=" in p)
            url = f"{params.get('protocol', 'git')}://{match.group('hostpath')}"
            urls[params.get("name")] = url
        match = RECIPE_SRCREV.match(line)
        if match:
            srcrevs[match.group("name")] = match.group("rev")
    pins = {}
    for name, url in urls.items():
        if name not in srcrevs:
            fail(f"{recipe}: no SRCREV{'_' + name if name else ''} for {url}")
        pins[url] = srcrevs[name]
    return pins


def main():
    if len(sys.argv) < 4:
        fail("usage: check_rust_pins.py <Cargo.toml|Cargo.lock> <recipe.inc> <crate>...")
    cargo_file, recipe = Path(sys.argv[1]), Path(sys.argv[2])
    fetched = recipe_git_pins(recipe)
    for crate in sys.argv[3:]:
        rev = cargo_pin(cargo_file, crate)
        if rev not in fetched.values():
            fail(f"{crate} is pinned to {rev} in {cargo_file}, but {recipe} fetches {', '.join(f'{url} at {rev}' for url, rev in fetched.items()) or 'nothing'}")
        print(f"ok: {crate} @ {rev}")


if __name__ == "__main__":
    main()
