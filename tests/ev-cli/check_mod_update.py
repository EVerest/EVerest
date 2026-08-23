#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Check that `ev-cli mod update --force` is a no-op for every module.

Generated module files (CMakeLists.txt, <Module>.hpp and the interface
implementation headers) are supposed to be reproducible from the manifest, the
interface definitions and the ev-cli templates.  Whenever they are not - stale
templates, hand-edited generated sections, invalid or hand-invented
auto-generation markers - the next developer who runs `ev-cli mod update` gets
unrelated changes mixed into their diff.

This checker force-updates one module at a time and reports whether the working
tree stayed clean.  The working tree is restored after every module, so it is
safe to run on a checkout you are working in (a clean tree is required, see
--allow-dirty).

Note that `mod update` never overwrites .cpp files (they use the
'update-if-non-existent' strategy), so no implementation code is at risk.

Findings are graded, and the exit code reflects the worst one:

  0  every module regenerates to a zero diff
  1  some modules only need a template refresh (a template version bump and
     whatever the new template adds) - nothing hand-written is at stake
  2  some modules would lose code written outside the `ev@<uuid>` blocks, or
     ev-cli refuses to update them at all
  3  the checker could not run
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

# Truncate per-module diffs in the report, some are huge.
MAX_DIFF_LINES = 40
# The GitHub job summary is capped at 1 MiB, keep the diff section well below that.
MAX_DIFF_SECTION_CHARS = 200000


class Result:
    CLEAN = 'clean'
    DIRTY = 'dirty'
    ERROR = 'error'
    SKIPPED = 'skipped'


class Exit:
    CLEAN = 0
    TEMPLATE_ONLY = 1
    CRITICAL = 2
    UNUSABLE = 3


class Kind:
    # The diff only bumps the ev-cli template version and adds what the new template generates,
    # so regenerating the module is safe (it may still need matching definitions in the .cpp).
    TEMPLATE_ONLY = 'template-only'
    # The diff removes hand-written lines: code that lives outside the `ev@<uuid>` blocks and
    # would be lost on the next `ev-cli mod update`.
    CONTENT_LOSS = 'content-loss'


TEMPLATE_VERSION_RE = re.compile(r'^-\s*(//|#) template version \d+\s*$')


def classify(diff: str):
    """Tell a plain template refresh apart from a diff that drops hand-written code."""
    for line in diff.splitlines():
        if not line.startswith('-') or line.startswith('---'):
            continue
        if line.strip() == '-' or TEMPLATE_VERSION_RE.match(line):
            continue
        return Kind.CONTENT_LOSS
    return Kind.TEMPLATE_ONLY


def run(cmd, cwd, check=False):
    return subprocess.run(cmd, cwd=cwd, check=check, capture_output=True, text=True)


def git(repo, *args):
    return run(['git', *args], cwd=repo)


def discover_modules(repo: Path):
    """Return (cpp_modules, skipped) as lists of (name, reason)."""
    cpp_modules = []
    skipped = []
    for manifest in sorted((repo / 'modules').rglob('manifest.yaml')):
        mod_dir = manifest.parent
        rel = mod_dir.relative_to(repo / 'modules').as_posix()
        name = mod_dir.name
        if (mod_dir / f'{name}.hpp').exists() or (mod_dir / f'{name}.cpp').exists():
            cpp_modules.append(rel)
        elif (mod_dir / 'Cargo.toml').exists():
            skipped.append((rel, 'Rust module'))
        elif list(mod_dir.glob('*.py')):
            skipped.append((rel, 'Python module'))
        else:
            skipped.append((rel, 'no C++ sources found'))
    return cpp_modules, skipped


def restore(repo: Path, rel_mod: str):
    """Undo everything ev-cli did to a module directory."""
    mod_path = f'modules/{rel_mod}'
    git(repo, 'checkout', '--', mod_path)
    git(repo, 'clean', '-fdq', '--', mod_path)


