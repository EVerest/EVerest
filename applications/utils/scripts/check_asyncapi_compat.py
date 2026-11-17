#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Classifies the changes to the EVerest AsyncAPI specifications between a git ref and the
working tree, and suggests a version for each specification.

Rules follow docs/source/project/releases/breaking-changes.rst. Payloads are judged from
the API client's point of view: operations with action 'receive' carry payloads EVerest
sends (the client reads them), 'send' carries payloads the client writes. Operations are
matched by action and channel address, so renaming an operation or channel id is not a
change. Reply channels have no address of their own and are matched through the request
that names them. $refs are resolved across files, so a change to a shared schema is
reported for every specification that reaches it.

Changes that only show in prose (behavior, semantics) are not detected; findings marked
'review' and 'breaking' need a human look, since a spec fix that aligns the specification
with an unchanged implementation reads like a breaking change. The report names the
commit that introduced each of them. Once reviewed as not breaking, that commit goes into
the acknowledgement file (see --acknowledged): the breaking and review findings the commit
produces against its parent are then reported as 'acknowledged' and count as additive.
Later changes at the same place are not covered. --acknowledge prints entries to fill in.

A changed specification needs a version at least as high as the suggested one; a bump for
an earlier, smaller change in the same release does not suffice. --update-versions writes
the suggested version into info.version of every changed specification below it that has
no unresolved findings.

Exits with 1 if a specification has breaking or review findings that are not
acknowledged, or a changed specification is below its suggested version.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import yaml

BREAKING, REVIEW, ACKNOWLEDGED = "breaking", "review", "acknowledged"
ADDITIVE, NOTE, DOCS = "additive", "note", "docs"
SEVERITY_ORDER = [BREAKING, REVIEW, ACKNOWLEDGED, ADDITIVE, NOTE, DOCS]
UNRESOLVED = (BREAKING, REVIEW)
FULL_HASH = re.compile(r"[0-9a-f]{40}")
DOC_KEYS = {"description", "title", "summary", "examples", "example", "name", "tags", "externalDocs", "license"}
SCHEMA_KEYS = DOC_KEYS | {
    "$ref", "type", "enum", "const", "default", "deprecated", "format", "pattern",
    "properties", "required", "additionalProperties", "items", "oneOf", "anyOf", "allOf",
    "minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum",
    "minLength", "maxLength", "minItems", "maxItems",
}
LOWER_BOUNDS = ("minimum", "exclusiveMinimum", "minLength", "minItems")
UPPER_BOUNDS = ("maximum", "exclusiveMaximum", "maxLength", "maxItems")
TYPE_WIDENS = {"integer": {"number"}}
DEFAULT_REPO = Path(__file__).resolve().parents[3]


class Tree:
    """The specifications of one revision, either a git ref or the working tree (ref None)."""

    def __init__(self, repo, ref, api_dir):
        self.repo, self.ref, self.api_dir, self.cache = repo, ref, api_dir, {}

    def load(self, name):
        if name not in self.cache:
            rel = f"{self.api_dir}/{name}"
            if self.ref is None:
                path = Path(self.repo, rel)
                text = path.read_text() if path.exists() else None
            else:
                r = subprocess.run(["git", "-C", self.repo, "show", f"{self.ref}:{rel}"],
                                   capture_output=True, text=True)
                text = r.stdout if r.returncode == 0 else None
            self.cache[name] = yaml.safe_load(text) if text is not None else None
        return self.cache[name]

    def names(self):
        if self.ref is None:
            return sorted(p.name for p in Path(self.repo, self.api_dir).glob("*.yaml"))
        r = subprocess.run(["git", "-C", self.repo, "ls-tree", "--name-only", f"{self.ref}:{self.api_dir}"],
                           capture_output=True, text=True, check=True)
        return sorted(n for n in r.stdout.split() if n.endswith(".yaml"))

    def resolve(self, node, file):
        """Follows a $ref chain and returns (node, file the node lives in, last $ref or None)."""
        ref = None
        seen = set()
        while isinstance(node, dict) and "$ref" in node:
            ref = node["$ref"]
            target_file, _, pointer = ref.partition("#")
            target_file = target_file or file
            if (target_file, pointer) in seen:
                break
            seen.add((target_file, pointer))
            node = self.load(target_file)
            for part in filter(None, pointer.split("/")):
                node = (node or {}).get(part.replace("~1", "/").replace("~0", "~"))
            file = target_file
        return node, file, ref


