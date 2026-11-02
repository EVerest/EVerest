#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Checks that the Yocto recipe fetches the crates of the Rust modules at the versions
cargo pins them to.

The Yocto build has no network access: the recipe fetches each git dependency at its
SRCREV and patches it into cargo, which replaces whatever revision cargo asked for, and
it fetches every crates.io crate from the list `bitbake -c update_crates` generates out
of Cargo.lock. Both have to agree with cargo. Takes a Cargo.toml or Cargo.lock, the
recipe and the names of the git crates; with --crates-inc and a Cargo.lock, also checks
the generated crate list against the lock.
"""

import argparse
import re
import sys
import tomllib
from pathlib import Path

RECIPE_SRC_URI = re.compile(r'git://(?P<hostpath>[^;"\s]+)(?P<params>(;[^;"\s]+)*)')
RECIPE_SRCREV = re.compile(r'^\s*SRCREV(?:_(?P<name>\w+))?\s*=\s*"(?P<rev>[0-9a-f]+)"')
CRATES_SRC_URI = re.compile(r"crate://crates\.io/(?P<name>[^/\s]+)/(?P<version>[^\s;\\]+)")
CRATES_SHA256 = re.compile(r'^SRC_URI\[(?P<key>[^\]]+)\.sha256sum\]\s*=\s*"(?P<sum>[0-9a-f]+)"', re.M)


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


def locked_registry_crates(lock):
    """Returns {(name, version): checksum} for the crates.io packages of a Cargo.lock."""
    packages = tomllib.loads(lock.read_text()).get("package", [])
    return {
        (package["name"], package["version"]): package.get("checksum")
        for package in packages
        if package.get("source", "").startswith("registry+")
    }


def recipe_crates(crates_inc):
    """Returns {(name, version): sha256} for the crates.io crates a generated crate list fetches."""
    text = crates_inc.read_text()
    sums = {match.group("key"): match.group("sum") for match in CRATES_SHA256.finditer(text)}
    return {
        (match.group("name"), match.group("version")): sums.get(f"{match.group('name')}-{match.group('version')}")
        for match in CRATES_SRC_URI.finditer(text)
    }


def check_crates(lock, crates_inc):
    """Fails unless the generated crate list fetches exactly the crates.io crates the lock pins."""
    locked = locked_registry_crates(lock)
    fetched = recipe_crates(crates_inc)
    problems = [f"{name} {version} is locked in {lock} but not fetched by {crates_inc}"
                for name, version in sorted(locked.keys() - fetched.keys())]
    problems += [f"{name} {version} is fetched by {crates_inc} but not locked in {lock}"
                 for name, version in sorted(fetched.keys() - locked.keys())]
    problems += [f"{name} {version} has sha256 {fetched[(name, version)]} in {crates_inc}, "
                 f"but checksum {locked[(name, version)]} in {lock}"
                 for name, version in sorted(locked.keys() & fetched.keys())
                 if fetched[(name, version)] != locked[(name, version)]]
    if problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        fail(f"{crates_inc} is out of date; regenerate it with `bitbake -c update_crates everest-core`")
    print(f"ok: {len(locked)} crates.io crates in {crates_inc} match {lock}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("cargo_file", type=Path, metavar="Cargo.toml|Cargo.lock")
    parser.add_argument("recipe", type=Path, metavar="recipe.inc")
    parser.add_argument("crates", nargs="+", metavar="crate", help="git crate to check")
    parser.add_argument("--crates-inc", type=Path, metavar="crates.inc",
                        help="crate list generated by `bitbake -c update_crates`, checked against the Cargo.lock")
    args = parser.parse_args()
    if args.crates_inc and args.cargo_file.name != "Cargo.lock":
        fail("--crates-inc needs a Cargo.lock")

    fetched = recipe_git_pins(args.recipe)
    for crate in args.crates:
        rev = cargo_pin(args.cargo_file, crate)
        if rev not in fetched.values():
            fail(f"{crate} is pinned to {rev} in {args.cargo_file}, but {args.recipe} fetches {', '.join(f'{url} at {rev}' for url, rev in fetched.items()) or 'nothing'}")
        print(f"ok: {crate} @ {rev}")

    if args.crates_inc:
        check_crates(args.cargo_file, args.crates_inc)


if __name__ == "__main__":
    main()
