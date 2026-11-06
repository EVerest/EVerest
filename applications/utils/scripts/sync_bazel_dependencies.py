#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
"""
Synchronize the Bazel dependencies with dependencies.yaml.

* a ``bazel_dep`` in MODULE.bazel is set to the matching Bazel Central Registry (BCR)
  version,
* an ``http_archive`` in third-party/bazel/extension.bzl gets a new url, sha256 and
  strip_prefix pointing at the GitHub archive of the tag or commit,
* an ``http_archive`` whose exact version is available in the BCR is replaced by a
  ``bazel_dep`` (``repo_name`` keeps the existing repository name).

Entries that the Bazel build does not use are reported and left alone. A version that
the BCR does not offer is reported and the existing ``bazel_dep`` is kept.

Exit status: 0 when everything is in sync after the run, 1 when mismatches remain
(or, with --dry-run, when changes are pending), 2 on a fatal error.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import yaml
from packaging.version import InvalidVersion, Version

BCR_URL = "https://bcr.bazel.build/modules"
EXTENSION_LABEL = "//third-party/bazel:extension.bzl"
GITHUB_RE = re.compile(r"^(?:https?://|git@)github\.com[:/](?P<owner>[^/]+)/(?P<repo>[^/]+?)(?:\.git)?/?$")
GITHUB_ARCHIVE_RE = re.compile(
    r"^https://github\.com/(?P<owner>[^/]+)/(?P<repo>[^/]+)/archive/(?:refs/tags/)?(?P<ref>[^/]+)\.(?:tar\.gz|zip)$"
)
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
STRING_LITERAL_RE = re.compile(r'^(?:"([^"\\]*)"|\'([^\'\\]*)\')$')
KWARG_RE = re.compile(r"^(\s*)([A-Za-z_][A-Za-z0-9_]*)(\s*=\s*)(.*)$", re.DOTALL)
BCR_SUFFIX_RE = re.compile(r"^(?P<base>.+?)(?:\.bcr\.(?P<rev>\d+))?$")
STRING_ASSIGNMENT_RE = re.compile(r'^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*"[^"\n]*"[ \t]*\n', re.MULTILINE)
TAG_PREFIXES = ("wip-release-", "release-", "tiny-curl-", "curl-", "v")
USER_AGENT = {"User-Agent": "everest-sync-bazel-dependencies"}


def log(message: str) -> None:
    print(message)


def warn(message: str) -> None:
    print(f"WARNING: {message}", file=sys.stderr)


# --------------------------------------------------------------------------------------
# dependencies.yaml
# --------------------------------------------------------------------------------------


@dataclass
class Dependency:
    key: str
    git: str
    git_tag: str
    owner: Optional[str] = None
    repo: Optional[str] = None

    @property
    def slug(self) -> Optional[str]:
        """``owner/repo`` in lower case, or None for non-GitHub URLs."""
        if self.owner is None or self.repo is None:
            return None
        return f"{self.owner}/{self.repo}".lower()

    @property
    def is_commit(self) -> bool:
        return bool(COMMIT_RE.match(self.git_tag))

    @property
    def version(self) -> str:
        """The tag without the prefixes projects like to put in front of the version."""
        version = self.git_tag
        for prefix in TAG_PREFIXES:
            if version.startswith(prefix):
                version = version[len(prefix):]
        if self.key == "libcurl":
            version = version.replace("_", ".")
        return version


def github_owner_repo(url: str) -> Tuple[Optional[str], Optional[str]]:
    match = GITHUB_RE.match(url.strip())
    if not match:
        return None, None
    return match.group("owner"), match.group("repo")


def parse_dependencies(path: Path) -> List[Dependency]:
    with open(path, encoding="utf-8") as dependencies_file:
        content = yaml.safe_load(dependencies_file)
    if not isinstance(content, dict):
        raise ValueError(f"{path} does not contain a mapping of dependencies")
    dependencies = []
    for key, entry in content.items():
        if not isinstance(entry, dict) or "git" not in entry or "git_tag" not in entry:
            warn(f"{key}: entry has no git/git_tag, skipping")
            continue
        owner, repo = github_owner_repo(str(entry["git"]))
        dependencies.append(
            Dependency(key=key, git=str(entry["git"]), git_tag=str(entry["git_tag"]), owner=owner, repo=repo)
        )
    return dependencies


# --------------------------------------------------------------------------------------
# Minimal Starlark call scanner: enough to locate top-level calls and their arguments
# without reformatting anything around them.
# --------------------------------------------------------------------------------------


@dataclass
class Argument:
    start: int
    end: int
    text: str
    keyword: Optional[str] = None
    expr_start: int = 0
    expr_end: int = 0

    @property
    def expr(self) -> str:
        if self.keyword is None:
            return self.text
        return self.text[self.expr_start - self.start:self.expr_end - self.start]


@dataclass
class Call:
    func: str
    start: int
    end: int
    args: List[Argument] = field(default_factory=list)

    @property
    def positional(self) -> List[Argument]:
        return [arg for arg in self.args if arg.keyword is None]

    @property
    def kwargs(self) -> Dict[str, Argument]:
        return {arg.keyword: arg for arg in self.args if arg.keyword is not None}

    def literal(self, keyword: str) -> Optional[str]:
        arg = self.kwargs.get(keyword)
        return string_literal(arg.expr) if arg else None


def string_literal(expr: str) -> Optional[str]:
    match = STRING_LITERAL_RE.match(expr.strip())
    if not match:
        return None
    return match.group(1) if match.group(1) is not None else match.group(2)


def skip_string(text: str, index: int) -> int:
    quote = text[index]
    if text.startswith(quote * 3, index):
        closing = text.find(quote * 3, index + 3)
        return len(text) if closing == -1 else closing + 3
    index += 1
    while index < len(text):
        if text[index] == "\\":
            index += 2
            continue
        if text[index] == quote:
            return index + 1
        index += 1
    return index


def build_argument(text: str, start: int, end: int) -> Optional[Argument]:
    raw = text[start:end]
    if not raw.strip():
        return None
    match = KWARG_RE.match(raw)
    if match and not raw.lstrip().startswith(("'", '"')):
        expr_start = start + match.end(3)
        expr_end = expr_start + len(match.group(4).rstrip())
        return Argument(start=start, end=end, text=raw, keyword=match.group(2), expr_start=expr_start, expr_end=expr_end)
    return Argument(start=start, end=end, text=raw)


def find_calls(text: str, names: Sequence[str]) -> List[Call]:
    """Top-level (depth 0) calls to any of ``names``; the arguments are split on commas."""
    calls: List[Call] = []
    depth = 0
    pending: Optional[Call] = None
    bounds: List[int] = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        if char == "#":
            newline = text.find("\n", index)
            index = length if newline == -1 else newline
            continue
        if char in "\"'":
            index = skip_string(text, index)
            continue
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
            if pending is not None and depth == 0:
                bounds.append(index)
                for arg_start, arg_end in zip(bounds, bounds[1:]):
                    if text[arg_start] == ",":
                        arg_start += 1
                    argument = build_argument(text, arg_start, arg_end)
                    if argument:
                        pending.args.append(argument)
                pending.end = index + 1
                calls.append(pending)
                pending = None
        elif char == "," and pending is not None and depth == 1:
            bounds.append(index)
        elif char.isalpha() or char == "_":
            match = IDENT_RE.match(text, index)
            ident = match.group(0)
            after = match.end()
            while after < length and text[after] in " \t":
                after += 1
            preceded_by_name = index > 0 and (text[index - 1].isalnum() or text[index - 1] in "_.")
            is_call = after < length and text[after] == "("
            if pending is None and depth == 0 and ident in names and is_call and not preceded_by_name:
                pending = Call(func=ident, start=index, end=-1)
                bounds = [after + 1]
                depth = 1
                index = after + 1
                continue
            index = match.end()
            continue
        index += 1
    return calls


class StarlarkFile:
    """A file whose edits are collected as spans and spliced in one go, highest offset first."""

    def __init__(self, path: Path):
        self.path = path
        self.text = path.read_text(encoding="utf-8")
        self.edits: List[Tuple[int, int, str]] = []

    def calls(self, *names: str) -> List[Call]:
        return find_calls(self.text, names)

    def replace(self, start: int, end: int, new_text: str) -> None:
        self.edits.append((start, end, new_text))

    def insert(self, position: int, new_text: str) -> None:
        self.edits.append((position, position, new_text))

    def line_start(self, position: int) -> int:
        return self.text.rfind("\n", 0, position) + 1

    def line_end(self, position: int) -> int:
        newline = self.text.find("\n", position)
        return len(self.text) if newline == -1 else newline + 1

    def indent_of(self, position: int) -> str:
        line = self.text[self.line_start(position):position]
        return line[:len(line) - len(line.lstrip())]

    def remove_statement(self, call: Call) -> None:
        """Remove a top-level call together with the comment lines directly above it."""
        start = self.line_start(call.start)
        while start > 0:
            previous_start = self.line_start(start - 1)
            if not self.text[previous_start:start].strip().startswith("#"):
                break
            start = previous_start
        end = self.line_end(call.end)
        following_blank = self.text[end:self.line_end(end)].strip() == ""
        preceding_blank = start > 0 and self.text[self.line_start(start - 1):start].strip() == ""
        if following_blank and preceding_blank:
            end = self.line_end(end)
        self.replace(start, end, "")

    def remove_argument(self, call: Call, argument: Argument) -> None:
        """Remove ``argument`` including the comma that separates it from its predecessor."""
        index = call.args.index(argument)
        if index == 0:
            raise ValueError("refusing to remove the first argument of a call")
        self.replace(call.args[index - 1].end, argument.end, "")

    def remove_unused_string_variables(self) -> List[str]:
        """Drop ``name = "..."`` assignments nothing references, as left behind by a templated argument turned literal."""
        removed = []
        for match in STRING_ASSIGNMENT_RE.finditer(self.text):
            name = match.group(1)
            if len(re.findall(rf"\b{re.escape(name)}\b", self.text)) == 1:
                self.replace(match.start(), match.end(), "")
                removed.append(name)
        return removed

    @property
    def dirty(self) -> bool:
        return bool(self.edits)

    def apply(self) -> str:
        text = self.text
        for start, end, new_text in sorted(self.edits, key=lambda edit: (edit[0], edit[1]), reverse=True):
            text = text[:start] + new_text + text[end:]
        return text

    def write(self) -> None:
        self.path.write_text(self.apply(), encoding="utf-8")
        self.text = self.path.read_text(encoding="utf-8")
        self.edits.clear()


# --------------------------------------------------------------------------------------
# Bazel Central Registry
# --------------------------------------------------------------------------------------


def bcr_base(version: str) -> str:
    return BCR_SUFFIX_RE.match(version).group("base")


def bcr_revision(version: str) -> int:
    rev = BCR_SUFFIX_RE.match(version).group("rev")
    return int(rev) if rev else 0


class Registry:
    def __init__(self, base_url: str = BCR_URL):
        self.base_url = base_url
        self._metadata: Dict[str, Optional[dict]] = {}

    def metadata(self, module: str) -> Optional[dict]:
        if module not in self._metadata:
            self._metadata[module] = self._fetch(f"{self.base_url}/{module}/metadata.json")
        return self._metadata[module]

    @staticmethod
    def _fetch(url: str) -> Optional[dict]:
        request = urllib.request.Request(url, headers=USER_AGENT)
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            raise
        except json.JSONDecodeError:
            return None

    def versions(self, module: str) -> List[str]:
        metadata = self.metadata(module) or {}
        yanked = set(metadata.get("yanked_versions", {}))
        return [version for version in metadata.get("versions", []) if version not in yanked]

    def repositories(self, module: str) -> List[str]:
        return [str(repo).lower() for repo in (self.metadata(module) or {}).get("repository", [])]

    def match_version(self, module: str, version: str) -> Optional[str]:
        """The registry version for ``version``, preferring the newest ``.bcr.N`` revision."""
        candidates = [candidate for candidate in self.versions(module) if bcr_base(candidate) == version]
        return max(candidates, key=bcr_revision) if candidates else None

    def find_module(self, dependency: Dependency) -> Optional[str]:
        """Guess the registry module for a dependency and verify it via the module's repository."""
        candidates = [dependency.key, dependency.key.removeprefix("lib")]
        if dependency.repo:
            repo = dependency.repo.lower()
            candidates += [repo, repo.replace("-", "_"), f"{dependency.owner}_{repo}".lower()]
        seen = set()
        for candidate in candidates:
            if not candidate or candidate in seen:
                continue
            seen.add(candidate)
            if self.metadata(candidate) is None:
                continue
            repositories = self.repositories(candidate)
            if dependency.slug and f"github:{dependency.slug}" in repositories:
                return candidate
            if candidate == dependency.key and not any(repo.startswith("github:") for repo in repositories):
                return candidate
        return None