class Diff:
    def __init__(self, old_tree, new_tree):
        self.old, self.new = old_tree, new_tree
        self.findings = []
        self.visited = set()

    def add(self, severity, where, message):
        self.findings.append({"severity": severity, "where": where, "message": message})

    def schema(self, o, ofile, n, nfile, where, direction):
        o, ofile, oref = self.old.resolve(o, ofile)
        n, nfile, nref = self.new.resolve(n, nfile)
        if oref or nref:
            if (oref, nref, direction) in self.visited:
                return
            self.visited.add((oref, nref, direction))
        if o is None and n is None:
            return
        if o is None or n is None:
            self.add(BREAKING, where, f'schema {"removed" if n is None else "appeared"} ({oref} -> {nref})')
            return
        if not isinstance(o, dict) or not isinstance(n, dict):
            if o != n:
                self.add(REVIEW, where, f"non-object schema changed: {o!r} -> {n!r}")
            return

        reads = direction == "read"
        for k in ("description", "title", "examples", "example"):
            if o.get(k) != n.get(k):
                self.add(DOCS, where, f"{k} changed")
        self._types(o, n, where, reads)
        self._enum(o, n, where, reads)
        self._bounds(o, n, where, reads)
        self._object(o, ofile, n, nfile, where, direction)
        if "items" in o or "items" in n:
            self.schema(o.get("items"), ofile, n.get("items"), nfile, f"{where}[]", direction)
        for comb in ("oneOf", "anyOf", "allOf"):
            if comb in o or comb in n:
                self._combinator(comb, o, ofile, n, nfile, where, direction)
        if o.get("deprecated") != n.get("deprecated"):
            self.add(NOTE, where, f'deprecated: {o.get("deprecated")} -> {n.get("deprecated")}')
        if o.get("default") != n.get("default"):
            self.add(REVIEW, where, f'default changed: {o.get("default")!r} -> {n.get("default")!r}')
        for k in sorted((set(o) | set(n)) - SCHEMA_KEYS):
            if not k.startswith("x-") and o.get(k) != n.get(k):
                self.add(REVIEW, where, f"keyword {k!r} changed: {o.get(k)!r} -> {n.get(k)!r}")

    @staticmethod
    def _typeset(s):
        t = s.get("type")
        if t is None:
            return None
        return set(t) if isinstance(t, list) else {t}

    def _types(self, o, n, where, reads):
        ot, nt = self._typeset(o), self._typeset(n)
        if ot == nt:
            return

        def covered(src, dst):
            if dst is None:
                return True
            return all(t in dst or TYPE_WIDENS.get(t, set()) & dst for t in (src or {"<any>"}))

        # a reader must still understand everything EVerest produces, a writer's old values must still be accepted
        ok = covered(nt, ot) if reads else covered(ot, nt)
        self.add(ADDITIVE if ok else BREAKING, where, f"type {sorted(ot or [])} -> {sorted(nt or [])}")

    def _enum(self, o, n, where, reads):
        for k in ("enum", "const"):
            if k not in o and k not in n:
                continue
            ov = o.get(k) if k == "enum" else ([o[k]] if k in o else None)
            nv = n.get(k) if k == "enum" else ([n[k]] if k in n else None)
            if ov == nv:
                continue
            if ov is None:
                self.add(NOTE if reads else BREAKING, where, f"{k} constraint added: {nv}")
                continue
            if nv is None:
                self.add(REVIEW if reads else ADDITIVE, where, f"{k} constraint removed (was {ov})")
                continue
            removed = [v for v in ov if v not in nv]
            added = [v for v in nv if v not in ov]
            if removed:
                self.add(BREAKING, where, f"{k} values removed: {removed}")
            if added:
                self.add(ADDITIVE, where, f"{k} values added: {added}" +
                         (" (clients must tolerate unknown values)" if reads else ""))
            if not removed and not added:
                self.add(DOCS, where, f"{k} order changed")

    def _bounds(self, o, n, where, reads):
        # tightening hurts writers; loosening hurts readers, which the policy calls non-breaking
        for k in LOWER_BOUNDS + UPPER_BOUNDS:
            ov, nv = o.get(k), n.get(k)
            if ov == nv:
                continue
            if isinstance(ov, bool) or isinstance(nv, bool):
                self.add(REVIEW, where, f"{k}: {ov} -> {nv}")
                continue
            if ov is None or nv is None:
                tighter = ov is None
            else:
                tighter = nv > ov if k in LOWER_BOUNDS else nv < ov
            severity = NOTE if reads else (BREAKING if tighter else ADDITIVE)
            self.add(severity, where, f'{k}: {ov} -> {nv} ({"tightened" if tighter else "loosened"})')
        for k in ("pattern", "format"):
            ov, nv = o.get(k), n.get(k)
            if ov != nv:
                severity = NOTE if reads else (ADDITIVE if nv is None else BREAKING if ov is None else REVIEW)
                self.add(severity, where, f"{k}: {ov!r} -> {nv!r}")

    def _object(self, o, ofile, n, nfile, where, direction):
        reads = direction == "read"
        oprops, nprops = o.get("properties") or {}, n.get("properties") or {}
        oreq, nreq = set(o.get("required") or []), set(n.get("required") or [])
        o_closed = o.get("additionalProperties") is False
        n_closed = n.get("additionalProperties") is False
        for name in sorted(set(oprops) - set(nprops)):
            if reads:
                self.add(BREAKING, f"{where}.{name}", "field removed from payload clients read")
            elif n_closed:
                self.add(BREAKING, f"{where}.{name}",
                         "field removed from payload clients send (additionalProperties: false, now rejected)")
            else:
                self.add(REVIEW, f"{where}.{name}", "field removed from payload clients send (ignored?)")
        for name in sorted(set(nprops) - set(oprops)):
            required = name in nreq
            if required and not reads:
                self.add(BREAKING, f"{where}.{name}", "required field added to payload clients send")
            else:
                self.add(ADDITIVE, f"{where}.{name}", f'field added ({"required" if required else "optional"})')
        for name in sorted(set(oprops) & set(nprops)):
            if name in nreq and name not in oreq and not reads:
                self.add(BREAKING, f"{where}.{name}", "optional field became required in payload clients send")
            if name in oreq and name not in nreq and reads:
                self.add(NOTE, f"{where}.{name}", "required field became optional in payload EVerest sends")
            self.schema(oprops[name], ofile, nprops[name], nfile, f"{where}.{name}", direction)
        oap, nap = o.get("additionalProperties"), n.get("additionalProperties")
        if o_closed != n_closed:
            if n_closed and not reads:
                self.add(BREAKING, where, "additionalProperties closed in payload clients send")
            else:
                self.add(NOTE, where, f"additionalProperties: {oap} -> {nap}")
        if isinstance(oap, dict) or isinstance(nap, dict):
            self.schema(oap if isinstance(oap, dict) else {}, ofile, nap if isinstance(nap, dict) else {}, nfile,
                        f"{where}{{*}}", direction)

    @staticmethod
    def _variant_key(tree, node, file, index):
        resolved, _, ref = tree.resolve(node, file)
        if ref:
            return ref.rsplit("/", 1)[-1]
        if isinstance(resolved, dict) and resolved.get("title"):
            return resolved["title"]
        if isinstance(resolved, dict) and isinstance(resolved.get("type"), str):
            return resolved["type"]
        return f"#{index}"

    def _combinator(self, comb, o, ofile, n, nfile, where, direction):
        reads = direction == "read"
        ovariants = {self._variant_key(self.old, v, ofile, i): v for i, v in enumerate(o.get(comb) or [])}
        nvariants = {self._variant_key(self.new, v, nfile, i): v for i, v in enumerate(n.get(comb) or [])}
        for k in sorted(ovariants.keys() - nvariants.keys()):
            severity = BREAKING if (not reads or comb == "allOf") else NOTE
            self.add(severity, f"{where}<{comb}:{k}>", "variant removed")
        for k in sorted(nvariants.keys() - ovariants.keys()):
            severity = BREAKING if comb == "allOf" else (REVIEW if reads else ADDITIVE)
            self.add(severity, f"{where}<{comb}:{k}>", "variant added")
        for k in sorted(ovariants.keys() & nvariants.keys()):
            self.schema(ovariants[k], ofile, nvariants[k], nfile, f"{where}<{comb}:{k}>", direction)

    def message(self, o, ofile, n, nfile, where, direction):
        o, ofile, _ = self.old.resolve(o, ofile)
        n, nfile, _ = self.new.resolve(n, nfile)
        o, n = o or {}, n or {}
        if o.get("contentType") != n.get("contentType"):
            self.add(BREAKING, where, f'contentType {o.get("contentType")} -> {n.get("contentType")}')
        self.schema(o.get("payload"), ofile, n.get("payload"), nfile, f"{where}:payload", direction)
        if "headers" in o or "headers" in n:
            self.schema(o.get("headers"), ofile, n.get("headers"), nfile, f"{where}:headers", direction)
        for k in ("summary", "title", "description"):
            if o.get(k) != n.get(k):
                self.add(DOCS, where, f"message {k} changed")


