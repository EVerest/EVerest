// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Gate: no assertion may take a guard in its condition.
//!
//! `assert_eq!` and `assert_ne!` evaluate their operands into a `match`
//! scrutinee, and a scrutinee's temporaries live until the end of the match --
//! which includes the arm that formats the panic. A `MutexGuard` created in an
//! operand is therefore still held while the failure message is built, so a
//! message that locks the same mutex deadlocks instead of failing.
//!
//! `assert!` happens to escape this today, because the condition of an `if` is
//! a terminating temporary scope and the guard drops before the panic block.
//! That is a difference between two macros that read alike, it is not written
//! down where anyone editing a test would see it, and turning an `assert!` into
//! an `assert_eq!` silently arms the deadlock. This gate therefore rejects a
//! guard in the condition of either, so nobody has to know which is which.
//!
//! The fix is to snapshot before the assertion and name the snapshot in the
//! message:
//!
//! ```ignore
//! let events_now = events.lock().unwrap().clone();
//! assert!(events_now.is_empty(), "{fact:?} reached a path: {events_now:?}");
//! ```
//!
//! This walks `src/` rather than a fixed list, so a file added later is covered
//! without touching this test. Under `scripts/test-core.sh` the crate is copied
//! without `src/main.rs`, so that file is not scanned there; it holds no
//! violation today and its own tests run under `scripts/test-boundary.sh`.

use std::fs;
use std::path::{Path, PathBuf};

/// Assertion macros whose condition is checked, with how many leading
/// arguments make up that condition. Longest name first: the scan takes the
/// first match, and `assert` is a prefix of nothing but `assert_eq`/`assert_ne`
/// only in the other direction.
const MACROS: [(&str, usize); 6] = [
    ("debug_assert_eq", 2),
    ("debug_assert_ne", 2),
    ("debug_assert", 1),
    ("assert_eq", 2),
    ("assert_ne", 2),
    ("assert", 1),
];

/// Guard-producing methods. Every one of these is nullary, which is what keeps
/// `self.read(out)` and friends out of the net.
const GUARDS: [&str; 10] = [
    "lock",
    "borrow_mut",
    "borrow",
    "try_lock",
    "try_borrow_mut",
    "try_borrow",
    "read",
    "write",
    "try_read",
    "try_write",
];

/// Blank string literals, char literals and comments, preserving length and
/// line structure. Two things depend on this. Structural scanning below must
/// not see a brace or comma that lives inside a literal, and this file's own
/// needles are literals, so masking is what stops the gate matching itself.
fn mask(src: &str) -> Vec<char> {
    let c: Vec<char> = src.chars().collect();
    let mut out: Vec<char> = Vec::with_capacity(c.len());
    let blank = |ch: char| if ch == '\n' { '\n' } else { ' ' };
    let mut i = 0;
    while i < c.len() {
        // line comment
        if c[i] == '/' && i + 1 < c.len() && c[i + 1] == '/' {
            while i < c.len() && c[i] != '\n' {
                out.push(' ');
                i += 1;
            }
            continue;
        }
        // block comment, nesting as Rust allows
        if c[i] == '/' && i + 1 < c.len() && c[i + 1] == '*' {
            let mut depth = 0usize;
            while i < c.len() {
                if c[i] == '/' && i + 1 < c.len() && c[i + 1] == '*' {
                    depth += 1;
                    out.push(' ');
                    out.push(' ');
                    i += 2;
                    continue;
                }
                if c[i] == '*' && i + 1 < c.len() && c[i + 1] == '/' {
                    depth -= 1;
                    out.push(' ');
                    out.push(' ');
                    i += 2;
                    if depth == 0 {
                        break;
                    }
                    continue;
                }
                out.push(blank(c[i]));
                i += 1;
            }
            continue;
        }
        // raw string, with any number of hashes; `br"..."` arrives here at the `r`
        if c[i] == 'r' {
            let mut j = i + 1;
            let mut hashes = 0usize;
            while j < c.len() && c[j] == '#' {
                hashes += 1;
                j += 1;
            }
            if j < c.len() && c[j] == '"' {
                out.extend(std::iter::repeat_n(' ', j + 1 - i));
                j += 1;
                while j < c.len() {
                    if c[j] == '"' {
                        let mut k = j + 1;
                        let mut n = 0usize;
                        while k < c.len() && n < hashes && c[k] == '#' {
                            k += 1;
                            n += 1;
                        }
                        if n == hashes {
                            out.extend(std::iter::repeat_n(' ', k - j));
                            j = k;
                            break;
                        }
                    }
                    out.push(blank(c[j]));
                    j += 1;
                }
                i = j;
                continue;
            }
        }
        // ordinary or byte string
        if c[i] == '"' {
            out.push(' ');
            let mut j = i + 1;
            while j < c.len() {
                if c[j] == '\\' {
                    out.push(' ');
                    if j + 1 < c.len() {
                        out.push(blank(c[j + 1]));
                    }
                    j += 2;
                    continue;
                }
                if c[j] == '"' {
                    out.push(' ');
                    j += 1;
                    break;
                }
                out.push(blank(c[j]));
                j += 1;
            }
            i = j;
            continue;
        }
        // char literal, told from a lifetime by the closing quote
        if c[i] == '\'' {
            let escaped = i + 1 < c.len() && c[i + 1] == '\\';
            let plain = i + 2 < c.len() && c[i + 2] == '\'';
            if escaped || plain {
                let mut j = i + 1;
                if escaped {
                    j += 1;
                }
                j += 1;
                while j < c.len() && c[j] != '\'' {
                    j += 1;
                }
                if j < c.len() {
                    j += 1;
                }
                out.extend(std::iter::repeat_n(' ', j - i));
                i = j;
                continue;
            }
        }
        out.push(c[i]);
        i += 1;
    }
    out
}