# --------------------------------------------------------------------------------------
# git and archives
# --------------------------------------------------------------------------------------


def resolve_tag(git_url: str, tag: str) -> Optional[str]:
    """Commit hash a tag points to, following annotated tags to the tagged commit."""
    try:
        result = subprocess.run(
            ["git", "ls-remote", "--tags", git_url, f"refs/tags/{tag}", f"refs/tags/{tag}^{{}}"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
            timeout=120,
        )
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
        return None
    resolved: Dict[str, str] = {}
    for line in result.stdout.decode("utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) == 2:
            resolved[parts[1]] = parts[0]
    return resolved.get(f"refs/tags/{tag}^{{}}") or resolved.get(f"refs/tags/{tag}")


def github_archive_url(dependency: Dependency) -> str:
    base = f"https://github.com/{dependency.owner}/{dependency.repo}/archive"
    if dependency.is_commit:
        return f"{base}/{dependency.git_tag}.tar.gz"
    return f"{base}/refs/tags/{dependency.git_tag}.tar.gz"


def fetch_archive(url: str) -> Tuple[str, str]:
    """Download an archive and return its sha256 and the top-level directory it unpacks to."""
    request = urllib.request.Request(url, headers=USER_AGENT)
    digest = hashlib.sha256()
    with tempfile.NamedTemporaryFile(suffix=".tar.gz") as archive:
        with urllib.request.urlopen(request, timeout=300) as response:
            while True:
                chunk = response.read(1 << 20)
                if not chunk:
                    break
                digest.update(chunk)
                archive.write(chunk)
        archive.flush()
        with tarfile.open(archive.name) as tar:
            first = next(iter(tar), None)
    if first is None:
        raise ValueError(f"{url} is an empty archive")
    return digest.hexdigest(), first.name.split("/")[0]


# --------------------------------------------------------------------------------------
# Sync
# --------------------------------------------------------------------------------------


@dataclass
class Report:
    in_sync: List[str] = field(default_factory=list)
    updated: List[str] = field(default_factory=list)
    mismatched: List[str] = field(default_factory=list)
    unused: List[str] = field(default_factory=list)

    def print(self, dry_run: bool) -> None:
        sections = [
            ("Would update" if dry_run else "Updated", self.updated),
            ("Already in sync", self.in_sync),
            ("Still mismatched, needs manual attention", self.mismatched),
            ("Not used by the Bazel build", self.unused),
        ]
        for title, lines in sections:
            if lines:
                log(f"\n{title}:")
                for line in lines:
                    log(f"  {line}")


class Synchronizer:
    def __init__(self, root: Path, registry: Registry, dry_run: bool):
        self.root = root
        self.registry = registry
        self.dry_run = dry_run
        self.module = StarlarkFile(root / "MODULE.bazel")
        self.extension = StarlarkFile(root / "third-party" / "bazel" / "extension.bzl")
        self.report = Report()
        self.bazel_deps: Dict[str, Call] = {}
        for call in self.module.calls("bazel_dep"):
            name = call.literal("name")
            if name:
                self.bazel_deps[name] = call
        self.archives: Dict[str, Call] = {}
        for call in self.extension.calls("http_archive", "maybe"):
            if call.func == "maybe" and not (call.positional and call.positional[0].expr.strip() == "http_archive"):
                continue
            name = call.literal("name")
            if name:
                self.archives[name] = call
        self.module_by_slug = self._module_by_slug()
        self.archive_by_slug = self._archive_by_slug()

    def _module_by_slug(self) -> Dict[str, str]:
        result = {}
        for name in self.bazel_deps:
            for repo in self.registry.repositories(name):
                if repo.startswith("github:"):
                    result[repo[len("github:"):].strip("/")] = name
        return result

    def _archive_by_slug(self) -> Dict[str, str]:
        result = {}
        for name, call in self.archives.items():
            for url in self.archive_urls(call):
                match = GITHUB_ARCHIVE_RE.match(url)
                if match:
                    result[f"{match.group('owner')}/{match.group('repo')}".lower()] = name
        return result

    @staticmethod
    def archive_urls(call: Call) -> List[str]:
        url = call.literal("url")
        if url:
            return [url]
        urls = call.kwargs.get("urls")
        if urls:
            parts = urls.expr.strip().strip("[]").split(",")
            return [literal for literal in (string_literal(part) for part in parts) if literal]
        return []

    def run(self, dependencies: List[Dependency]) -> None:
        for dependency in dependencies:
            slug = dependency.slug or ""
            module_name = self.module_by_slug.get(slug)
            archive_name = self.archive_by_slug.get(slug) or (dependency.key if dependency.key in self.archives else None)
            if module_name:
                self.sync_bazel_dep(dependency, module_name)
            elif archive_name:
                self.sync_archive(dependency, archive_name)
            else:
                self.report_unused(dependency)

    # -- bazel_dep -----------------------------------------------------------------------

    def sync_bazel_dep(self, dependency: Dependency, module_name: str) -> None:
        call = self.bazel_deps[module_name]
        version_arg = call.kwargs.get("version")
        current = string_literal(version_arg.expr) if version_arg else None
        if current is None:
            self.report.mismatched.append(f"{dependency.key}: bazel_dep {module_name} has no literal version, cannot update")
            return
        if bcr_base(current) == dependency.version:
            self.report.in_sync.append(f"{dependency.key}: bazel_dep {module_name} {current}")
            return
        target = self.registry.match_version(module_name, dependency.version)
        if target is None:
            available = ", ".join(self.registry.versions(module_name)[-5:]) or "none"
            self.report.mismatched.append(
                f"{dependency.key}: BCR has no {module_name} {dependency.version}, keeping bazel_dep {current} "
                f"(newest in BCR: {available})"
            )
            return
        self.module.replace(version_arg.expr_start, version_arg.expr_end, f'"{target}"')
        note = self.downgrade_note(current, target)
        self.report.updated.append(f"{dependency.key}: bazel_dep {module_name} {current} -> {target}{note}")

    @staticmethod
    def downgrade_note(current: str, target: str) -> str:
        try:
            if Version(bcr_base(target)) < Version(bcr_base(current)):
                return " (DOWNGRADE)"
        except InvalidVersion:
            pass
        return ""

    # -- http_archive --------------------------------------------------------------------

    def sync_archive(self, dependency: Dependency, archive_name: str) -> None:
        module_name = self.registry.find_module(dependency)
        if module_name:
            target = self.registry.match_version(module_name, dependency.version)
            if target:
                self.migrate_to_bazel_dep(dependency, archive_name, module_name, target)
                return
            log(f"{dependency.key}: BCR module {module_name} exists but not at {dependency.version}, keeping http_archive")
        call = self.archives[archive_name]
        if self.archive_is_current(dependency, call):
            self.report.in_sync.append(f"{dependency.key}: http_archive {archive_name} at {dependency.git_tag}")
            return
        if not dependency.owner:
            self.report.mismatched.append(
                f"{dependency.key}: {dependency.git} is not on GitHub, update http_archive {archive_name} by hand"
            )
            return
        url = github_archive_url(dependency)
        if self.dry_run:
            self.report.updated.append(f"{dependency.key}: http_archive {archive_name} -> {url}")
            return
        log(f"{dependency.key}: fetching {url}")
        sha256, strip_prefix = fetch_archive(url)
        kwargs = call.kwargs
        indent = self.extension.indent_of(kwargs["name"].expr_start)
        if "urls" in kwargs:
            anchor = kwargs["urls"]
            self.extension.replace(anchor.expr_start, anchor.expr_end, f'["{url}"]')
        elif "url" in kwargs:
            anchor = kwargs["url"]
            self.extension.replace(anchor.expr_start, anchor.expr_end, f'"{url}"')
        else:
            anchor = kwargs["name"]
            self.extension.insert(anchor.expr_end, f',\n{indent}url = "{url}"')
        for keyword, value in (("sha256", sha256), ("strip_prefix", strip_prefix)):
            if keyword in kwargs:
                self.extension.replace(kwargs[keyword].expr_start, kwargs[keyword].expr_end, f'"{value}"')
            else:
                self.extension.insert(anchor.expr_end, f',\n{indent}{keyword} = "{value}"')
        self.warn_about_build_file(dependency, call)
        self.report.updated.append(f"{dependency.key}: http_archive {archive_name} -> {dependency.git_tag}")

    def archive_is_current(self, dependency: Dependency, call: Call) -> bool:
        urls = self.archive_urls(call)
        if not urls:
            return False
        for url in urls:
            match = GITHUB_ARCHIVE_RE.match(url)
            if not match:
                return dependency.version in url.rsplit("/", 1)[-1]
            ref = match.group("ref")
            if ref == dependency.git_tag:
                return True
            if COMMIT_RE.match(ref) and not dependency.is_commit:
                return resolve_tag(dependency.git, dependency.git_tag) == ref
        return False

    def warn_about_build_file(self, dependency: Dependency, call: Call) -> None:
        build_file = call.literal("build_file")
        if not build_file or not build_file.startswith("@everest-core//"):
            return
        package, _, target = build_file[len("@everest-core//"):].partition(":")
        path = self.root / package / target
        if not path.exists():
            return
        old_versions = {version for url in self.archive_urls(call) for version in re.findall(r"\d+\.\d+(?:\.\d+)?", url)}
        content = path.read_text(encoding="utf-8")
        for version in old_versions:
            if version in content:
                warn(f"{dependency.key}: {path.relative_to(self.root)} mentions {version}, check it by hand")

    def migrate_to_bazel_dep(self, dependency: Dependency, archive_name: str, module_name: str, target: str) -> None:
        call = self.archives[archive_name]
        self.report.updated.append(
            f"{dependency.key}: http_archive {archive_name} -> bazel_dep {module_name} {target}; "
            f"check that consumers of @{archive_name} still find their targets in the registry module"
        )
        if self.dry_run:
            return
        self.extension.remove_statement(call)
        extension_variable = self.extension_variable()
        for use_repo in self.module.calls("use_repo"):
            if not use_repo.positional or use_repo.positional[0].expr.strip() != extension_variable:
                continue
            for argument in use_repo.positional[1:]:
                if string_literal(argument.expr) == archive_name:
                    self.module.remove_argument(use_repo, argument)
        repo_name = f', repo_name = "{archive_name}"' if archive_name != module_name else ""
        last = max(self.bazel_deps.values(), key=lambda dep: dep.end)
        self.module.insert(last.end, f'\nbazel_dep(name = "{module_name}", version = "{target}"{repo_name})')
        build_file = call.literal("build_file")
        if build_file:
            log(f"{dependency.key}: {build_file} is no longer referenced and can be removed")

    def extension_variable(self) -> Optional[str]:
        pattern = r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*use_extension\(\s*\"" + re.escape(EXTENSION_LABEL) + r"\""
        match = re.search(pattern, self.module.text, re.MULTILINE)
        return match.group(1) if match else None

    # -- not used ------------------------------------------------------------------------

    def report_unused(self, dependency: Dependency) -> None:
        note = ""
        module_name = self.registry.find_module(dependency)
        if module_name:
            target = self.registry.match_version(module_name, dependency.version)
            if target:
                note = f" (BCR offers {module_name} {target})"
            else:
                note = f" (BCR offers {module_name}, but not {dependency.version})"
        self.report.unused.append(f"{dependency.key}{note}")

    # -- output --------------------------------------------------------------------------

    def write(self) -> List[Path]:
        written = []
        for starlark_file in (self.module, self.extension):
            if not starlark_file.dirty:
                continue
            starlark_file.write()
            removed = starlark_file.remove_unused_string_variables()
            if removed:
                starlark_file.write()
                log(f"{starlark_file.path.name}: removed unused variable(s) {', '.join(removed)}")
            written.append(starlark_file.path)
        return written


def format_files(paths: List[Path]) -> None:
    buildifier = shutil.which("buildifier")
    if not buildifier:
        log("buildifier not found on PATH, formatting skipped")
        return
    subprocess.run([buildifier] + [str(path) for path in paths], check=False)


def main() -> int:
    default_root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--input",
        dest="dependencies",
        type=Path,
        default=default_root / "dependencies.yaml",
        help="Path to dependencies.yaml (default: the one at the repository root)",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=default_root,
        help="Repository root holding MODULE.bazel and third-party/bazel/extension.bzl",
    )
    parser.add_argument("--dry-run", action="store_true", help="Report what would change without downloading or writing")
    parser.add_argument("--no-format", action="store_true", help="Do not run buildifier on the written files")
    args = parser.parse_args()

    dependencies_path = args.dependencies.expanduser().resolve()
    root = args.repo_root.expanduser().resolve()
    try:
        dependencies = parse_dependencies(dependencies_path)
    except (OSError, yaml.YAMLError, ValueError) as error:
        print(f"Error reading {dependencies_path}: {error}", file=sys.stderr)
        return 2
    try:
        synchronizer = Synchronizer(root, Registry(), args.dry_run)
        synchronizer.run(dependencies)
        written = [] if args.dry_run else synchronizer.write()
    except (OSError, urllib.error.URLError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 2
    if written and not args.no_format:
        format_files(written)
    synchronizer.report.print(args.dry_run)
    if written:
        log("\nWritten: " + ", ".join(str(path.relative_to(root)) for path in written))
    if synchronizer.report.mismatched or (args.dry_run and synchronizer.report.updated):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