def channel_id(ref_node):
    ref = ref_node.get("$ref", "") if isinstance(ref_node, dict) else ""
    return ref.rsplit("/", 1)[-1] or None


def index_operations(tree, file, doc):
    """Keys the operations of a specification by (action, channel address)."""
    operations = doc.get("operations") or {}
    reply_address = {}
    for op in operations.values():
        if op.get("reply"):
            chan, _, _ = tree.resolve(op.get("channel"), file)
            reply_address[channel_id(op["reply"].get("channel"))] = f'{(chan or {}).get("address")} (reply)'
    ops = {}
    for op_id, op in operations.items():
        chan, cfile, _ = tree.resolve(op.get("channel"), file)
        cid = channel_id(op.get("channel"))
        address = (chan or {}).get("address") or reply_address.get(cid, f"<{cid}>")
        reply = None
        if op.get("reply"):
            rchan, _, _ = tree.resolve(op["reply"].get("channel"), file)
            reply = (rchan or {}).get("address") or "replyTo"
        ops[(op.get("action"), address)] = {"id": op_id, "op": op, "channel": chan or {}, "file": cfile,
                                            "reply": reply}
    return ops


def is_experimental(doc):
    return "experimental" in str((doc.get("info") or {}).get("description", "")).lower()


def strip_docs(node):
    if isinstance(node, dict):
        return {k: strip_docs(v) for k, v in node.items() if k not in DOC_KEYS and not k.startswith("x-")}
    if isinstance(node, list):
        return [strip_docs(v) for v in node]
    return node


