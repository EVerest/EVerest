#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Snapshot and compare the EVerest public API contract.

    compat_contract.py snapshot (--ref REF | --tree DIR) -o SNAPSHOT.json
    compat_contract.py diff OLD.json NEW.json [options]
    compat_contract.py check --base REF [--head REF | --tree DIR] [options]
    compat_contract.py baseline [--target-branch BRANCH | --tag TAG]

Exit code 0: every breaking or unclassified change is accepted by the registry of the release or by
the allowlist. 1: such changes remain. 2: usage error.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from contract import allowlist as allowlist_module  # type: ignore
    from contract import release, report, rules, snapshot  # type: ignore
else:
    from . import allowlist as allowlist_module
    from . import release, report, rules, snapshot

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]
DEFAULT_ALLOWLIST = HERE / "allowlist.yaml"
LATEST_RELEASE = "latest-release"


def _add_diff_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--allowlist", type=Path, default=DEFAULT_ALLOWLIST, help="allowlist YAML (default: %(default)s)")
    parser.add_argument("--no-allowlist", action="store_true", help="ignore the allowlist; the registry of the release still applies")
    parser.add_argument(
        "--patch-release",
        action="store_true",
        help="the checked tree continues the release line of the baseline: only experimental components may "
        "change, the allowlist does not apply",
    )
    parser.add_argument(
        "--target-release",
        help="release the checked tree becomes, e.g. 2027.04.0: a deprecated component may change only if its "
        "earliest removal release is not later (default: the CMake project version of the checked tree)",
    )
    parser.add_argument("--report", type=Path, help="write a Markdown report to this file")
    parser.add_argument("--json", type=Path, dest="json_out", help="write the findings as JSON to this file")
    parser.add_argument("--show-non-breaking", action="store_true", help="list non-breaking findings in the report")
    parser.add_argument("--quiet", action="store_true", help="print only the summary line")


def _run_diff(old: dict, new: dict, args: argparse.Namespace, released: dict | None = None) -> int:
    """``released`` is the snapshot of the release that deployments run, when it is not the
    baseline itself. Its registry decides which exceptions apply, see registry.py, and only
    changes to what it ships count."""
    if not (new.get("modules") and new.get("asyncapi")):
        raise ValueError(f"{new.get('label', 'the checked tree')} has no modules or no API specifications to check")
    findings = rules.diff_snapshots(old, new)
    unreleased: list = []
    if released is not None:
        findings, unreleased = rules.split_unreleased(findings, rules.diff_snapshots(released, new))
    registry = (released if released is not None else old).get("registry") or {}
    entries = [] if args.no_allowlist else allowlist_module.load_allowlist(args.allowlist)
    result = allowlist_module.apply_exceptions(
        findings, registry, entries, args.target_release or new.get("version"), args.patch_release
    )

    # Against another ref than the release, such as a merge base, most entries have nothing to accept.
    unused = [] if released is not None or args.patch_release else [entry.pointer for entry in result.unused]
    old_label, new_label = old.get("label", "old"), new.get("label", "new")
    markdown = report.render_markdown(
        old_label, new_label, result.remaining, result.accepted, args.show_non_breaking, result.not_due, unreleased, unused
    )
    if args.report:
        args.report.write_text(markdown + "\n")
    if args.json_out:
        args.json_out.write_text(
            report.render_json(old_label, new_label, result.remaining, result.accepted, result.not_due, unreleased) + "\n"
        )
    if not args.quiet:
        print(markdown)
    print(report.summary_line(result.remaining, len(result.accepted)))

    groups = rules.by_classification(result.remaining)
    return 1 if groups[rules.BREAKING] or groups[rules.UNCLASSIFIED] else 0


def _snapshot_source(ref: str | None, tree: Path | None, repo: Path) -> dict:
    if tree is not None:
        return snapshot.snapshot_tree(tree, label=str(tree))
    return snapshot.snapshot_ref(repo, ref)


def cmd_snapshot(args: argparse.Namespace) -> int:
    result = _snapshot_source(args.ref, args.tree, args.repo)
    snapshot.write_snapshot(result, args.output)
    return 0


def cmd_diff(args: argparse.Namespace) -> int:
    return _run_diff(snapshot.read_snapshot(args.old), snapshot.read_snapshot(args.new), args)


def cmd_check(args: argparse.Namespace) -> int:
    old = snapshot.snapshot_ref(args.repo, args.base)
    if args.tree is not None:
        new = snapshot.snapshot_tree(args.tree, label=f"tree:{args.tree}")
    else:
        new = snapshot.snapshot_ref(args.repo, args.head)
    return _run_diff(old, new, args, _released(args))


def _released(args: argparse.Namespace) -> dict | None:
    ref = args.released
    if ref is None:
        return None
    if ref == LATEST_RELEASE:
        baseline = release.release_baseline(release.git_tags(args.repo))
        if baseline is None:
            raise ValueError("the repository has no release tag")
        ref = baseline.tag
    return snapshot.snapshot_ref(args.repo, ref)


def cmd_baseline(args: argparse.Namespace) -> int:
    baseline = release.release_baseline(release.git_tags(args.repo), args.target_branch, args.tag)
    if baseline is None:
        raise ValueError("the repository has no release tag to compare with")
    print(f"release_tag={baseline.tag}")
    print(f"patch_release={str(baseline.patch_release).lower()}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=REPO_ROOT, help="git repository (default: %(default)s)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_snapshot = sub.add_parser("snapshot", help="write the contract snapshot of a ref or tree")
    source = p_snapshot.add_mutually_exclusive_group(required=True)
    source.add_argument("--ref", help="git ref to snapshot")
    source.add_argument("--tree", type=Path, help="source tree to snapshot")
    p_snapshot.add_argument("-o", "--output", type=Path, required=True)
    p_snapshot.set_defaults(func=cmd_snapshot)

    p_diff = sub.add_parser("diff", help="compare two snapshots")
    p_diff.add_argument("old", type=Path)
    p_diff.add_argument("new", type=Path)
    _add_diff_options(p_diff)
    p_diff.set_defaults(func=cmd_diff)

    p_check = sub.add_parser("check", help="compare a base ref with a head ref or the working tree")
    p_check.add_argument("--base", required=True, help="baseline git ref, e.g. the latest release tag")
    head = p_check.add_mutually_exclusive_group()
    head.add_argument("--head", default="HEAD", help="git ref to check (default: HEAD)")
    head.add_argument("--tree", type=Path, help="working tree to check instead of a ref")
    p_check.add_argument(
        "--released",
        metavar="REF",
        help=f"the release that deployments run, when the baseline is another ref such as a merge base; "
        f"'{LATEST_RELEASE}' names the latest release tag. Its registry of deprecated and experimental "
        "components applies, and a change to something it does not ship is reported without counting",
    )
    _add_diff_options(p_check)
    p_check.set_defaults(func=cmd_check)

    p_baseline = sub.add_parser("baseline", help="print the release a tree is compared with, as key=value lines")
    developed_as = p_baseline.add_mutually_exclusive_group()
    developed_as.add_argument("--target-branch", help="branch the tree is developed on, e.g. main or stable/2026.10")
    developed_as.add_argument("--tag", help="release the tree is tagged as, e.g. 2026.10.1")
    p_baseline.set_defaults(func=cmd_baseline)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
