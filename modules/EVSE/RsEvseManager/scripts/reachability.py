#!/usr/bin/env python3
"""Reachability audit for RsEvseManager.

Lists surface that exists but is not reachable from production code, and fails if
that list grows against the checked in baseline.

Five gap classes have each bitten this port at least once, every time discovered by
accident rather than by a gate:

  UNROUTED      a Command variant whose Core arm produces no effect
  NO-PRODUCER   an enum variant nothing in production constructs
  TEST-ONLY-FN  a pub fn whose only callers are inside #[cfg(test)] modules
  DROPPED       an empty bodied on_* subscriber callback in main.rs
  UNMATCHED     a variant sent into a match that has no arm for it

The audit is deliberately coarse. It reads Rust as text, so it cannot see through a
macro or a trait object, and it will occasionally be wrong. It is a ratchet, not a
proof: the number must not rise without someone saying why.

NO-PRODUCER censuses `SessionEvent`, `Effect`, `IecInput`, `HlcEvent`, `Command`,
`HlcUpdate` and `SlacUpdate`. An enum off that list is not audited at all, which is
how both `SessionPhase` defects stayed invisible; adding the name to `collect` is
all that class needs, and doing so reports `SessionPhase::Reserved` and
`SessionPhase::Charging`.

Usage:
    scripts/reachability.py            compare against the baseline, exit 1 if it grew
    scripts/reachability.py --update   rewrite the baseline (state why in the commit)
    scripts/reachability.py --list     print the current findings and exit 0
    scripts/reachability.py --selftest check the pattern/producer discriminator
"""

import argparse
import functools
import pathlib
import re
import sys

MODULE = pathlib.Path(__file__).resolve().parent.parent
SRC = MODULE / "src"
BASELINE = MODULE / "scripts" / "reachability-baseline.txt"

# How far `is_consumer` reads ahead of a token before giving up. The longest real
# match arm in this module is the `Effect::context` or-pattern, a little over 1 KB.
PATTERN_SCAN_LIMIT = 4096

# How many enclosing brackets `is_consumer` may step out of. Three covers the
# nesting this module writes, `Some(Ok(E::V(x)))`.
ESCAPE_LIMIT = 3

# A `(` that a pattern can be nested inside: a tuple variant constructor, which
# reads as an uppercase path immediately left of the bracket. `session_event(` is
# a call and does not qualify, which is what keeps
# `self.session_event(SessionEvent::ReservationStart)` a producer.
TUPLE_CONSTRUCTOR = re.compile(r"(?:[A-Za-z0-9_]+::)*[A-Z][A-Za-z0-9_]*\s*$")

# A variant named as either operand of `==` or `!=` is being tested for, not
# written. This is the whole distance between the two known `SessionPhase`
# defects: `SessionPhase::Reserved` is never named in production at all, while
# `SessionPhase::Charging` is named once, on the right of the `==` at
# `core/mod.rs:962` that fills `AuthorizationHeld.charging` and is therefore
# always false. Without this, that comparison reads as the writer it is not.
COMPARISON_BEFORE = re.compile(r"[=!]=\s*$")
COMPARISON_AFTER = re.compile(r"\s*[=!]=[^=]")

RAW_STRING = re.compile(r'r(#*)"')
CHAR_LITERAL = re.compile(r"'(?:\\.|[^\\'])'")


def rust_files():
    return sorted(SRC.rglob("*.rs"))


def test_spans(text):
    """Line ranges (1 based, inclusive) covered by #[cfg(test)] modules.

    Brace counting from the module opening. Good enough for this codebase, which
    puts every test module at the end of its file in the conventional shape.
    """
    spans = []
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if line.strip() != "#[cfg(test)]":
            continue
        depth, started = 0, False
        for j in range(i, len(lines)):
            depth += lines[j].count("{") - lines[j].count("}")
            if "{" in lines[j]:
                started = True
            if started and depth <= 0:
                spans.append((i + 1, j + 1))
                break
        else:
            spans.append((i + 1, len(lines)))
    return spans