def compare_servers(d, old, new):
    oservers, nservers = old.get("servers") or {}, new.get("servers") or {}
    for name in sorted(set(oservers) | set(nservers)):
        os_, ns = oservers.get(name), nservers.get(name)
        if ns is None:
            d.add(BREAKING, f"servers.{name}", "server removed")
        elif os_ is None:
            d.add(ADDITIVE, f"servers.{name}", "server added")
        else:
            for k in ("host", "pathname", "protocol", "protocolVersion"):
                if os_.get(k) != ns.get(k):
                    d.add(BREAKING, f"servers.{name}.{k}", f"{os_.get(k)!r} -> {ns.get(k)!r}")
    if old.get("defaultContentType") != new.get("defaultContentType"):
        d.add(BREAKING, "defaultContentType", f'{old.get("defaultContentType")} -> {new.get("defaultContentType")}')


def compare_operations(d, old_tree, new_tree, name, old, new):
    oops, nops = index_operations(old_tree, name, old), index_operations(new_tree, name, new)
    old_addresses = {addr for _, addr in oops}
    new_actions = {addr: action for action, addr in nops}
    for action, addr in sorted(oops.keys() - nops.keys(), key=str):
        if addr in new_actions:
            d.add(BREAKING, addr, f"operation action changed {action} -> {new_actions[addr]}")
        else:
            d.add(BREAKING, addr, f'{action} operation removed ({oops[(action, addr)]["id"]})')
    for action, addr in sorted(nops.keys() - oops.keys(), key=str):
        if addr not in old_addresses:
            d.add(ADDITIVE, addr, f'{action} operation added ({nops[(action, addr)]["id"]})')
    for key in sorted(oops.keys() & nops.keys(), key=str):
        action, addr = key
        o, n = oops[key], nops[key]
        direction = "read" if action == "receive" else "write"
        if o["reply"] != n["reply"]:
            d.add(BREAKING if o["reply"] else REVIEW, addr, f'reply channel {o["reply"]} -> {n["reply"]}')
        if (o["op"].get("deprecated"), o["channel"].get("deprecated")) != \
                (n["op"].get("deprecated"), n["channel"].get("deprecated")):
            d.add(NOTE, addr, "deprecation flag changed")
        for k in ("title", "summary", "description"):
            if o["op"].get(k) != n["op"].get(k) or o["channel"].get(k) != n["channel"].get(k):
                d.add(DOCS, addr, f"operation/channel {k} changed")
        omessages, nmessages = o["channel"].get("messages") or {}, n["channel"].get("messages") or {}
        for mk in sorted(set(omessages) - set(nmessages)):
            d.add(BREAKING, f"{addr}#{mk}", "message removed from channel")
        for mk in sorted(set(nmessages) - set(omessages)):
            d.add(REVIEW if direction == "read" else ADDITIVE, f"{addr}#{mk}", "message added to channel")
        for mk in sorted(set(omessages) & set(nmessages)):
            d.message(omessages[mk], o["file"], nmessages[mk], n["file"], addr, direction)


