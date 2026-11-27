#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
"""
Build the third-party dependencies listed in dependencies.yaml from source and install them
into a prefix, so EVerest can be configured against it with -DDISABLE_EDM=ON.

Repositories, versions and CMake options come from dependencies.yaml. Toolchain and system
libraries (Boost, OpenSSL, SQLite, zlib, libcap, libsystemd) are taken from the host.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml

# Not built by default: heavy or not installable with a plain CMake build.
DEFAULT_SKIP = ["grpc", "eebus-grpc", "grpc-extended-cpp-plugin", "Josev", "libnfc-nci"]

# Dependencies are installed one after another and find each other in the prefix.
# Entries not listed here follow in dependencies.yaml order.
ORDER = ["nlohmann_json", "date", "libfmt", "ryml", "pybind11", "pybind11_json",
         "nlohmann_json_schema_validator", "json-rpc-cxx", "sigslot", "pugixml",
         "libwebsockets", "mosquitto", "sdbus-cpp", "ftxui", "gtest", "catch2", "libcurl"]

# dependencies.yaml switches these off because EDM builds the dependency in-tree.
INSTALL_OPTIONS = {
    "nlohmann_json_schema_validator": {"JSON_VALIDATOR_INSTALL": "ON"},
}


def cmake_options(name: str, entry: dict, prefix: Path) -> list:
    options = {}
    for item in entry.get("options") or []:
        key, _, value = item.partition(" ")
        # EDM expands the variable; on the command line it would stay literal.
        options[key] = value.strip().replace("${CMAKE_INSTALL_LIBDIR}", str(prefix / "lib"))
    options.update(INSTALL_OPTIONS.get(name, {}))
    return [f"-D{key}={value}" for key, value in options.items()]


def run(cmd: list, log) -> None:
    log.write("$ " + " ".join(str(c) for c in cmd) + "\n")
    log.flush()
    subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)


def fetch(url: str, ref: str, src: Path, log) -> None:
    run(["git", "init", "--quiet", src], log)
    run(["git", "-C", src, "fetch", "--quiet", "--depth", "1", url, ref], log)
    run(["git", "-C", src, "checkout", "--quiet", "FETCH_HEAD"], log)
    run(["git", "-C", src, "submodule", "update", "--quiet", "--init", "--recursive", "--depth", "1"], log)


def build(src: Path, build_dir: Path, prefix: Path, args, options: list, log) -> None:
    run(["cmake", "-S", src, "-B", build_dir, "-G", "Ninja",
         f"-DCMAKE_INSTALL_PREFIX={prefix}",
         f"-DCMAKE_PREFIX_PATH={prefix}",
         f"-DCMAKE_BUILD_TYPE={args.build_type}",
         "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
         "-DBUILD_TESTING=OFF",
         *options], log)
    run(["cmake", "--build", build_dir, "-j", str(args.jobs)], log)
    run(["cmake", "--install", build_dir], log)


def install_ev_cli(src: Path, prefix: Path, log) -> None:
    # Without EDM, the build does not install ev-dev-tools, so ev-cli goes into a venv in the prefix.
    venv = prefix / "venv"
    run([sys.executable, "-m", "venv", venv], log)
    run([venv / "bin" / "pip", "install", "--quiet", src / "applications" / "utils" / "ev-dev-tools"], log)


def step(name: str, logs: Path, action) -> None:
    print(f"==> {name}", flush=True)
    log_path = logs / f"{name}.log"
    with open(log_path, "w", encoding="utf-8") as log:
        try:
            action(log)
        except subprocess.CalledProcessError:
            log.close()
            lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
            print("\n".join(lines[-50:]), file=sys.stderr)
            sys.exit(f"{name} failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--prefix", type=Path, help="install prefix of the sysroot")
    parser.add_argument("--src", type=Path, default=Path(__file__).resolve().parents[3],
                        help="EVerest checkout holding dependencies.yaml (default: this checkout)")
    parser.add_argument("--work-dir", type=Path,
                        help="keep sources, build trees and logs here (default: a temporary directory)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count(), help="parallel build jobs")
    parser.add_argument("--build-type", default="Release", help="CMAKE_BUILD_TYPE of the dependencies")
    parser.add_argument("--skip", action="append", default=[], help="skip a dependency (repeatable)")
    parser.add_argument("--with", dest="extra", action="append", default=[],
                        help="build a dependency that is skipped by default (repeatable)")
    parser.add_argument("--list", action="store_true", help="print the build plan and exit")
    args = parser.parse_args()
    if not args.prefix and not args.list:
        parser.error("--prefix is required")

    src = args.src.resolve()
    deps = yaml.safe_load((src / "dependencies.yaml").read_text(encoding="utf-8"))
    names = [n for n in ORDER if n in deps] + [n for n in deps if n not in ORDER]
    skip = set(args.skip) | (set(DEFAULT_SKIP) - set(args.extra))
    plan = [n for n in names if n not in skip and "git" in (deps[n] or {})]

    if args.list:
        for name in names:
            print(f"{name:34} {'build' if name in plan else 'skip':6} {deps[name].get('git_tag', '')}")
        return

    prefix = args.prefix.resolve()
    work = args.work_dir.resolve() if args.work_dir else Path(tempfile.mkdtemp(prefix="everest-sysroot-"))
    logs = work / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    try:
        for name in plan:
            def install_dependency(log, name=name, entry=deps[name]):
                source, build_dir = work / "src" / name, work / "build" / name
                shutil.rmtree(source, ignore_errors=True)
                shutil.rmtree(build_dir, ignore_errors=True)
                fetch(entry["git"], str(entry.get("git_tag", "HEAD")), source, log)
                build(source, build_dir, prefix, args, cmake_options(name, entry, prefix), log)
            step(name, logs, install_dependency)
        step("ev-cli", logs, lambda log: install_ev_cli(src, prefix, log))
    finally:
        if not args.work_dir:
            shutil.rmtree(work, ignore_errors=True)

    excluded = ";".join(n for n in names if n not in plan)
    print(f"\nsysroot ready: {prefix}\n"
          f"configure EVerest with -DDISABLE_EDM=ON -DCMAKE_PREFIX_PATH={prefix} "
          f"-DEV_CLI={prefix}/venv/bin/ev-cli -Deverest-core_USE_PYTHON_VENV=OFF "
          f"-DEVEREST_EXCLUDE_DEPENDENCIES=\"{excluded}\"")


if __name__ == "__main__":
    main()