def in_test(spans, lineno):
    return any(lo <= lineno <= hi for lo, hi in spans)


def strip_noise(text):
    """Blank out comments, string literals and char literals, in place.

    Every removed character becomes a space and every newline is kept, so line
    numbers and columns survive. The census reads code after this; without it,
    prose counts as code. Two variants hid behind exactly that: a comment in
    `path/dc.rs` saying `Effect::OverVoltageLimits` has no producer was itself
    read as the producer.
    """
    out, i, n = [], 0, len(text)
    while i < n:
        two = text[i : i + 2]
        if two == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif two == "/*":
            j, depth = i + 2, 1
            while j < n and depth:
                if text[j : j + 2] == "/*":
                    depth += 1
                    j += 2
                elif text[j : j + 2] == "*/":
                    depth -= 1
                    j += 2
                else:
                    j += 1
            out.append(_blank(text[i:j]))
            i = j
        elif text[i] == "r" and (m := RAW_STRING.match(text, i)):
            hashes = m.group(1)
            j = text.find('"' + hashes, m.end())
            j = n if j < 0 else j + 1 + len(hashes)
            out.append(_blank(text[i:j]))
            i = j
        elif text[i] == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    break
                j += 1
            out.append(_blank(text[i:j]))
            i = j
        elif text[i] == "'" and (m := CHAR_LITERAL.match(text, i)):
            # Only a real char literal. A bare `'` is a lifetime and stays.
            out.append(" " * (m.end() - i))
            i = m.end()
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def _blank(chunk):
    return "".join(c if c == "\n" else " " for c in chunk)


@functools.lru_cache(maxsize=None)
def code_text(path):
    """A file's source with comments and literals blanked. See `strip_noise`."""
    return strip_noise(path.read_text(encoding="utf-8", errors="replace"))


@functools.lru_cache(maxsize=None)
def production_text(path):
    """`code_text` with every `#[cfg(test)]` line blanked as well.

    Same shape as the file, so an offset into it is an offset into the file.
    """
    code = code_text(path)
    spans = test_spans(code)
    lines = code.splitlines(keepends=True)
    for n, line in enumerate(lines, start=1):
        if in_test(spans, n):
            lines[n - 1] = _blank(line)
    return "".join(lines)


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


PATTERN_KEYWORD = re.compile(r"matches!\s*\(|\bif\s+let\b|\bwhile\s+let\b|\blet\b")