def compare_file(old_tree, new_tree, name):
    old, new = old_tree.load(name), new_tree.load(name)
    result = {"file": name, "findings": [], "old_version": None, "new_version": None, "experimental": False}
    if new is None:
        result["status"] = "removed"
        result["findings"].append({"severity": BREAKING, "where": name, "message": "specification removed"})
        return result
    result["new_version"] = str((new.get("info") or {}).get("version"))
    result["experimental"] = is_experimental(new)
    if old is None:
        result["status"] = "new"
        return result
    result["old_version"] = str((old.get("info") or {}).get("version"))

    d = Diff(old_tree, new_tree)
    compare_servers(d, old, new)
    compare_operations(d, old_tree, new_tree, name, old, new)
    if not d.findings and strip_docs(old) != strip_docs(new):
        d.add(REVIEW, name, "structural change outside the reachable operations (renamed or unused components?)")
    if not d.findings and old != new:
        d.add(DOCS, name, "documentation-only change")

    # shared schemas reached through several operations report the same finding more than once
    findings, seen = [], set()
    for f in d.findings:
        key = (f["severity"], f["where"], f["message"])
        if key not in seen:
            seen.add(key)
            findings.append(f)
    if is_experimental(old):
        for f in findings:
            if f["severity"] in UNRESOLVED:
                f["message"] += " [experimental: exempt from stability guarantees]"
                f["severity"] = NOTE
    result["findings"] = findings
    return result


def update_status(result):
    if result.get("status") in ("new", "removed"):
        return
    severities = {f["severity"] for f in result["findings"]}
    result["status"] = next((s for s in SEVERITY_ORDER if s in severities), "unchanged")


def git(repo, *args):
    r = subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def is_ancestor(repo, ancestor, descendant):
    return subprocess.run(["git", "-C", repo, "merge-base", "--is-ancestor", ancestor, descendant]).returncode == 0


def unresolved_by_commit(repo, api_dir, commit, name):
    """Returns the breaking and review findings a commit produces for one specification against its parent."""
    parent = Tree(repo, f"{commit}^", api_dir)
    result = compare_file(parent, Tree(repo, commit, api_dir), name)
    return {(f["where"], f["message"]) for f in result["findings"] if f["severity"] in UNRESOLVED}


def load_acknowledged(path, repo):
    if not path.exists():
        return []
    entries = yaml.safe_load(path.read_text()) or []
    for entry in entries:
        missing = {"file", "commit", "reason"} - set(entry)
        if missing:
            sys.exit(f"error: {path.name}: entry {entry} lacks {sorted(missing)}")
        if not FULL_HASH.fullmatch(str(entry["commit"])):
            sys.exit(f'error: {path.name}: {entry["commit"]!r} is not a full commit hash')
        if git(repo, "cat-file", "-e", f'{entry["commit"]}^{{commit}}') is None:
            sys.exit(f'error: {path.name}: commit {entry["commit"]} not found, is the history complete?')
    return entries