def check_module(repo: Path, ev_cli: str, rel_mod: str, everest_dir: Path):
    mod_path = f'modules/{rel_mod}'
    cmd = [
        ev_cli, 'mod', 'update', '--force', rel_mod,
        '--work-dir', str(repo),
        '--everest-dir', str(everest_dir),
        '--schemas-dir', str(everest_dir / 'lib' / 'everest' / 'framework' / 'schemas'),
        '--clang-format-file', str(repo),
    ]
    proc = run(cmd, cwd=repo)

    if proc.returncode != 0:
        restore(repo, rel_mod)
        tail = (proc.stderr or proc.stdout).strip().splitlines()
        return {
            'module': rel_mod,
            'result': Result.ERROR,
            'kind': None,
            'detail': 'ev-cli failed: ' + (tail[-1] if tail else f'exit code {proc.returncode}'),
            'diff': '\n'.join(tail[-MAX_DIFF_LINES:]),
        }

    status = git(repo, 'status', '--porcelain', '--', mod_path).stdout.strip()
    if not status:
        return {'module': rel_mod, 'result': Result.CLEAN, 'kind': None, 'detail': '', 'diff': ''}

    files = [line[3:] for line in status.splitlines()]
    full_diff = git(repo, 'diff', '--', mod_path).stdout
    kind = classify(full_diff)
    diff = full_diff.splitlines()
    if len(diff) > MAX_DIFF_LINES:
        diff = diff[:MAX_DIFF_LINES] + [f'... ({len(diff) - MAX_DIFF_LINES} more lines)']
    restore(repo, rel_mod)
    return {
        'module': rel_mod,
        'result': Result.DIRTY,
        'kind': kind,
        'detail': ', '.join(Path(f).name for f in files),
        'diff': '\n'.join(diff),
    }


def verdict(content_loss, errors, template_only):
    if content_loss or errors:
        return (f'{len(content_loss) + len(errors)} module(s) would lose hand-written code or '
                f'cannot be updated at all')
    if template_only:
        return f'{len(template_only)} module(s) need a template refresh, nothing hand-written at stake'
    return 'every module regenerates to a zero diff'


def write_markdown(path: Path, results, skipped, ev_cli_version, clang_format_version):
    dirty = [r for r in results if r['result'] == Result.DIRTY]
    errors = [r for r in results if r['result'] == Result.ERROR]
    clean = [r for r in results if r['result'] == Result.CLEAN]
    content_loss = [r for r in dirty if r['kind'] == Kind.CONTENT_LOSS]
    template_only = [r for r in dirty if r['kind'] == Kind.TEMPLATE_ONLY]

    out = []
    out.append('## `ev-cli mod update --force` cleanliness')
    out.append('')
    out.append(f'- {len(clean)} clean')
    out.append(f'- {len(content_loss)} would lose hand-written code')
    out.append(f'- {len(template_only)} only need a template refresh')
    out.append(f'- {len(errors)} fail to run')
    out.append(f'- {len(skipped)} skipped (not a C++ module)')
    out.append('')
    out.append(f'**{verdict(content_loss, errors, template_only)}.**')
    out.append('')
    out.append(f'ev-cli: `{ev_cli_version}` &middot; clang-format: `{clang_format_version}`')
    out.append('')

    if errors:
        out.append('### Modules where ev-cli fails')
        out.append('')
        out.append('| Module | Error |')
        out.append('| --- | --- |')
        for r in errors:
            out.append(f'| `{r["module"]}` | {r["detail"]} |')
        out.append('')

    if content_loss:
        out.append('### Modules that would lose hand-written code')
        out.append('')
        out.append('These carry code outside the `ev@<uuid>` blocks, which `ev-cli mod update` '
                   'discards.')
        out.append('')
        out.append('| Module | Changed files |')
        out.append('| --- | --- |')
        for r in content_loss:
            out.append(f'| `{r["module"]}` | {r["detail"]} |')
        out.append('')

    if template_only:
        out.append('### Modules that only need a template refresh')
        out.append('')
        out.append('| Module | Changed files |')
        out.append('| --- | --- |')
        for r in template_only:
            out.append(f'| `{r["module"]}` | {r["detail"]} |')
        out.append('')

    if dirty or errors:
        out.append('<details><summary>Diffs</summary>')
        out.append('')
        budget = MAX_DIFF_SECTION_CHARS
        omitted = 0
        for r in content_loss + errors + template_only:
            block = [f'#### `{r["module"]}`', '', '```diff', r['diff'], '```', '']
            size = sum(len(line) + 1 for line in block)
            if size > budget:
                omitted += 1
                continue
            budget -= size
            out.extend(block)
        if omitted:
            out.append(f'_Diffs for {omitted} more module(s) omitted, see the report artifact._')
            out.append('')
        out.append('</details>')
        out.append('')

    if not dirty and not errors:
        out.append('All C++ modules regenerate cleanly. :tada:')
        out.append('')

    path.write_text('\n'.join(out))