def is_consumer(code, end):
    """True when the path token ending at `end` is read rather than produced.

    A comparison is the easy half: `self.session.phase == SessionPhase::Charging`
    names the variant to test for it, and nothing is written. The hard half is
    that a pattern and a constructor are spelled identically.

    `HlcUpdate::SendError(error)` is the same eleven characters whether it is a
    match arm consuming the variant or an expression producing it, so the token
    cannot decide this. Two things around it can.

    First, an enclosing `matches!`, `if let` or `while let` puts everything up to
    its `=` in pattern position.

    Otherwise, scan forward from the token: a pattern reaches `=>`, an expression
    reaches `,` or `;` first. That is what separates the `main.rs` dispatch arm
    `HlcUpdate::Setup { .. } => hlc.setup(..)` from the producer
    `Effect::HlcUpdate(HlcUpdate::Setup { .. })` in `hlc/setup.rs`, which the
    previous single-line `.*=>` heuristic could not: a struct variant puts its
    `=>` several lines below the name.

    Closing a bracket the token sits inside steps out one level and keeps
    scanning, but only when that bracket is one a pattern can nest inside:
    `Some(SessionEvent::Enabled) if .. =>` is a pattern two levels down, while
    `self.session_event(SessionEvent::ReservationStart)` is a producer one level
    down, and the two differ only in what sits left of the `(`. A block, an index
    or a call ends the walk, and so does `ESCAPE_LIMIT` levels; each of those
    answers producer, the reading that adds no finding.
    """
    token_start = end
    while token_start and (code[token_start - 1].isalnum() or code[token_start - 1] in "_:"):
        token_start -= 1

    if COMPARISON_BEFORE.search(code[max(0, token_start - 40) : token_start]):
        return True
    if COMPARISON_AFTER.match(code, end):
        return True

    start = max(code.rfind(c, 0, end) for c in ";{}")
    keywords = list(PATTERN_KEYWORD.finditer(code, start + 1, end))
    if keywords:
        tail = code[keywords[-1].start() : end]
        if not re.search(r"=(?![=>])", tail):
            return True

    openers = enclosing_openers(code, end, ESCAPE_LIMIT)
    depth, escapes = 0, 0
    for i in range(end, min(len(code), end + PATTERN_SCAN_LIMIT)):
        c = code[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth < 0:
                if escapes >= len(openers) or not nestable(code, openers[escapes]):
                    return False
                escapes += 1
                depth = 0
        elif depth == 0:
            if code[i : i + 2] == "=>":
                return True
            if c in ",;":
                return False
    return False


def enclosing_openers(code, pos, limit):
    """Indices of the brackets enclosing `pos`, innermost first."""
    out, depth = [], 0
    for i in range(pos - 1, max(-1, pos - PATTERN_SCAN_LIMIT), -1):
        c = code[i]
        if c in ")]}":
            depth += 1
        elif c in "([{":
            if depth:
                depth -= 1
            else:
                out.append(i)
                if len(out) == limit:
                    break
    return out


def nestable(code, opener):
    """Whether a pattern can appear directly inside the bracket at `opener`."""
    if code[opener] != "(":
        return False
    before = code[max(0, opener - 80) : opener]
    if TUPLE_CONSTRUCTOR.search(before):
        return True
    stripped = before.rstrip()
    return not stripped or stripped[-1] in "(,|" or stripped.endswith("=>")


def production_lines():
    """Yield (path, lineno, text) for every code line outside a test module."""
    for path in rust_files():
        for n, line in enumerate(production_text(path).splitlines(), start=1):
            if line.strip():
                yield path, n, line


def enum_variants(enum_name):
    """Variant names of a top level `pub enum <name>`, and its declaration span."""
    pattern = re.compile(r"^\s*pub enum\s+" + re.escape(enum_name) + r"\b")
    for path in rust_files():
        lines = code_text(path).splitlines()
        for i, line in enumerate(lines):
            if not pattern.match(line):
                continue
            depth, started, out = 0, False, []
            for j in range(i, len(lines)):
                depth += lines[j].count("{") - lines[j].count("}")
                if "{" in lines[j]:
                    started = True
                if j > i:
                    m = re.match(r"\s{4}([A-Z][A-Za-z0-9]*)\s*[({,=]?", lines[j])
                    if m:
                        out.append(m.group(1))
                if started and depth <= 0:
                    return path, out, (i + 1, j + 1)
    return None, [], (0, 0)


def find_no_producer(enum_name, prefix):
    """Variants no production expression constructs.

    A match arm, a `matches!`, an or-pattern and a comparison are all consumers.
    So is the enum's own declaration. What is left is a producer.
    """
    decl_path, variants, decl_span = enum_variants(enum_name)
    if not variants:
        return []
    constructed = set()
    for path in rust_files():
        code = production_text(path)
        for v in variants:
            if v in constructed:
                continue
            for hit in re.finditer(rf"\b{re.escape(prefix)}::{re.escape(v)}\b", code):
                line = line_of(code, hit.start())
                if path == decl_path and decl_span[0] <= line <= decl_span[1]:
                    continue
                if is_consumer(code, hit.end()):
                    continue
                constructed.add(v)
                break
    return [f"NO-PRODUCER   {enum_name}::{v}" for v in variants if v not in constructed]


def find_dropped_callbacks():
    """on_* subscriber callbacks in main.rs that consume nothing.

    A body that is empty drops the fact. So does a body that only announces the
    drop: the log line makes the gap visible to an operator, it does not make the
    fact reach the core. Counting only empty bodies would let a callback leave
    this ledger by gaining a log line, which is the false comfort the ledger
    exists to prevent.
    """
    main = SRC / "main.rs"
    if not main.exists():
        return []
    text = main.read_text(encoding="utf-8", errors="replace")

    empty = re.findall(r"fn\s+(on_[a-z0-9_]+)\s*\((?:[^{}]*?)\)\s*\{\s*\}", text, re.S)
    announced = re.findall(
        r"fn\s+(on_[a-z0-9_]+)\s*\((?:[^{}]*?)\)\s*\{\s*"
        r"log::(?:warn|debug|info|trace)!\(\s*\"unported HLC fact dropped:[^\"]*\"\s*\)\s*;\s*\}",
        text,
        re.S,
    )
    return [f"DROPPED       main.rs {name}" for name in sorted(set(empty) | set(announced))]


def find_test_only_pub_fns():
    """pub fns whose only call sites are inside #[cfg(test)] modules."""
    declared = {}
    for path in rust_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        spans = test_spans(text)
        for n, line in enumerate(text.splitlines(), start=1):
            if in_test(spans, n):
                continue
            m = re.match(r"\s*pub fn\s+([a-z_][a-z0-9_]*)\s*[(<]", line)
            if m and m.group(1) not in ("new", "default"):
                declared[m.group(1)] = path

    called_in_prod = set()
    for path, n, line in production_lines():
        for name in declared:
            if re.search(rf"\.{re.escape(name)}\s*\(", line) or re.search(
                rf"\b(?<!fn ){re.escape(name)}\s*\(", line
            ):
                if not re.match(rf"\s*pub fn\s+{re.escape(name)}\b", line):
                    called_in_prod.add(name)

    return [
        f"TEST-ONLY-FN  {declared[name].relative_to(MODULE)} {name}"
        for name in sorted(declared)
        if name not in called_in_prod
    ]


# Cases for `--selftest`. Each is a Rust fragment holding one `E::V`, and whether
# that occurrence is a pattern. The discriminator is the only part of this audit
# whose own bug is silent: a token misread as a producer removes a finding, and
# nothing downstream notices. Every case below is a shape this module actually
# contains.
CONSUMER_CASES = [
    ("E::V(x) => f(),", True),
    ("E::V => f(),", True),
    ("E::V(v) if v > 0 => f(),", True),
    ("Outer::Inner(E::V(x)),", False),
    ("A::B => E::V,", False),
    ("vec![E::V]", False),
    (".then_some(E::V(true))", False),
    ("let x = E::V(1);", False),
    ("if matches!(u, E::V(_)) {", True),
    ("if let E::V(v) = u {", True),
    ("while let E::V(v) = it.next() {", True),
    ("Other::A(_)\n    | E::V(_) => f(),", True),
    ("E::V {\n    a,\n    b,\n} => f(),", True),
    ("Outer::Inner(E::V {\n    a: 1,\n    b: 2,\n}),", False),
    ("Some(E::V(power)) => f(),", True),
    ("Some(E::V) if ready => f(),", True),
    ("(Some(_), Some(p)) => Some(E::V(p)),", False),
    ("A::B => Some(E::V),", False),
    ("self.session_event(E::V)\n}", False),
    ("Some(E::V(Power {\n    w: 1.0,\n}))\n}", False),
    ("charging: self.phase == E::V,", True),
    ("if E::V != self.phase {", True),
    ("let x = E::V;", False),
]

# Fragments whose `E::V` must not survive `strip_noise`, plus one that must.
NOISE_CASES = [
    ("// E::V has no producer anywhere", False),
    ("/// `E::V` is sent on plug in", False),
    ('let s = "E::V";', False),
    ('let s = r#"E::V"#;', False),
    ("let v = E::V;", True),
    ("fn f<\'a>(s: &\'a str) -> E::V { E::V }", True),
]


def selftest():
    """Check the discriminator against known shapes. Returns the failure count."""
    failures = 0
    for snippet, expected in CONSUMER_CASES:
        end = snippet.index("E::V") + len("E::V")
        got = is_consumer(snippet, end)
        if got != expected:
            failures += 1
            want = "pattern" if expected else "producer"
            print(f"  FAIL  read as {'pattern' if got else 'producer'}, want {want}: {snippet!r}")

    for snippet, expected in NOISE_CASES:
        got = "E::V" in strip_noise(snippet)
        if got != expected:
            failures += 1
            print(f"  FAIL  strip_noise {'kept' if got else 'dropped'} E::V: {snippet!r}")

    total = len(CONSUMER_CASES) + len(NOISE_CASES)
    print(f"selftest: {total - failures}/{total} passed")
    return failures


def collect():
    findings = []
    findings += find_dropped_callbacks()
    for enum_name, prefix in (
        ("SessionEvent", "SessionEvent"),
        ("Effect", "Effect"),
        ("IecInput", "IecInput"),
        ("HlcEvent", "HlcEvent"),
        ("Command", "Command"),
        # The two outbound boundary vocabularies. Every variant here is a call
        # the C++ makes on the HLC or SLAC stack, so a variant with no producer
        # is a call this port never makes.
        ("HlcUpdate", "HlcUpdate"),
        ("SlacUpdate", "SlacUpdate"),
        # The session event payloads and the stop reason they name. Both are
        # relayed rather than decided, so a variant with no producer is a
        # payload or a reason no consumer of this module can ever be told;
        # `StopTransactionReason` carries all twenty three wire values and the
        # narrowing to `StopReason` is many to one, so twenty of them are
        # reachable only through the carry.
        ("SessionPayload", "SessionPayload"),
        ("StopTransactionReason", "StopTransactionReason"),
        # The two vocabularies behind the pass-through variable group. Both are
        # reported on the `evse_manager` interface and neither is compared by
        # anything in this module, so a variant with no producer is a value no
        # consumer can ever be told. `SelectedProtocol` spells its own
        # constructions with the full path so that this census can see them;
        # see the note on the enum.
        ("SelectedProtocol", "SelectedProtocol"),
        ("CarManufacturer", "CarManufacturer"),
    ):
        findings += find_no_producer(enum_name, prefix)
    findings += find_test_only_pub_fns()
    return sorted(set(findings))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true", help="rewrite the baseline")
    ap.add_argument("--list", action="store_true", help="print findings, always exit 0")
    ap.add_argument("--selftest", action="store_true", help="check the discriminator")
    args = ap.parse_args()

    if args.selftest:
        return 1 if selftest() else 0

    findings = collect()

    if args.list:
        print("\n".join(findings) or "(nothing unreachable)")
        return 0

    if args.update or not BASELINE.exists():
        BASELINE.write_text("\n".join(findings) + "\n", encoding="utf-8")
        print(f"baseline written: {len(findings)} items -> {BASELINE.name}")
        return 0

    baseline = [
        line
        for line in BASELINE.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.startswith("#")
    ]
    new = [f for f in findings if f not in baseline]
    gone = [f for f in baseline if f not in findings]

    for f in gone:
        print(f"  fixed:  {f}")
    for f in new:
        print(f"  NEW:    {f}")

    if new:
        print(
            f"\nreachability regressed: {len(new)} new unreachable item(s), "
            f"{len(findings)} total against a baseline of {len(baseline)}.\n"
            "Wire it up, or run --update and say why in the commit message."
        )
        return 1

    if gone:
        print(
            f"\n{len(gone)} item(s) fixed. Run --update to lower the baseline "
            f"to {len(findings)}."
        )
    else:
        print(f"reachability holding at {len(findings)} known items.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