def apply_acknowledged(results, entries, repo, api_dir, base, path):
    """Marks the findings of acknowledged commits between base and HEAD. Warns about entries in that
    range that match nothing, since that means the specification changed again since the review."""
    by_file = {r["file"]: r for r in results}
    for entry in entries:
        commit = entry["commit"]
        if is_ancestor(repo, commit, base) or not is_ancestor(repo, commit, "HEAD"):
            continue
        acknowledged = unresolved_by_commit(repo, api_dir, commit, entry["file"])
        matched = False
        for f in (by_file.get(entry["file"]) or {}).get("findings", []):
            if f["severity"] in UNRESOLVED and (f["where"], f["message"]) in acknowledged:
                f["severity"] = ACKNOWLEDGED
                f["commit"] = commit
                matched = True
        if not matched:
            print(f'warning: {path.name}: {entry["file"]} {commit[:12]} matches no finding', file=sys.stderr)


def attribute_unresolved(results, repo, api_dir, base):
    """Names the commit between base and HEAD that introduced each unresolved finding, if a single one did."""
    for r in results:
        open_findings = [f for f in r["findings"] if f["severity"] in UNRESOLVED]
        if not open_findings:
            continue
        log = git(repo, "log", "--reverse", "--format=%H", f"{base}..HEAD", "--", f'{api_dir}/{r["file"]}')
        for commit in (log or "").split():
            introduced = unresolved_by_commit(repo, api_dir, commit, r["file"])
            for f in open_findings:
                if (f["where"], f["message"]) in introduced:
                    f["commit"] = commit


def acknowledgement_template(results, repo):
    entries = {}
    for r in results:
        for f in r["findings"]:
            if f["severity"] in UNRESOLVED and f.get("commit"):
                entries.setdefault((r["file"], f["commit"]), {
                    "file": r["file"],
                    "commit": f["commit"],
                    "subject": git(repo, "log", "-1", "--format=%s", f["commit"]),
                    "reason": "TODO: why clients are not affected",
                })
    return yaml.safe_dump(list(entries.values()), sort_keys=False, width=120, allow_unicode=True)


def set_spec_version(path, version):
    """Rewrites the value of info.version in place, keeping the rest of the file untouched."""
    lines = path.read_text().splitlines(keepends=True)
    in_info, child_indent = False, None
    for i, line in enumerate(lines):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip())
        if indent == 0:
            if in_info:
                break
            in_info = line.rstrip() == "info:"
            continue
        if in_info:
            child_indent = child_indent if child_indent is not None else indent
            match = re.match(r"(\s*version:\s*)(['\"]?)[^'\"\s#]+\2(.*)", line, re.S)
            if indent == child_indent and match:
                lines[i] = f"{match.group(1)}{match.group(2)}{version}{match.group(2)}{match.group(3)}"
                path.write_text("".join(lines))
                return
    sys.exit(f"error: {path}: no info.version found")


def update_versions(results, repo, api_dir):
    for r in results:
        if not r["needs_bump"]:
            continue
        if any(f["severity"] in UNRESOLVED for f in r["findings"]):
            print(f'not updated {r["file"]}: unresolved breaking or review findings', file=sys.stderr)
            continue
        path = Path(repo, api_dir, r["file"])
        set_spec_version(path, r["suggested_version"])
        if str(yaml.safe_load(path.read_text())["info"]["version"]) != r["suggested_version"]:
            sys.exit(f"error: {path}: info.version not updated as expected")
        print(f'updated {r["file"]}: {r["new_version"]} -> {r["suggested_version"]}', file=sys.stderr)
        r["new_version"], r["needs_bump"] = r["suggested_version"], False


def parse_version(version, file):
    match = re.fullmatch(r"(\d+)(?:\.(\d+))?(?:\.(\d+))?", str(version))
    if not match:
        sys.exit(f"error: {file}: version {version!r} is not MAJOR.MINOR.PATCH")
    return tuple(int(part or 0) for part in match.groups())


