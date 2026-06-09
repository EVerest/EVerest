#!/usr/bin/env python3
"""Check API/ABI compatibility of standalone lib/everest libraries.

The script compares two git refs without changing the current worktree:
it exports each ref with ``git archive``, builds selected libraries in
temporary directories, and runs abi-compliance-checker on generated XML
descriptors.
"""

from __future__ import annotations

import argparse
import dataclasses
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[2]
LIB_ROOT = Path("lib/everest")
PUBLIC_LIB_HINTS = {
    "cbv2g",
    "evse_security",
    "framework",
    "ieee2030_1_1",
    "iso15118",
    "log",
    "ocpp",
}
SIBLING_PREREQUISITES = {
    "evse_security": ("log", "timer"),
    "framework": ("log", "sqlite"),
}
INTERFACE_PREREQUISITES = {
    "framework": (("everest_util", "everest::util", "util/include"),),
}


@dataclasses.dataclass(frozen=True)
class Library:
    name: str
    relpath: Path
    project_name: str | None
    install_options: tuple[str, ...]
    has_include_dir: bool


@dataclasses.dataclass
class CommandResult:
    command: list[str]
    returncode: int
    log_path: Path


@dataclasses.dataclass
class BuiltLibrary:
    library: Library
    ref: str
    source_dir: Path
    build_dir: Path
    install_dir: Path
    headers: list[Path]
    search_headers: list[Path]
    libs: list[Path]
    configure: CommandResult
    build: CommandResult | None
    install: CommandResult | None
    notes: list[str]


@dataclasses.dataclass
class CompatibilityResult:
    library: Library
    old: BuiltLibrary
    new: BuiltLibrary
    returncode: int
    report_path: Path
    log_path: Path
    descriptor_old: Path
    descriptor_new: Path
    source_compat: str | None
    binary_compat: str | None
    notes: list[str]