fn is_ident(ch: char) -> bool {
    ch.is_alphanumeric() || ch == '_'
}

/// Byte-for-byte the condition arguments of the macro whose body is `body`.
fn condition_of(body: &[char], leading: usize) -> String {
    let mut depth = 0i32;
    let mut args = 0usize;
    let mut end = body.len();
    for (i, &ch) in body.iter().enumerate() {
        match ch {
            '(' | '[' | '{' => depth += 1,
            ')' | ']' | '}' => depth -= 1,
            ',' if depth == 0 => {
                args += 1;
                if args == leading {
                    end = i;
                    break;
                }
            }
            _ => {}
        }
    }
    body[..end].iter().collect()
}

/// A nullary guard call -- `.lock()`, `.borrow_mut()` -- anywhere in `cond`.
fn guard_in(cond: &str) -> Option<&'static str> {
    let c: Vec<char> = cond.chars().collect();
    for name in GUARDS {
        let n: Vec<char> = name.chars().collect();
        let mut i = 0;
        while i + n.len() < c.len() {
            if c[i] != '.' {
                i += 1;
                continue;
            }
            let mut j = i + 1;
            while j < c.len() && c[j].is_whitespace() {
                j += 1;
            }
            if j + n.len() > c.len() || c[j..j + n.len()] != n[..] {
                i += 1;
                continue;
            }
            let mut k = j + n.len();
            if k < c.len() && is_ident(c[k]) {
                i += 1;
                continue;
            }
            while k < c.len() && c[k].is_whitespace() {
                k += 1;
            }
            if k >= c.len() || c[k] != '(' {
                i += 1;
                continue;
            }
            k += 1;
            while k < c.len() && c[k].is_whitespace() {
                k += 1;
            }
            if k < c.len() && c[k] == ')' {
                return Some(name);
            }
            i += 1;
        }
    }
    None
}

fn rs_files(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(entries) = fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            rs_files(&path, out);
        } else if path.extension().is_some_and(|e| e == "rs") {
            out.push(path);
        }
    }
}

/// Every guard taken in an assertion condition, as `file:line (macro, method)`.
fn violations(root: &Path) -> Vec<String> {
    let mut files = Vec::new();
    rs_files(root, &mut files);
    files.sort();
    let mut found = Vec::new();
    for file in files {
        let Ok(text) = fs::read_to_string(&file) else {
            continue;
        };
        let masked = mask(&text);
        let mut i = 0;
        while i < masked.len() {
            if i > 0 && is_ident(masked[i - 1]) {
                i += 1;
                continue;
            }
            let hit = MACROS.iter().find(|(name, _)| {
                let n: Vec<char> = name.chars().collect();
                i + n.len() <= masked.len()
                    && masked[i..i + n.len()] == n[..]
                    && !masked
                        .get(i + n.len())
                        .copied()
                        .is_some_and(is_ident)
            });
            let Some((name, leading)) = hit else {
                i += 1;
                continue;
            };
            let mut j = i + name.chars().count();
            while j < masked.len() && masked[j].is_whitespace() {
                j += 1;
            }
            if j >= masked.len() || masked[j] != '!' {
                i += 1;
                continue;
            }
            j += 1;
            while j < masked.len() && masked[j].is_whitespace() {
                j += 1;
            }
            if j >= masked.len() || masked[j] != '(' {
                i += 1;
                continue;
            }
            let open = j;
            let mut depth = 0i32;
            let mut k = open;
            while k < masked.len() {
                match masked[k] {
                    '(' => depth += 1,
                    ')' => {
                        depth -= 1;
                        if depth == 0 {
                            break;
                        }
                    }
                    _ => {}
                }
                k += 1;
            }
            if k >= masked.len() {
                i += 1;
                continue;
            }
            let cond = condition_of(&masked[open + 1..k], *leading);
            if let Some(method) = guard_in(&cond) {
                let line = masked[..i].iter().filter(|&&ch| ch == '\n').count() + 1;
                let shown = cond.split_whitespace().collect::<Vec<_>>().join(" ");
                let shown: String = shown.chars().take(90).collect();
                found.push(format!(
                    "{}:{} {}! takes .{}() in its condition -- {}",
                    file.display(),
                    line,
                    name,
                    method,
                    shown
                ));
            }
            i = k + 1;
        }
    }
    found
}