def needs_bump(result):
    """A changed specification needs a bump while its version is below the suggested one, so a bump
    made for an earlier, smaller change in the same release does not count."""
    if result["status"] in ("new", "removed", "unchanged"):
        return False
    return parse_version(result["new_version"], result["file"]) < \
        parse_version(result["suggested_version"], result["file"])


def suggest_version(result, docs_bump):
    old = result["old_version"]
    if result["status"] in ("new", "removed", "unchanged") or not old:
        return result["new_version"]
    major, minor, patch = parse_version(old, result["file"])
    if result["status"] == BREAKING:
        return f"{major + 1}.0.0"
    if result["status"] != DOCS or docs_bump == "minor":
        return f"{major}.{minor + 1}.0"
    return f"{major}.{minor}.{patch + 1}"


def print_report(results, verbose):
    shown = {BREAKING, REVIEW, ADDITIVE}
    if verbose >= 1:
        shown |= {ACKNOWLEDGED, NOTE}
    if verbose >= 2:
        shown.add(DOCS)
    for r in results:
        version = f'{r["old_version"]} -> {r["new_version"]}' if r["old_version"] else r["new_version"]
        experimental = " (experimental)" if r["experimental"] else ""
        print(f'== {r["file"]}: {r["status"]}{experimental}  [{version}; suggested {r["suggested_version"]}]')
        for f in sorted(r["findings"], key=lambda f: SEVERITY_ORDER.index(f["severity"])):
            if f["severity"] in shown:
                commit = f' [{f["commit"][:12]}]' if f.get("commit") else ""
                print(f'   {f["severity"]:12} {f["where"]}: {f["message"]}{commit}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("base", help="git ref to compare the working tree against, e.g. 2026.02.0")
    parser.add_argument("--repo", type=Path, default=DEFAULT_REPO, help="EVerest repository (default: %(default)s)")
    parser.add_argument("--api-dir", default="docs/source/reference/EVerest_API",
                        help="directory of the specifications, relative to --repo (default: %(default)s)")
    parser.add_argument("--docs-bump", choices=["patch", "minor"], default="patch",
                        help="version bump suggested for documentation-only changes (default: %(default)s)")
    parser.add_argument("-v", "--verbose", action="count", default=0,
                        help="-v also lists notes, -vv also lists documentation changes")
    parser.add_argument("--json", action="store_true", help="print the results as JSON")
    parser.add_argument("--acknowledged", default="docs/source/project/releases/asyncapi_compat_acknowledged.yaml",
                        help="commits whose findings were reviewed as not breaking, relative to --repo "
                             "(default: %(default)s)")
    parser.add_argument("--acknowledge", action="store_true",
                        help="print acknowledgement entries for the commits behind the unresolved findings")
    parser.add_argument("--update-versions", action="store_true",
                        help="write the suggested versions into the changed specifications below them, "
                             "skipping those with unresolved findings")
    args = parser.parse_args()

    old_tree = Tree(args.repo, args.base, args.api_dir)
    new_tree = Tree(args.repo, None, args.api_dir)
    names = sorted(set(old_tree.names()) | set(new_tree.names()))
    results = [compare_file(old_tree, new_tree, name) for name in names]
    acknowledged = args.repo / args.acknowledged
    entries = load_acknowledged(acknowledged, args.repo)
    apply_acknowledged(results, entries, args.repo, args.api_dir, args.base, acknowledged)
    attribute_unresolved(results, args.repo, args.api_dir, args.base)
    for r in results:
        update_status(r)
        r["suggested_version"] = suggest_version(r, args.docs_bump)
        r["needs_bump"] = needs_bump(r)

    if args.acknowledge:
        print(acknowledgement_template(results, args.repo), end="")
        return 0
    if args.update_versions:
        update_versions(results, args.repo, args.api_dir)

    unresolved = [r for r in results if any(f["severity"] in UNRESOLVED for f in r["findings"])]
    below_suggested = [r for r in results if r["needs_bump"]]
    if args.json:
        json.dump(results, sys.stdout, indent=2)
        print()
    else:
        print_report(results, args.verbose)
        print(f"\n{len(unresolved)} specification(s) with unacknowledged breaking or review findings, "
              f"{len(below_suggested)} changed specification(s) below the suggested version")
    return 1 if unresolved or below_suggested else 0


if __name__ == "__main__":
    sys.exit(main())