def run_command(command: list[str], log_path: Path, cwd: Path | None = None) -> CommandResult:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log:
        log.write("$ " + " ".join(command) + "\n\n")
        completed = subprocess.run(
            command,
            cwd=cwd,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    return CommandResult(command=command, returncode=completed.returncode, log_path=log_path)


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return path.read_text(encoding="latin-1")


def parse_project_name(cmake_text: str) -> str | None:
    match = re.search(r"\bproject\s*\(\s*([A-Za-z0-9_.+-]+)", cmake_text)
    return match.group(1) if match else None


def parse_install_options(cmake_text: str) -> tuple[str, ...]:
    options = []
    for match in re.finditer(r"\boption\s*\(\s*([A-Za-z0-9_]+)\b", cmake_text):
        option_name = match.group(1)
        if option_name.endswith("_INSTALL"):
            options.append(option_name)
    return tuple(dict.fromkeys(options))


def sibling_source_dir_args(lib_root: Path) -> list[str]:
    args: list[str] = []
    for cmake_file in sorted(lib_root.glob("*/CMakeLists.txt")):
        project_name = parse_project_name(read_text(cmake_file))
        if project_name:
            args.append(f"-D{project_name}_SOURCE_DIR={cmake_file.parent}")
    return args


def discover_libraries(repo_root: Path) -> list[Library]:
    libraries: list[Library] = []
    for cmake_file in sorted((repo_root / LIB_ROOT).glob("*/CMakeLists.txt")):
        relpath = cmake_file.parent.relative_to(repo_root)
        cmake_text = read_text(cmake_file)
        if "project(" not in cmake_text and "project (" not in cmake_text:
            continue

        has_include_dir = (cmake_file.parent / "include").is_dir()
        install_options = parse_install_options(cmake_text)
        if not has_include_dir and not install_options:
            continue

        libraries.append(
            Library(
                name=cmake_file.parent.name,
                relpath=relpath,
                project_name=parse_project_name(cmake_text),
                install_options=install_options,
                has_include_dir=has_include_dir,
            )
        )
    return libraries


def select_libraries(repo_root: Path, requested: list[str], all_libraries: bool) -> list[Library]:
    discovered = discover_libraries(repo_root)
    by_name = {library.name: library for library in discovered}
    if all_libraries:
        return discovered
    if not requested:
        names = ", ".join(library.name for library in discovered)
        raise SystemExit(f"Select at least one --library or pass --all. Discovered: {names}")

    selected: list[Library] = []
    for name in requested:
        key = name.strip().rstrip("/")
        if "/" in key:
            key = Path(key).name
        if key not in by_name:
            names = ", ".join(library.name for library in discovered)
            raise SystemExit(f"Unknown library '{name}'. Discovered: {names}")
        selected.append(by_name[key])
    return selected


def safe_label(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def safe_rmtree(path: Path, work_dir: Path) -> None:
    """Remove generated directories only when they are safely below work_dir."""
    if not path.exists():
        return

    resolved_path = path.resolve()
    resolved_work_dir = work_dir.resolve()
    forbidden_paths = {
        Path("/"),
        Path.cwd().resolve(),
        REPO_ROOT.resolve(),
        resolved_work_dir,
    }
    if resolved_path in forbidden_paths:
        raise RuntimeError(f"refusing to remove unsafe path: {resolved_path}")

    try:
        resolved_path.relative_to(resolved_work_dir)
    except ValueError as exc:
        raise RuntimeError(f"refusing to remove path outside work dir: {resolved_path}") from exc

    shutil.rmtree(resolved_path)


def export_ref(repo_root: Path, ref: str, destination: Path, logs_dir: Path, work_dir: Path) -> None:
    safe_rmtree(destination, work_dir)
    destination.mkdir(parents=True, exist_ok=True)
    archive_path = destination.parent / f"{safe_label(ref)}.tar"
    archive = run_command(
        ["git", "-C", str(repo_root), "archive", "--format=tar", "-o", str(archive_path), ref],
        logs_dir / f"git-archive-{safe_label(ref)}.log",
    )
    if archive.returncode != 0:
        raise RuntimeError(f"git archive failed for {ref}; see {archive.log_path}")

    extract = run_command(["tar", "-xf", str(archive_path), "-C", str(destination)], logs_dir / f"tar-{safe_label(ref)}.log")
    if extract.returncode != 0:
        raise RuntimeError(f"tar extraction failed for {ref}; see {extract.log_path}")
    archive_path.unlink(missing_ok=True)


def unique_existing(paths: Iterable[Path]) -> list[Path]:
    seen: set[Path] = set()
    result: list[Path] = []
    for path in paths:
        resolved = path.resolve()
        if path.exists() and resolved not in seen:
            seen.add(resolved)
            result.append(path)
    return result


def find_shared_libraries(*roots: Path) -> list[Path]:
    candidates: list[Path] = []
    patterns = ("*.so", "*.so.*", "*.dylib", "*.dll")
    for root in roots:
        if not root.exists():
            continue
        for pattern in patterns:
            candidates.extend(path for path in root.rglob(pattern) if path.is_file())
    return unique_existing(candidates)


def read_cmake_cache_value(build_dir: Path, name: str) -> Path | None:
    cache_file = build_dir / "CMakeCache.txt"
    if not cache_file.exists():
        return None
    for line in read_text(cache_file).splitlines():
        if line.startswith(f"{name}:"):
            _, value = line.split("=", 1)
            return Path(value)
    return None


def find_include_roots(*roots: Path) -> list[Path]:
    candidates: list[Path] = []
    for root in roots:
        if not root or not root.exists():
            continue
        if root.name == "include":
            candidates.append(root)
        candidates.extend(path for path in root.rglob("include") if path.is_dir())
    return unique_existing(candidates)


def find_generated_roots(*roots: Path) -> list[Path]:
    candidates: list[Path] = []
    for root in roots:
        if not root or not root.exists():
            continue
        if root.name == "generated":
            candidates.append(root)
        candidates.extend(path for path in root.rglob("generated") if path.is_dir())
    return unique_existing(candidates)


def find_header_marker_roots(root: Path | None, markers: Iterable[Path]) -> list[Path]:
    if not root or not root.exists():
        return []

    candidates: list[Path] = []
    for marker in markers:
        marker_parts = marker.parts
        marker_depth = len(marker_parts)
        for header in root.rglob(marker.name):
            if header.parts[-marker_depth:] == marker_parts:
                candidates.append(header.parents[marker_depth - 1])
    return unique_existing(candidates)


def create_wrapper_source_dir(repo_export: Path, library: Library, work_dir: Path, dir_label: str) -> Path:
    prerequisites = SIBLING_PREREQUISITES.get(library.name, ())
    if not prerequisites:
        return repo_export / library.relpath

    wrapper_dir = work_dir / "wrappers" / f"{library.name}-{dir_label}"
    safe_rmtree(wrapper_dir, work_dir)
    wrapper_dir.mkdir(parents=True, exist_ok=True)

    lines = [
        "cmake_minimum_required(VERSION 3.14)\n",
        f"project(compat-{library.name} LANGUAGES CXX C)\n",
        "\n",
        "find_package(everest-cmake 0.5 REQUIRED)\n",
        f'include("{repo_export / "cmake" / "ev-targets.cmake"}")\n',
        "\n",
    ]
    for prerequisite in prerequisites:
        prerequisite_dir = repo_export / LIB_ROOT / prerequisite
        lines.append(f'add_subdirectory("{prerequisite_dir}" "{prerequisite}")\n')
    for target_name, alias_name, include_relpath in INTERFACE_PREREQUISITES.get(library.name, ()):
        include_dir = repo_export / LIB_ROOT / include_relpath
        lines.extend(
            [
                f"add_library({target_name} INTERFACE)\n",
                f"add_library({alias_name} ALIAS {target_name})\n",
                f'target_include_directories({target_name} INTERFACE "{include_dir}")\n',
            ]
        )
    lines.append(f'add_subdirectory("{repo_export / library.relpath}" "{library.name}")\n')
    (wrapper_dir / "CMakeLists.txt").write_text("".join(lines), encoding="utf-8")
    return wrapper_dir


def build_library(
    repo_export: Path,
    library: Library,
    ref_name: str,
    dir_label: str,
    work_dir: Path,
    jobs: int,
    cmake_args: list[str],
    cmake_prefixes: list[Path],
) -> BuiltLibrary:
    source_dir = repo_export / library.relpath
    cmake_source_dir = create_wrapper_source_dir(repo_export, library, work_dir, dir_label)
    build_dir = work_dir / "build" / f"{library.name}-{dir_label}"
    install_dir = work_dir / "install" / f"{library.name}-{dir_label}"
    logs_dir = work_dir / "logs" / library.name / dir_label
    notes: list[str] = []

    safe_rmtree(build_dir, work_dir)
    safe_rmtree(install_dir, work_dir)

    configure_cmd = [
        "cmake",
        "-S",
        str(cmake_source_dir),
        "-B",
        str(build_dir),
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        f"-DCMAKE_INSTALL_PREFIX={install_dir}",
        "-DBUILD_SHARED_LIBS=ON",
        "-DBUILD_TESTING=OFF",
        "-DEVC_BUILD_TESTING=OFF",
        f"-DCPM_SOURCE_CACHE={work_dir / 'cpm_source_cache'}",
    ]
    configure_cmd.extend(f"-D{option}=ON" for option in library.install_options)
    configure_cmd.extend(sibling_source_dir_args(repo_export / LIB_ROOT))
    if cmake_prefixes:
        configure_cmd.append("-DCMAKE_PREFIX_PATH=" + ";".join(str(prefix) for prefix in cmake_prefixes))
    configure_cmd.extend(cmake_args)

    configure = run_command(configure_cmd, logs_dir / "configure.log")
    build: CommandResult | None = None
    install: CommandResult | None = None

    if configure.returncode == 0:
        build = run_command(["cmake", "--build", str(build_dir), "--parallel", str(jobs)], logs_dir / "build.log")
        if build.returncode == 0:
            install = run_command(["cmake", "--install", str(build_dir), "--prefix", str(install_dir)], logs_dir / "install.log")
            if install.returncode != 0:
                notes.append(f"install failed; using source/build artifacts where possible ({install.log_path})")
        else:
            notes.append(f"build failed ({build.log_path})")
    else:
        notes.append(f"configure failed ({configure.log_path})")

    return collect_built_library(repo_export, library, ref_name, dir_label, work_dir, configure, build, install, notes)


def collect_built_library(
    repo_export: Path,
    library: Library,
    ref_name: str,
    dir_label: str,
    work_dir: Path,
    configure: CommandResult,
    build: CommandResult | None,
    install: CommandResult | None,
    notes: list[str] | None = None,
) -> BuiltLibrary:
    source_dir = repo_export / library.relpath
    build_dir = work_dir / "build" / f"{library.name}-{dir_label}"
    install_dir = work_dir / "install" / f"{library.name}-{dir_label}"
    notes = notes or []

    source_header_roots = unique_existing([source_dir / "include"])
    installed_header_roots = unique_existing([install_dir / "include"])
    header_roots = source_header_roots or installed_header_roots
    cpm_source_cache = read_cmake_cache_value(build_dir, "CPM_SOURCE_CACHE")
    search_header_roots = unique_existing(
        [
            *installed_header_roots,
            *source_header_roots,
            *find_include_roots(repo_export / LIB_ROOT),
            *find_generated_roots(build_dir),
            *(find_include_roots(cpm_source_cache) if cpm_source_cache else []),
            *find_header_marker_roots(cpm_source_cache, [Path("nlohmann/json-schema.hpp")]),
            *find_include_roots(build_dir / "_deps"),
            *find_header_marker_roots(build_dir / "_deps", [Path("nlohmann/json-schema.hpp")]),
        ]
    )
    lib_roots = [install_dir / "lib", install_dir / "lib64", build_dir]
    libs = find_shared_libraries(*lib_roots)
    if not libs:
        notes.append("no shared library artifact found; ABI check may be unavailable or header-only")

    return BuiltLibrary(
        library=library,
        ref=ref_name,
        source_dir=source_dir,
        build_dir=build_dir,
        install_dir=install_dir,
        headers=header_roots,
        search_headers=search_header_roots,
        libs=libs,
        configure=configure,
        build=build,
        install=install,
        notes=notes,
    )


def reuse_built_library(repo_export: Path, library: Library, ref_name: str, dir_label: str, work_dir: Path) -> BuiltLibrary:
    logs_dir = work_dir / "logs" / library.name / dir_label
    configure = CommandResult(command=["<reused configure>"], returncode=0, log_path=logs_dir / "configure.log")
    build = CommandResult(command=["<reused build>"], returncode=0, log_path=logs_dir / "build.log")
    install = CommandResult(command=["<reused install>"], returncode=0, log_path=logs_dir / "install.log")
    notes = ["reused existing build/install artifacts"]
    return collect_built_library(repo_export, library, ref_name, dir_label, work_dir, configure, build, install, notes)


def abi_version_label(ref: str, fallback: str) -> str:
    matches = re.findall(r"\d+(?:[._-]\d+)*", ref)
    if not matches:
        return fallback
    return matches[-1].replace("_", ".").replace("-", ".")


def write_descriptor(path: Path, version: str, headers: list[Path], search_headers: list[Path], libs: list[Path]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    parts = [f"<version>\n    {html.escape(version)}\n</version>\n"]
    if headers:
        parts.append("<headers>\n")
        parts.extend(f"    {html.escape(str(header))}\n" for header in headers)
        parts.append("</headers>\n")
    if search_headers:
        parts.append("<search_headers>\n")
        parts.extend(f"    {html.escape(str(header))}\n" for header in search_headers)
        parts.append("</search_headers>\n")
    if libs:
        parts.append("<libs>\n")
        parts.extend(f"    {html.escape(str(lib))}\n" for lib in libs)
        parts.append("</libs>\n")
    path.write_text("".join(parts), encoding="utf-8")


def parse_compatibility(log_text: str) -> tuple[str | None, str | None]:
    source = None
    binary = None
    for label, value in re.findall(r"\b(Binary|Source)\s+compatibility\s*:\s*([0-9.]+%)", log_text, re.IGNORECASE):
        if label.lower() == "source":
            source = value
        elif label.lower() == "binary":
            binary = value
    return source, binary


def write_combined_log(path: Path, logs: list[Path]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    parts = []
    for log in logs:
        parts.append(f"===== {log} =====\n")
        parts.append(read_text(log) if log.exists() else "<missing log>\n")
        if parts[-1] and not parts[-1].endswith("\n"):
            parts.append("\n")
        parts.append("\n")
    path.write_text("".join(parts), encoding="utf-8")


def run_compatibility_check(
    tool: str,
    library: Library,
    old: BuiltLibrary,
    new: BuiltLibrary,
    output_dir: Path,
) -> CompatibilityResult:
    library_dir = output_dir / library.name
    old_descriptor = library_dir / "old.xml"
    new_descriptor = library_dir / "new.xml"
    old_dump = library_dir / "old.dump"
    new_dump = library_dir / "new.dump"
    report_path = library_dir / "compat_report.html"
    log_path = library_dir / "abi-compliance-checker.log"
    old_dump_log = library_dir / "abi-dump-old.log"
    new_dump_log = library_dir / "abi-dump-new.log"
    compare_log = library_dir / "abi-compare.log"

    write_descriptor(old_descriptor, abi_version_label(old.ref, "1"), old.headers, old.search_headers, old.libs)
    write_descriptor(new_descriptor, abi_version_label(new.ref, "2"), new.headers, new.search_headers, new.libs)

    notes = []
    old_build_failed = old.build is None or old.build.returncode != 0
    new_build_failed = new.build is None or new.build.returncode != 0
    if old.configure.returncode != 0 or new.configure.returncode != 0 or old_build_failed or new_build_failed:
        log_path.write_text(
            "Skipped abi-compliance-checker because at least one side did not configure/build successfully.\n",
            encoding="utf-8",
        )
        return CompatibilityResult(
            library=library,
            old=old,
            new=new,
            returncode=2,
            report_path=report_path,
            log_path=log_path,
            descriptor_old=old_descriptor,
            descriptor_new=new_descriptor,
            source_compat=None,
            binary_compat=None,
            notes=["analysis skipped because at least one build failed"],
        )

    if not old.libs or not new.libs:
        notes.append("one side has no shared library artifact; running source/API headers-only analysis")

    old_dump_command = [
        tool,
        "-lib",
        library.name,
        "-dump",
        str(old_descriptor),
        "-dump-path",
        str(old_dump),
    ]
    if not old.libs or not new.libs:
        old_dump_command.append("-headers-only")
    old_dump_result = run_command(old_dump_command, old_dump_log)

    new_dump_command = [
        tool,
        "-lib",
        library.name,
        "-dump",
        str(new_descriptor),
        "-dump-path",
        str(new_dump),
    ]
    if not old.libs or not new.libs:
        new_dump_command.append("-headers-only")
    new_dump_result = run_command(new_dump_command, new_dump_log)

    if old_dump_result.returncode != 0 or new_dump_result.returncode != 0:
        write_combined_log(log_path, [old_dump_log, new_dump_log])
        log_text = read_text(log_path)
        source_compat, binary_compat = parse_compatibility(log_text)
        return CompatibilityResult(
            library=library,
            old=old,
            new=new,
            returncode=old_dump_result.returncode or new_dump_result.returncode,
            report_path=report_path,
            log_path=log_path,
            descriptor_old=old_descriptor,
            descriptor_new=new_descriptor,
            source_compat=source_compat,
            binary_compat=binary_compat,
            notes=notes,
        )

    compare_command = [
        tool,
        "-lib",
        library.name,
        "-old",
        str(old_dump),
        "-new",
        str(new_dump),
        "-report-path",
        str(report_path),
    ]
    if not old.libs or not new.libs:
        compare_command.extend(["-headers-only", "-source"])

    result = run_command(compare_command, compare_log)
    write_combined_log(log_path, [old_dump_log, new_dump_log, compare_log])
    log_text = read_text(compare_log)
    source_compat, binary_compat = parse_compatibility(log_text)

    return CompatibilityResult(
        library=library,
        old=old,
        new=new,
        returncode=result.returncode,
        report_path=report_path,
        log_path=log_path,
        descriptor_old=old_descriptor,
        descriptor_new=new_descriptor,
        source_compat=source_compat,
        binary_compat=binary_compat,
        notes=notes,
    )


def markdown_path(path: Path) -> str:
    return str(path)


def write_summary(path: Path, old_ref: str, new_ref: str, results: list[CompatibilityResult]) -> None:
    lines = [
        "# EVerest Library API/ABI Compatibility\n",
        "",
        f"- Old ref: `{old_ref}`",
        f"- New ref: `{new_ref}`",
        "",
        "| Library | Source/API | Binary/ABI | Status | Report | Notes |",
        "|---|---:|---:|---|---|---|",
    ]
    for result in results:
        status = "compatible/no issues reported" if result.returncode == 0 else f"issues or tool error (exit {result.returncode})"
        notes = list(result.notes)
        notes.extend(result.old.notes)
        notes.extend(result.new.notes)
        note_text = "<br>".join(html.escape(note) for note in notes) if notes else ""
        report = markdown_path(result.report_path) if result.report_path.exists() else markdown_path(result.log_path)
        lines.append(
            "| {lib} | {source} | {binary} | {status} | `{report}` | {notes} |".format(
                lib=result.library.name,
                source=result.source_compat or "n/a",
                binary=result.binary_compat or "n/a",
                status=status,
                report=report,
                notes=note_text,
            )
        )

    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "- Source/API compatibility is the important signal for consumers recompiling against the new headers.",
            "- Binary/ABI compatibility is meaningful only when both refs produced shared library artifacts with comparable public headers.",
            "- Non-zero tool exit can mean real incompatibility or a build/analysis problem; inspect the linked log/report before treating it as a regression.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def print_libraries(libraries: list[Library]) -> None:
    for library in libraries:
        marker = "*" if library.name in PUBLIC_LIB_HINTS else " "
        options = ", ".join(library.install_options) if library.install_options else "-"
        print(f"{marker} {library.name:24} {library.relpath} install_options={options}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build two git refs and check API/ABI compatibility of lib/everest libraries.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--old", default="HEAD~1", help="old git ref/tag/commit")
    parser.add_argument("--new", default="HEAD", help="new git ref/tag/commit")
    parser.add_argument("--library", action="append", default=[], help="library name under lib/everest; may be repeated")
    parser.add_argument("--all", action="store_true", help="check all discovered standalone lib/everest libraries")
    parser.add_argument("--list-libraries", action="store_true", help="list discovered libraries and exit")
    parser.add_argument("--output", default="compat-reports", type=Path, help="directory for reports and summary")
    parser.add_argument("--work-dir", type=Path, default=None, help="temporary build/export directory")
    parser.add_argument(
        "--abi-only",
        action="store_true",
        help="reuse existing --work-dir sources/build/install artifacts and rerun only abi-compliance-checker",
    )
    parser.add_argument("--keep-work-dir", action="store_true", help="do not delete the temporary build/export directory")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="parallel build jobs")
    parser.add_argument("--tool", default=shutil.which("abi-compliance-checker") or "abi-compliance-checker")
    parser.add_argument(
        "--cmake-prefix",
        action="append",
        default=[],
        type=Path,
        help="extra CMake package prefix; may be repeated",
    )
    parser.add_argument("--cmake-arg", action="append", default=[], help="extra CMake configure argument; may be repeated")
    parser.add_argument("--old-cmake-arg", action="append", default=[], help="extra CMake argument only for --old")
    parser.add_argument("--new-cmake-arg", action="append", default=[], help="extra CMake argument only for --new")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    repo_root = REPO_ROOT
    libraries = discover_libraries(repo_root)

    if args.list_libraries:
        print_libraries(libraries)
        return 0

    if not shutil.which(args.tool):
        print(f"error: abi-compliance-checker not found: {args.tool}", file=sys.stderr)
        return 2

    selected = select_libraries(repo_root, args.library, args.all)
    output_dir = args.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    cmake_prefixes = unique_existing(
        [
            *(prefix.resolve() for prefix in args.cmake_prefix),
            repo_root.parent / "everest-cmake",
            repo_root / LIB_ROOT / "everest-cmake",
        ]
    )

    owned_tmp: tempfile.TemporaryDirectory[str] | None = None
    if args.abi_only and args.work_dir is None:
        print("error: --abi-only requires --work-dir with existing artifacts", file=sys.stderr)
        return 2

    if args.work_dir is None:
        owned_tmp = tempfile.TemporaryDirectory(prefix="everest-compat-")
        work_dir = Path(owned_tmp.name)
    else:
        work_dir = args.work_dir.resolve()
        work_dir.mkdir(parents=True, exist_ok=True)

    try:
        logs_dir = work_dir / "logs"
        old_export = work_dir / "src" / "old"
        new_export = work_dir / "src" / "new"
        if not args.abi_only:
            export_ref(repo_root, args.old, old_export, logs_dir, work_dir)
            export_ref(repo_root, args.new, new_export, logs_dir, work_dir)
        else:
            missing = [path for path in (old_export, new_export) if not path.exists()]
            if missing:
                print(f"error: --abi-only missing exported source dir(s): {', '.join(str(path) for path in missing)}", file=sys.stderr)
                return 2

        results: list[CompatibilityResult] = []
        for library in selected:
            if args.abi_only:
                print(f"==> {library.name}: reusing existing build artifacts for {args.old}")
                old = reuse_built_library(old_export, library, args.old, "old", work_dir)
                print(f"==> {library.name}: reusing existing build artifacts for {args.new}")
                new = reuse_built_library(new_export, library, args.new, "new", work_dir)
            else:
                print(f"==> {library.name}: building {args.old}")
                old = build_library(
                    old_export,
                    library,
                    args.old,
                    "old",
                    work_dir,
                    args.jobs,
                    [*args.cmake_arg, *args.old_cmake_arg],
                    cmake_prefixes,
                )
                print(f"==> {library.name}: building {args.new}")
                new = build_library(
                    new_export,
                    library,
                    args.new,
                    "new",
                    work_dir,
                    args.jobs,
                    [*args.cmake_arg, *args.new_cmake_arg],
                    cmake_prefixes,
                )
            print(f"==> {library.name}: running abi-compliance-checker")
            results.append(run_compatibility_check(args.tool, library, old, new, output_dir))

        summary_path = output_dir / "summary.md"
        write_summary(summary_path, args.old, args.new, results)
        print(f"\nSummary: {summary_path}")
        print(f"Work dir: {work_dir}")

        if any(result.returncode != 0 for result in results):
            return 1
        return 0
    finally:
        if owned_tmp is not None and not args.keep_work_dir:
            owned_tmp.cleanup()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