#[test]
fn no_assertion_takes_a_guard_in_its_condition() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("src");
    assert!(root.is_dir(), "no src/ under {}", root.display());

    let found = violations(&root);
    assert!(
        found.is_empty(),
        "{} assertion(s) take a guard in the condition, so the guard is still \
         held while the failure message is built. Snapshot before the \
         assertion and name the snapshot in the message:\n\
         \x20   let events_now = events.lock().unwrap().clone();\n\
         \x20   assert!(events_now.is_empty(), \"reached a path: {{events_now:?}}\");\n\
         {}",
        found.len(),
        found.join("\n")
    );
}

/// The scanner must actually see the shapes it claims to, and must not fire on
/// the shapes it claims to leave alone. Without this a silent regression in
/// `mask` or `guard_in` turns the gate above into a test that always passes.
#[test]
fn the_scanner_sees_a_guard_and_only_a_guard() {
    let dir = std::env::temp_dir().join(format!(
        "assert-hygiene-{}-{:?}",
        std::process::id(),
        std::thread::current().id()
    ));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).expect("scratch dir");

    let caught = [
        ("plain", "fn a(){ assert!(m.lock().unwrap().is_empty()); }"),
        ("eq", "fn a(){ assert_eq!(*m.lock().unwrap(), v, \"x\"); }"),
        ("ne", "fn a(){ assert_ne!(*m.lock().unwrap(), v); }"),
        ("refcell", "fn a(){ assert!(c.borrow().ok); }"),
        ("nested", "fn a(){ assert!(matches!(m.lock().unwrap().as_slice(), [])); }"),
        ("spaced", "fn a(){ assert!( m . lock ( ) . unwrap().x ); }"),
        ("debug", "fn a(){ debug_assert_eq!(*m.lock().unwrap(), v); }"),
    ];
    for (name, body) in caught {
        let f = dir.join(format!("{name}.rs"));
        fs::write(&f, body).unwrap();
        let v = violations(&dir);
        assert_eq!(v.len(), 1, "{name}: expected one violation, got {v:?}");
        fs::remove_file(&f).unwrap();
    }

    let ignored = [
        // The guard is snapshotted out before the statement: the shape we want.
        ("fixed", "fn a(){ let n = m.lock().unwrap().clone(); assert!(n.is_empty(), \"{n:?}\"); }"),
        // A guard in the MESSAGE only is harmless; the condition holds nothing.
        ("msg_only", "fn a(){ assert!(flag, \"{:?}\", m.lock().unwrap()); }"),
        // Not nullary, so not a guard.
        ("read_arg", "fn a(){ assert!(self.read(out).is_ok()); }"),
        // The needle inside a literal must not count, or this file fails itself.
        ("in_string", "fn a(){ assert!(s == \".lock()\"); }"),
        ("in_comment", "fn a(){ /* .lock() */ assert!(x); }"),
        // A quote inside a char literal must not desync the masker.
        ("char_quote", "fn a(){ let q = '\"'; assert!(x, \"{q}\"); }"),
        ("not_an_assert", "fn a(){ let v = m.lock().unwrap(); }"),
    ];
    for (name, body) in ignored {
        let f = dir.join(format!("{name}.rs"));
        fs::write(&f, body).unwrap();
        let v = violations(&dir);
        assert!(v.is_empty(), "{name}: expected no violation, got {v:?}");
        fs::remove_file(&f).unwrap();
    }

    let _ = fs::remove_dir_all(&dir);
}