def tool_version(cmd):
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True)
        return (proc.stdout or proc.stderr).strip().splitlines()[0]
    except (OSError, IndexError):
        return 'unknown'


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--repo', help='path to the EVerest checkout (default: git toplevel)')
    parser.add_argument('--ev-cli', default='ev-cli', help='ev-cli executable to use (default: ev-cli)')
    parser.add_argument('--everest-dir', help='everest directory holding the interface definitions '
                                              '(default: same as --repo)')
    parser.add_argument('modules', nargs='*', help='modules to check, relative to modules/ '
                                                   '(default: all C++ modules)')
    parser.add_argument('--markdown', help='write a markdown report to this file')
    parser.add_argument('--json', dest='json_out', help='write the raw results to this json file')
    parser.add_argument('--allow-dirty', action='store_true',
                        help='run even though the working tree has uncommitted changes')
    args = parser.parse_args()

    if args.repo:
        repo = Path(args.repo).resolve()
    else:
        toplevel = run(['git', 'rev-parse', '--show-toplevel'], cwd=Path.cwd())
        if toplevel.returncode != 0:
            parser.error('not inside a git repository, pass --repo')  # exits with 2
        repo = Path(toplevel.stdout.strip())
    everest_dir = Path(args.everest_dir).resolve() if args.everest_dir else repo

    dirty_tree = git(repo, 'status', '--porcelain', '--', 'modules').stdout.strip()
    if dirty_tree and not args.allow_dirty:
        print('modules/ has uncommitted changes - this checker restores files by discarding them.',
              file=sys.stderr)
        print('Commit or stash your work first, or pass --allow-dirty.', file=sys.stderr)
        return Exit.UNUSABLE

    all_modules, skipped = discover_modules(repo)
    if args.modules:
        modules = args.modules
        skipped = []
    else:
        modules = all_modules

    ev_cli_version = tool_version([args.ev_cli, '--version'])
    clang_format_version = tool_version(['clang-format', '--version'])
    print(f'Using {ev_cli_version} and {clang_format_version}')
    print(f'Checking {len(modules)} module(s), skipping {len(skipped)}')

    results = []
    for i, rel_mod in enumerate(modules, start=1):
        result = check_module(repo, args.ev_cli, rel_mod, everest_dir)
        results.append(result)
        marker = {Result.CLEAN: 'ok  ', Result.DIRTY: 'DIFF', Result.ERROR: 'FAIL'}[result['result']]
        if result['kind'] == Kind.CONTENT_LOSS:
            marker = 'LOSS'
        detail = f'  ({result["detail"]})' if result['detail'] else ''
        print(f'[{i}/{len(modules)}] {marker} {rel_mod}{detail}', flush=True)

    dirty = [r for r in results if r['result'] == Result.DIRTY]
    errors = [r for r in results if r['result'] == Result.ERROR]
    content_loss = [r for r in dirty if r['kind'] == Kind.CONTENT_LOSS]
    template_only = [r for r in dirty if r['kind'] == Kind.TEMPLATE_ONLY]

    print()
    print(f'clean: {len(results) - len(dirty) - len(errors)}, '
          f'content loss: {len(content_loss)}, template refresh: {len(template_only)}, '
          f'failed: {len(errors)}, skipped: {len(skipped)}')
    print(verdict(content_loss, errors, template_only))

    if args.markdown:
        write_markdown(Path(args.markdown), results, skipped, ev_cli_version, clang_format_version)
        print(f'markdown report written to {args.markdown}')
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(
            {'results': results, 'skipped': [{'module': m, 'reason': r} for m, r in skipped]}, indent=2))
        print(f'json report written to {args.json_out}')

    if content_loss or errors:
        return Exit.CRITICAL
    if template_only:
        return Exit.TEMPLATE_ONLY
    return Exit.CLEAN


if __name__ == '__main__':
    sys.exit(main())
