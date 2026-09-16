// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Per session event log: one directory per session holding a CSV and an HTML
//! transcript of every EVSE, car and system message.
//!
//! A port of `modules/EVSE/EvseManager/SessionLog.cpp`. The C++ version owns two
//! `std::ofstream`s, reads the clock on every record and serializes concurrent
//! writers under a mutex. Here `SessionLogger` decides and the boundary
//! performs: every call returns the filesystem work as an `Action` list, the
//! timestamp arrives as an already formatted RFC 3339 string, and the single
//! writer removes the mutex. That keeps this file inside the rule that `core`
//! performs no I/O and reads no clock while still owning the parts that are
//! worth testing: path derivation, the containment guard, field escaping,
//! record formatting and the incomplete then rename naming.
//!
//! No method returns an error. A logging fault must never stop a charge, so the
//! boundary reports a failed action back through [`SessionLogger::note_failure`]
//! and logging degrades instead of propagating. That replaces the five
//! log-and-continue catch sites of the C++ (`SessionLog.cpp:36-38`, `:58-63`,
//! `:71-73`, `:89-94` and `:105-109`) with a shape that cannot propagate at all.

use serde::Serialize;

use super::config::LoggingSettings;
use super::path::iec::AcState;
use super::session::StartSessionReason;

/// CSV transcript while the session is live.
pub const CSV_INCOMPLETE: &str = "incomplete-eventlog.csv";
/// CSV transcript once the session has stopped.
pub const CSV_COMPLETE: &str = "eventlog.csv";
/// HTML transcript while the session is live.
pub const HTML_INCOMPLETE: &str = "incomplete-eventlog.html";
/// HTML transcript once the session has stopped.
pub const HTML_COMPLETE: &str = "eventlog.html";

/// Who sent the message being recorded.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Origin {
    Evse,
    Car,
    Sys,
}

impl Origin {
    pub fn as_str(&self) -> &'static str {
        match self {
            Origin::Evse => "EVSE",
            Origin::Car => "CAR",
            Origin::Sys => "SYS",
        }
    }

    /// The other end of the link, empty for a system message.
    pub fn target(&self) -> &'static str {
        match self {
            Origin::Evse => "CAR",
            Origin::Car => "EVSE",
            Origin::Sys => "",
        }
    }
}

/// The protocol payload attached to a message, all four representations the C++
/// records. Empty strings mean absent.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Payload<'a> {
    pub xml: &'a str,
    pub xml_hex: &'a str,
    pub xml_base64: &'a str,
    pub json: &'a str,
}

/// The same four representations, owned, so a record can travel as an effect.
///
/// Boxed at its only use site. Four strings would otherwise widen every
/// `Effect` in the module for the one variant that carries a payload, and only
/// the ISO 15118 message feed ever fills it.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct OwnedPayload {
    pub xml: String,
    pub xml_hex: String,
    pub xml_base64: String,
    pub json: String,
}

impl OwnedPayload {
    pub fn borrow(&self) -> Payload<'_> {
        Payload {
            xml: &self.xml,
            xml_hex: &self.xml_hex,
            xml_base64: &self.xml_base64,
            json: &self.json,
        }
    }

    pub fn is_empty(&self) -> bool {
        self.xml.is_empty()
            && self.xml_hex.is_empty()
            && self.xml_base64.is_empty()
            && self.json.is_empty()
    }
}

/// What the core asks the session log to do.
///
/// The core decides which records a session owes and in what order; it holds no
/// clock and no filesystem, so the boundary stamps the time and performs the
/// work. Ordering is the whole point of a transcript, so these run on the
/// serial publish lane rather than the pool; see `Effect::context`.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum SessionLogEffect {
    /// `SessionLog::startSession`, from the session started signal
    /// (`evse/evse_managerImpl.cpp:169`).
    ///
    /// Carries the session identity and not the derived suffix: the
    /// `logfile_suffix` rule is one of the four session log configuration keys
    /// and all four are read at one place. See [`suffix_for`].
    Start { session_uuid: String },
    /// `SessionLog::stopSession`, from `SessionFinished`
    /// (`evse/evse_managerImpl.cpp:329`).
    Stop,
    /// One record. `origin` is which of the three `output` types the C++ would
    /// have used.
    Record {
        origin: Origin,
        iso15118: bool,
        msg: String,
        payload: Option<Box<OwnedPayload>>,
    },
}

impl SessionLogEffect {
    /// An `evse` record with no payload, the two argument form the C++ uses at
    /// every site but one (`SessionLog.cpp:176-182`).
    pub fn evse(msg: impl Into<String>) -> Self {
        Self::Record {
            origin: Origin::Evse,
            iso15118: false,
            msg: msg.into(),
            payload: None,
        }
    }
}

/// Filesystem work the boundary performs on the logger's behalf.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Action {
    /// Create this directory and every missing parent.
    CreateDir {
        path: String,
    },
    /// Create or truncate this file.
    Truncate {
        path: String,
    },
    /// Append this text to this file and flush.
    Append {
        path: String,
        text: String,
    },
    Rename {
        from: String,
        to: String,
    },
}

/// One line for the EVerest log, the parallel output the C++ emits alongside the
/// session files (`SessionLog.cpp:210-227`).
///
/// Returned rather than written. `core` decides the line and the boundary is
/// the only side that writes it, so a record cannot reach the EVerest log twice
/// under two spellings.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LogLine {
    pub origin: Origin,
    pub iso15118: bool,
    pub text: String,
}

impl LogLine {
    /// The line as the C++ writes it (`SessionLog.cpp:219`, `:223-224` and
    /// `:228`). A car message is indented by the same 36 spaces, which is what
    /// puts the two directions in separate columns of a terminal.
    ///
    /// The C++ wraps the EVSE and car lines in ANSI color (`\033[1;34m` and
    /// `\033[1;33m`). Not reproduced: the sequences are terminal presentation,
    /// they corrupt a captured log file, and nothing parses them. The text is
    /// identical.
    ///
    /// A system message carries no protocol token, which is the one asymmetry
    /// in the C++: `:228` prints `msg` and not the `ISO`/`IEC` form the other
    /// two get.
    pub fn render(&self) -> String {
        match self.origin {
            Origin::Sys => format!("SYS  {}", self.text),
            Origin::Evse => format!("EVSE {} {}", self.protocol(), self.text),
            Origin::Car => format!("{CAR_LINE_INDENT}CAR {} {}", self.protocol(), self.text),
        }
    }

    fn protocol(&self) -> &'static str {
        if self.iso15118 {
            "ISO"
        } else {
            "IEC"
        }
    }
}

/// `SessionLog.cpp:223`, which indents the car side by this many spaces.
const CAR_LINE_INDENT: &str = "                                    ";

/// The external publication the C++ hands to its MQTT callback
/// (`SessionLog.cpp:244-250`, wired at `EvseManager.cpp:138-141`).
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct Publication {
    pub origin: String,
    pub target: String,
    pub iso15118: bool,
    pub msg: String,
}

impl Publication {
    /// The exact JSON body the C++ publishes.
    pub fn payload(&self) -> String {
        serde_json::to_string(self).unwrap_or_default()
    }
}

/// Everything one logger call asks the boundary to do.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct LogOutput {
    pub actions: Vec<Action>,
    pub lines: Vec<LogLine>,
    pub publications: Vec<Publication>,
}

impl LogOutput {
    fn absorb(&mut self, other: LogOutput) {
        self.actions.extend(other.actions);
        self.lines.extend(other.lines);
        self.publications.extend(other.publications);
    }
}

struct ActiveSession {
    dir: String,
    csv: String,
    html: String,
}

/// The per session transcript.
///
/// **A logger that exists is enabled.** `for_settings` is the only constructor
/// and it answers `None` for a deployment with `session_logging` off, so there
/// is no `enabled` field, no `enable()` and none of the three
/// `if !self.enabled` early returns this type used to carry. Two of those three
/// were already known to be dead code, recorded as unreachable mutants in
/// `the_redundant_guards_are_redundant_and_not_merely_untested`; the shape they
/// came from is the one that let 447 lines of this file ship with a green suite
/// and no constructor call in `main` at all.
pub struct SessionLogger {
    root: String,
    xml_output: bool,
    session: Option<ActiveSession>,
}

impl SessionLogger {
    /// `None` when `session_logging` is off, which is the whole of the feature
    /// gate. All four session log configuration keys are read here and by
    /// nothing else, except `logfile_suffix`, which is per session and is read
    /// by the boundary's own `session_log`.
    pub fn for_settings(settings: &LoggingSettings) -> Option<Self> {
        settings.session_logging.then(|| Self {
            root: normalize(&settings.session_logging_path),
            xml_output: settings.session_logging_xml,
            session: None,
        })
    }

    /// What to tell the operator at boot about the state of this subsystem.
    ///
    /// It exists because the failure this replaces was silence. Turning
    /// `session_logging` on and then finding nothing under the configured root
    /// is the expected state of a port that has not charged yet: a transcript
    /// is opened per session, not at boot. Without a line saying so, a correct
    /// configuration and a broken one look identical from the filesystem.
    ///
    /// Only a logger that exists announces itself, which is the C++ behavior:
    /// nothing is announced for a subsystem nobody asked for.
    pub fn announcement(&self) -> String {
        format!(
            "session logging enabled: one directory per session under {}, \
             each holding {CSV_INCOMPLETE} and {HTML_INCOMPLETE} while the session runs \
             and {CSV_COMPLETE} and {HTML_COMPLETE} once it ends. Nothing is written \
             until a session starts. Payload output is {}. The same config key puts the \
             ISO 15118 stack into debug mode, which is a separate subsystem in a \
             separate module.",
            self.root,
            if self.xml_output { "on" } else { "off" },
        )
    }

    pub fn is_session_active(&self) -> bool {
        self.session.is_some()
    }

    pub fn root(&self) -> &str {
        &self.root
    }

    pub fn session_dir(&self) -> Option<&str> {
        self.session.as_ref().map(|s| s.dir.as_str())
    }

    /// Derives the session directory, guards containment and returns the work
    /// that opens a fresh pair of transcripts. `now` is an already formatted
    /// RFC 3339 timestamp, so no clock is read here.
    pub fn start_session(&mut self, now: &str, suffix: &str) -> LogOutput {
        let mut out = LogOutput::default();
        if self.session.is_some() {
            out.absorb(self.stop_session(now));
        }

        let dir = normalize(&format!("{}/{}-{}", self.root, now, suffix));
        // A configured or session derived suffix must not escape the root. The
        // check is lexical, so a directory component that is itself a symlink
        // out of the root is not caught; that needs the boundary, which is the
        // only side that can read the filesystem.
        if !is_contained(&self.root, &dir) {
            log::error!(
                "Session logpath {dir} is not below the configured root {}, session logging skipped",
                self.root
            );
            return out;
        }

        let csv = format!("{dir}/{CSV_INCOMPLETE}");
        let html = format!("{dir}/{HTML_INCOMPLETE}");
        out.actions.push(Action::CreateDir {
            path: self.root.clone(),
        });
        out.actions.push(Action::CreateDir { path: dir.clone() });
        out.actions.push(Action::Truncate { path: csv.clone() });
        out.actions.push(Action::Truncate { path: html.clone() });
        out.actions.push(Action::Append {
            path: html.clone(),
            text: html_header(suffix),
        });
        self.session = Some(ActiveSession { dir, csv, html });
        out.absorb(self.sys(now, "Session logging started."));
        out
    }

    /// Closes the transcripts and renames both away from their incomplete names,
    /// which is what marks a session log as trustworthy to a reader.
    pub fn stop_session(&mut self, now: &str) -> LogOutput {
        let mut out = LogOutput::default();
        if self.session.is_none() {
            return out;
        }
        out.absorb(self.sys(now, "Session logging stopped."));

        let session = self.session.take().expect("session present");
        out.actions.push(Action::Append {
            path: session.html.clone(),
            text: "</table></body></html>\n".to_string(),
        });
        out.actions.push(Action::Rename {
            from: session.csv,
            to: format!("{}/{CSV_COMPLETE}", session.dir),
        });
        out.actions.push(Action::Rename {
            from: session.html,
            to: format!("{}/{HTML_COMPLETE}", session.dir),
        });
        out
    }

    pub fn evse(&mut self, now: &str, iso15118: bool, msg: &str, payload: Payload) -> LogOutput {
        self.record(Origin::Evse, now, iso15118, msg, payload)
    }

    pub fn car(&mut self, now: &str, iso15118: bool, msg: &str, payload: Payload) -> LogOutput {
        self.record(Origin::Car, now, iso15118, msg, payload)
    }

    pub fn sys(&mut self, now: &str, msg: &str) -> LogOutput {
        self.record(Origin::Sys, now, false, msg, Payload::default())
    }

    /// Reports an action the boundary could not carry out. Failing to create a
    /// directory or to open a transcript ends session logging, mirroring the C++
    /// which leaves `session_active` false; a failed record or rename is logged
    /// and ignored. Nothing is returned, so a logging fault cannot reach the
    /// charging logic.
    pub fn note_failure(&mut self, action: &Action, reason: &str) {
        match action {
            Action::CreateDir { path } | Action::Truncate { path } => {
                log::error!("Cannot open session log {path}: {reason}, session logging disabled");
                self.session = None;
            }
            Action::Append { path, .. } => {
                log::error!("Cannot write session log {path}: {reason}");
            }
            Action::Rename { from, to } => {
                log::error!("Could not rename {from} to {to}: {reason}");
            }
        }
    }

    fn record(
        &mut self,
        origin: Origin,
        now: &str,
        iso15118: bool,
        msg: &str,
        payload: Payload,
    ) -> LogOutput {
        let mut out = LogOutput::default();
        let Some(session) = self.session.as_ref() else {
            return out;
        };

        // Divergence 5b: the payload is recorded as received rather than pretty
        // printed, so no XML library is pulled in. Content is identical.
        let body = if !payload.xml.is_empty() {
            payload.xml
        } else {
            payload.json
        };

        out.actions.push(Action::Append {
            path: session.csv.clone(),
            text: format!(
                "{},{},{},{}\n",
                csv_field(now),
                csv_field(origin.as_str()),
                csv_field(msg),
                csv_field(body)
            ),
        });

        let target = origin.target();
        let from_car = origin == Origin::Car;
        out.actions.push(Action::Append {
            path: session.html.clone(),
            text: format!(
                "<tr class=\"{origin}\"> <td>{ts}</td> <td>{origin}&gt;{target}</td> \
                 <td><b>{evse_msg}</b></td><td><b>{car_msg}</b></td> <td><pre lang=\"xml\">{body}</pre></td> \
                 <td><pre lang=\"xml\">{hex}</pre></td> <td><pre lang=\"xml\">{base64}</pre></td> </tr>\n",
                origin = origin.as_str(),
                ts = html_escape(now),
                target = target,
                evse_msg = if from_car { String::new() } else { html_escape(msg) },
                car_msg = if from_car { html_escape(msg) } else { String::new() },
                body = html_escape(body),
                hex = html_escape(payload.xml_hex),
                base64 = html_escape(payload.xml_base64),
            ),
        });

        let mut text = msg.to_string();
        if self.xml_output && origin != Origin::Sys {
            text.push_str(body);
        }
        out.lines.push(LogLine {
            origin,
            iso15118,
            text,
        });
        out.publications.push(Publication {
            origin: origin.as_str().to_string(),
            target: target.to_string(),
            iso15118,
            msg: msg.to_string(),
        });
        out
    }
}

/// The opening markup and stylesheet of a session transcript, ported from
/// `SessionLog.cpp:113-138`.
fn html_header(suffix: &str) -> String {
    format!(
        "<html><head><title>EVerest log session {}</title>\n\
         <style>\
         .log {{ font-family: Arial, Helvetica, sans-serif; border-collapse: collapse; width: 100%; }}\
         .log td, .log th {{ border: 1px solid #ddd; padding: 8px; vertical-align: top; }}\
         .log tr.CAR {{ background-color: #E4E6F2; }}\
         .log tr.EVSE {{ background-color: #F2F0E4; }}\
         .log tr.SYS {{ background-color: white; }}\
         .log th {{ padding-top: 12px; padding-bottom: 12px; text-align: left; vertical-align: top; \
         background-color: #04AA6D; color: white; }}\
         </style>\
         </head><body><table class=\"log\">\n",
        html_escape(suffix)
    )
}

/// The special `logfile_suffix` value that means the session identity
/// (`modules/EVSE/EvseManager/manifest.yaml:210`).
pub const SUFFIX_FROM_SESSION: &str = "session_uuid";

/// The session directory suffix a start should use.
///
/// `evse/evse_managerImpl.cpp:170`:
/// `config.logfile_suffix == "session_uuid" ? session_uuid : config.logfile_suffix`.
/// A configured literal is used verbatim, which is why the containment guard in
/// [`SessionLogger::start_session`] exists: this value is operator supplied.
pub fn suffix_for(logfile_suffix: &str, session_uuid: &str) -> String {
    if logfile_suffix == SUFFIX_FROM_SESSION {
        session_uuid.to_string()
    } else {
        logfile_suffix.to_string()
    }
}

/// The transcript's spelling of a charger state, `Charger::evse_state_to_string`
/// (`Charger.cpp:1871-1910`).
///
/// It lives here rather than on `AcState` because the transcript is its only
/// consumer and these are the C++ strings, not the port's names: three of them
/// disagree with the variant (`Wait for Auth`, `Car Paused`, `EVSE Paused`), and
/// a reader diffing two transcripts is comparing against the C++ spelling.
///
/// `AcState::Startup` has no C++ counterpart: the C++ value initializes both
/// state fields to `Idle` (`Charger.cpp:54`, `:69`) and has no startup state.
/// It is spelled anyway rather than left unnamed, because a state that cannot
/// be spelled is a state whose transition line silently disappears. It is not
/// reachable from a transcript in practice, since the port leaves `Startup`
/// long before any session opens one.
///
/// Three C++ states have no `AcState`: `T_step_EF`, `T_step_X1` and
/// `SwitchPhases`. The port models the pilot detour as a sub state of the state
/// it detours from (`path::ac::Detour`) and has no phase switching state at all,
/// so a transcript here carries fewer transition lines than a C++ one. Recorded
/// in `docs/architecture.md`.
pub fn charger_state_name(state: AcState) -> &'static str {
    match state {
        AcState::Startup => "Startup",
        AcState::Disabled => "Disabled",
        AcState::Idle => "Idle",
        AcState::WaitingForAuthentication => "Wait for Auth",
        AcState::PrepareCharging => "PrepareCharging",
        AcState::Charging => "Charging",
        AcState::ChargingPausedEv => "Car Paused",
        AcState::ChargingPausedEvse => "EVSE Paused",
        // `Charger::evse_state_to_string` spells it without a separator.
        AcState::SwitchPhases => "SwitchPhases",
        AcState::Reinit => "Reinit",
        AcState::StoppingCharging => "StoppingCharging",
        AcState::Finished => "Finished",
    }
}

/// The wire spelling of a start reason, `start_session_reason_to_string`
/// (`types/evse_manager.yaml:77-78`), which is what the C++ writes into the
/// `Session Started` line (`evse/evse_managerImpl.cpp:174-175`).
pub fn start_reason_name(reason: StartSessionReason) -> &'static str {
    match reason {
        StartSessionReason::EvConnected => "EVConnected",
        StartSessionReason::Authorized => "Authorized",
    }
}

/// The transition line the C++ writes at the head of every state machine pass
/// (`Charger.cpp:166-168`), before the entered state's body runs.
pub fn state_transition_message(before: AcState, after: AcState) -> String {
    format!(
        "Charger state: {}->{}",
        charger_state_name(before),
        charger_state_name(after)
    )
}

/// Lexical resolution, the part of `std::filesystem::weakly_canonical` that
/// needs no filesystem: `.` and `..` components collapse and repeated
/// separators fold. Infallible, which is why the C++ resolve failure paths at
/// `SessionLog.cpp:36-38` and `:71-73` have no counterpart here.
fn normalize(path: &str) -> String {
    let absolute = path.starts_with('/');
    let mut parts: Vec<&str> = Vec::new();
    for part in path.split('/') {
        match part {
            "" | "." => {}
            ".." => match parts.last() {
                Some(&last) if last != ".." => {
                    parts.pop();
                }
                Some(_) => parts.push(".."),
                None => {
                    if !absolute {
                        parts.push("..");
                    }
                }
            },
            other => parts.push(other),
        }
    }
    let joined = parts.join("/");
    if absolute {
        format!("/{joined}")
    } else if joined.is_empty() {
        ".".to_string()
    } else {
        joined
    }
}

/// True when `candidate` is a strict descendant of `root`.
fn is_contained(root: &str, candidate: &str) -> bool {
    let root = root.trim_end_matches('/');
    candidate.len() > root.len() + 1
        && candidate.starts_with(root)
        && candidate.as_bytes()[root.len()] == b'/'
}

/// RFC 4180 field: always quoted, embedded quotes doubled. Divergence 5: the
/// C++ quotes without doubling (`SessionLog.cpp:232`), so one quote in a message
/// corrupts every following field of that record.
fn csv_field(value: &str) -> String {
    format!("\"{}\"", value.replace('"', "\"\""))
}

/// Divergence 5: the C++ encoder replaces the angle brackets only
/// (`SessionLog.cpp:261-266`), leaving the ampersand bare. Escaping character by
/// character also makes the ordering hazard unrepresentable: an ampersand
/// introduced by an earlier replacement can never be escaped a second time.
fn html_escape(value: &str) -> String {
    let mut out = String::with_capacity(value.len());
    for character in value.chars() {
        match character {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            '\'' => out.push_str("&apos;"),
            other => out.push(other),
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    const ROOT: &str = "/tmp/everest-session-logs";
    const TS: &str = "2026-08-31T10:11:12.000Z";

    /// The settings a logger exists on. `session_logging` off is not a shape
    /// this fixture can build, because it is not a shape the type has.
    fn logging(root: &str, xml_output: bool) -> LoggingSettings {
        LoggingSettings {
            session_logging: true,
            session_logging_path: root.to_string(),
            session_logging_xml: xml_output,
            logfile_suffix: String::new(),
            dbg_hlc_auth_after_tstep: false,
        }
    }

    fn logger() -> SessionLogger {
        logger_at(ROOT, true)
    }

    fn logger_at(root: &str, xml_output: bool) -> SessionLogger {
        SessionLogger::for_settings(&logging(root, xml_output)).expect("logging is on")
    }

    fn started() -> (SessionLogger, LogOutput) {
        let mut logger = logger();
        let out = logger.start_session(TS, "sessionid");
        (logger, out)
    }

    fn appends_to<'a>(out: &'a LogOutput, suffix: &str) -> Vec<&'a str> {
        out.actions
            .iter()
            .filter_map(|action| match action {
                Action::Append { path, text } if path.ends_with(suffix) => Some(text.as_str()),
                _ => None,
            })
            .collect()
    }

    /// RFC 4180 reader, deliberately independent of the writer under test.
    fn parse_csv_record(line: &str) -> Vec<String> {
        let mut fields = Vec::new();
        let mut field = String::new();
        let mut chars = line.trim_end_matches('\n').chars().peekable();
        while let Some(c) = chars.next() {
            match c {
                '"' => loop {
                    match chars.next() {
                        Some('"') => {
                            if chars.peek() == Some(&'"') {
                                chars.next();
                                field.push('"');
                            } else {
                                break;
                            }
                        }
                        Some(other) => field.push(other),
                        None => panic!("unterminated quoted field in {line:?}"),
                    }
                },
                ',' => fields.push(std::mem::take(&mut field)),
                other => field.push(other),
            }
        }
        fields.push(field);
        fields
    }

    /// Every ampersand must open a known entity and every tag must close in
    /// order, which is what "well formed" means for the fragments written here.
    fn assert_well_formed_html(fragment: &str) {
        let entities = ["&amp;", "&lt;", "&gt;", "&quot;", "&apos;"];
        for (index, _) in fragment.match_indices('&') {
            assert!(
                entities
                    .iter()
                    .any(|entity| fragment[index..].starts_with(entity)),
                "bare ampersand at {index} in {fragment:?}"
            );
        }
        let mut stack: Vec<String> = Vec::new();
        let mut cursor = 0;
        while let Some(offset) = fragment[cursor..].find('<') {
            let open = cursor + offset;
            let close = open
                + fragment[open..]
                    .find('>')
                    .unwrap_or_else(|| panic!("unclosed tag at {open} in {fragment:?}"));
            let inner = &fragment[open + 1..close];
            let name = inner
                .trim_start_matches('/')
                .split_whitespace()
                .next()
                .unwrap_or_default()
                .to_string();
            if inner.starts_with('/') {
                assert_eq!(stack.pop(), Some(name), "mismatched close in {fragment:?}");
            } else if !inner.ends_with('/') && !inner.starts_with('!') {
                stack.push(name);
            }
            cursor = close + 1;
        }
        assert!(stack.is_empty(), "unclosed tags {stack:?} in {fragment:?}");
    }

    #[test]
    fn a_session_directory_is_named_by_the_timestamp_and_the_configured_suffix() {
        let (logger, out) = started();
        let expected = format!("{ROOT}/{TS}-sessionid");
        assert_eq!(logger.session_dir(), Some(expected.as_str()));
        assert!(out.actions.contains(&Action::CreateDir {
            path: ROOT.to_string()
        }));
        assert!(out.actions.contains(&Action::CreateDir { path: expected }));
    }

    #[test]
    fn the_csv_and_html_files_are_written_incomplete_and_renamed_on_session_stop() {
        let (mut logger, start) = started();
        let dir = format!("{ROOT}/{TS}-sessionid");
        assert!(start.actions.contains(&Action::Truncate {
            path: format!("{dir}/{CSV_INCOMPLETE}"),
        }));
        assert!(start.actions.contains(&Action::Truncate {
            path: format!("{dir}/{HTML_INCOMPLETE}"),
        }));

        let stop = logger.stop_session("2026-08-31T10:20:00.000Z");
        assert!(stop.actions.contains(&Action::Rename {
            from: format!("{dir}/{CSV_INCOMPLETE}"),
            to: format!("{dir}/{CSV_COMPLETE}"),
        }));
        assert!(stop.actions.contains(&Action::Rename {
            from: format!("{dir}/{HTML_INCOMPLETE}"),
            to: format!("{dir}/{HTML_COMPLETE}"),
        }));
        assert!(!logger.is_session_active());
    }

    #[test]
    fn the_closing_html_is_written_before_the_files_are_renamed() {
        let (mut logger, _) = started();
        let stop = logger.stop_session(TS);
        let closing = stop
            .actions
            .iter()
            .position(
                |action| matches!(action, Action::Append { text, .. } if text.contains("</html>")),
            )
            .expect("closing html appended");
        let rename = stop
            .actions
            .iter()
            .position(|action| matches!(action, Action::Rename { .. }))
            .expect("files renamed");
        assert!(closing < rename);
    }

    #[test]
    fn a_suffix_that_escapes_the_configured_root_starts_no_session() {
        let mut logger = logger();
        let out = logger.start_session(TS, "/../../etc/cron.d");
        assert!(!logger.is_session_active());
        assert_eq!(out, LogOutput::default());
    }

    #[test]
    fn a_suffix_of_repeated_parent_directories_starts_no_session() {
        let mut logger = logger();
        assert_eq!(
            logger.start_session(TS, "/../../../../.."),
            LogOutput::default()
        );
        assert!(!logger.is_session_active());
    }

    #[test]
    fn an_absolute_suffix_stays_inside_the_configured_root() {
        let mut logger = logger();
        logger.start_session(TS, "/etc/passwd");
        let dir = logger.session_dir().expect("session started");
        assert!(
            dir.starts_with(&format!("{ROOT}/")),
            "escaped the root: {dir}"
        );
    }

    #[test]
    fn a_symlink_shaped_suffix_stays_inside_the_configured_root() {
        let mut logger = logger();
        logger.start_session(TS, "link->/etc");
        let dir = logger.session_dir().expect("session started");
        assert!(
            dir.starts_with(&format!("{ROOT}/")),
            "escaped the root: {dir}"
        );
    }

    #[test]
    fn an_empty_suffix_still_starts_a_session_under_the_root() {
        let mut logger = logger();
        logger.start_session(TS, "");
        assert_eq!(logger.session_dir(), Some(format!("{ROOT}/{TS}-").as_str()));
    }

    #[test]
    fn a_configured_root_carrying_a_parent_directory_is_resolved_before_it_is_used() {
        let logger = logger_at("/tmp/./logs/../everest-logs/", true);
        assert_eq!(logger.root(), "/tmp/everest-logs");
    }

    #[test]
    fn a_message_carrying_a_quote_an_ampersand_and_an_angle_bracket_parses_as_csv() {
        let (mut logger, _) = started();
        let msg = r#"said "hi" & <ok>"#;
        let out = logger.evse(TS, true, msg, Payload::default());
        let records = appends_to(&out, CSV_INCOMPLETE);
        assert_eq!(records.len(), 1);
        let fields = parse_csv_record(records[0]);
        assert_eq!(
            fields.len(),
            4,
            "unexpected field count in {:?}",
            records[0]
        );
        assert_eq!(fields[0], TS);
        assert_eq!(fields[1], "EVSE");
        assert_eq!(fields[2], msg);
        assert_eq!(fields[3], "");
    }

    #[test]
    fn a_message_carrying_a_quote_an_ampersand_and_an_angle_bracket_is_well_formed_html() {
        let (mut logger, _) = started();
        let out = logger.evse(
            TS,
            true,
            r#"said "hi" & <ok>"#,
            Payload {
                xml: r#"<Msg a="1">x & y</Msg>"#,
                ..Payload::default()
            },
        );
        let rows = appends_to(&out, HTML_INCOMPLETE);
        assert_eq!(rows.len(), 1);
        assert_well_formed_html(rows[0]);
    }

    #[test]
    fn the_html_header_is_well_formed_when_the_suffix_carries_markup() {
        let mut logger = logger();
        let out = logger.start_session(TS, r#"a&b<c>"#);
        let header = appends_to(&out, HTML_INCOMPLETE);
        assert!(!header.is_empty());
        assert!(
            header[0].contains("a&amp;b&lt;c&gt;"),
            "unescaped suffix in {:?}",
            header[0]
        );
    }

    #[test]
    fn an_ampersand_is_escaped_once_rather_than_twice() {
        assert_eq!(html_escape("a & b"), "a &amp; b");
        assert_eq!(html_escape("<a & b>"), "&lt;a &amp; b&gt;");
    }

    #[test]
    fn the_direction_marker_is_not_escaped_a_second_time() {
        let (mut logger, _) = started();
        let out = logger.evse(TS, true, "PowerDelivery", Payload::default());
        let rows = appends_to(&out, HTML_INCOMPLETE);
        assert!(
            rows[0].contains("EVSE&gt;CAR"),
            "missing direction in {:?}",
            rows[0]
        );
        assert!(
            !rows[0].contains("&amp;gt;"),
            "double escaped in {:?}",
            rows[0]
        );
    }

    #[test]
    fn a_csv_field_doubles_an_embedded_quote() {
        assert_eq!(csv_field(r#"a"b"#), r#""a""b""#);
        assert_eq!(csv_field("plain"), r#""plain""#);
    }

    #[test]
    fn an_iso_payload_is_written_unformatted() {
        let (mut logger, _) = started();
        let xml = "<Msg><A>1</A></Msg>";
        let out = logger.evse(
            TS,
            true,
            "msg",
            Payload {
                xml,
                ..Payload::default()
            },
        );
        let fields = parse_csv_record(appends_to(&out, CSV_INCOMPLETE)[0]);
        assert_eq!(fields[3], xml);
    }

    #[test]
    fn a_json_payload_is_recorded_when_no_xml_is_present() {
        let (mut logger, _) = started();
        let out = logger.evse(
            TS,
            true,
            "msg",
            Payload {
                json: r#"{"a":1}"#,
                ..Payload::default()
            },
        );
        let fields = parse_csv_record(appends_to(&out, CSV_INCOMPLETE)[0]);
        assert_eq!(fields[3], r#"{"a":1}"#);
    }

    /// A deployment with `session_logging` off has no logger.
    ///
    /// This replaces `logging_is_inert_until_it_is_enabled`, which built a
    /// logger over a root, left `enable()` uncalled and asserted that three
    /// methods returned nothing. That shape is what the whole of this file
    /// spent its first version in: constructed, inert, and reachable only from
    /// its own tests. There is no inert logger to build now.
    #[test]
    fn a_deployment_with_logging_off_has_no_logger_at_all() {
        let off = LoggingSettings {
            session_logging: false,
            ..logging(ROOT, true)
        };
        assert!(SessionLogger::for_settings(&off).is_none());
    }

    #[test]
    fn a_record_before_a_session_starts_produces_nothing() {
        let mut logger = logger();
        assert_eq!(
            logger.evse(TS, false, "msg", Payload::default()),
            LogOutput::default()
        );
    }

    #[test]
    fn starting_a_second_session_stops_the_first() {
        let (mut logger, _) = started();
        let out = logger.start_session("2026-08-31T11:00:00.000Z", "next");
        assert!(out.actions.iter().any(|action| matches!(action, Action::Rename { from, .. } if from.contains(&format!("{TS}-sessionid")))));
        assert_eq!(
            logger.session_dir(),
            Some(format!("{ROOT}/2026-08-31T11:00:00.000Z-next").as_str())
        );
    }

    #[test]
    fn starting_a_session_records_that_logging_started() {
        let (_, out) = started();
        assert!(out.lines.iter().any(
            |line| line.origin == Origin::Sys && line.text.contains("Session logging started")
        ));
    }

    #[test]
    fn stopping_a_session_records_that_logging_stopped() {
        let (mut logger, _) = started();
        let out = logger.stop_session(TS);
        assert!(out.lines.iter().any(
            |line| line.origin == Origin::Sys && line.text.contains("Session logging stopped")
        ));
    }

    #[test]
    fn a_failure_to_create_the_session_directory_stops_further_logging_without_an_error() {
        let (mut logger, out) = started();
        let create = out
            .actions
            .iter()
            .find(|action| matches!(action, Action::CreateDir { path } if path.contains(TS)))
            .cloned()
            .expect("session directory created");
        logger.note_failure(&create, "permission denied");
        assert!(!logger.is_session_active());
        assert_eq!(
            logger.evse(TS, false, "msg", Payload::default()),
            LogOutput::default()
        );
    }

    #[test]
    fn a_failure_to_open_the_transcript_stops_further_logging() {
        let (mut logger, out) = started();
        let truncate = out
            .actions
            .iter()
            .find(|action| matches!(action, Action::Truncate { path } if path.ends_with(CSV_INCOMPLETE)))
            .cloned()
            .expect("transcript truncated");
        logger.note_failure(&truncate, "read only filesystem");
        assert!(!logger.is_session_active());
    }

    #[test]
    fn a_failure_to_rename_the_finished_files_leaves_the_session_stopped() {
        let (mut logger, _) = started();
        let stop = logger.stop_session(TS);
        let rename = stop
            .actions
            .iter()
            .find(|action| matches!(action, Action::Rename { .. }))
            .cloned()
            .expect("files renamed");
        logger.note_failure(&rename, "cross device link");
        assert!(!logger.is_session_active());
    }

    #[test]
    fn a_failed_append_leaves_the_session_running() {
        let (mut logger, _) = started();
        let record = logger.evse(TS, false, "msg", Payload::default());
        let append = record
            .actions
            .iter()
            .find(|action| matches!(action, Action::Append { .. }))
            .cloned()
            .expect("record appended");
        logger.note_failure(&append, "disk full");
        assert!(logger.is_session_active());
    }

    #[test]
    fn the_publication_carries_the_origin_target_protocol_and_message() {
        let (mut logger, _) = started();
        let out = logger.car(TS, true, "SessionSetupReq", Payload::default());
        assert_eq!(out.publications.len(), 1);
        let publication = &out.publications[0];
        assert_eq!(publication.origin, "CAR");
        assert_eq!(publication.target, "EVSE");
        assert!(publication.iso15118);
        assert_eq!(publication.msg, "SessionSetupReq");
        let json: serde_json::Value =
            serde_json::from_str(&publication.payload()).expect("valid json");
        assert_eq!(json["origin"], "CAR");
        assert_eq!(json["target"], "EVSE");
        assert_eq!(json["iso15118"], true);
        assert_eq!(json["msg"], "SessionSetupReq");
    }

    #[test]
    fn the_structured_log_line_carries_the_payload_only_when_payload_output_is_enabled() {
        let (mut logger, _) = started();
        let with = logger.evse(
            TS,
            true,
            "msg",
            Payload {
                xml: "<A/>",
                ..Payload::default()
            },
        );
        assert!(with.lines[0].text.contains("<A/>"));

        let mut logger = logger_at(ROOT, false);
        logger.start_session(TS, "sessionid");
        let without = logger.evse(
            TS,
            true,
            "msg",
            Payload {
                xml: "<A/>",
                ..Payload::default()
            },
        );
        assert!(!without.lines[0].text.contains("<A/>"));
        assert!(without.lines[0].text.contains("msg"));
    }

    #[test]
    fn a_car_message_and_an_evse_message_land_in_different_columns() {
        let (mut logger, _) = started();
        let from_evse = logger.evse(TS, false, "EvseSaid", Payload::default());
        let from_car = logger.car(TS, false, "CarSaid", Payload::default());
        let evse_row = appends_to(&from_evse, HTML_INCOMPLETE)[0].to_string();
        let car_row = appends_to(&from_car, HTML_INCOMPLETE)[0].to_string();
        assert!(
            evse_row.contains("<td><b>EvseSaid</b></td><td><b></b></td>"),
            "{evse_row}"
        );
        assert!(
            car_row.contains("<td><b></b></td><td><b>CarSaid</b></td>"),
            "{car_row}"
        );
        assert!(evse_row.contains(r#"class="EVSE""#), "{evse_row}");
        assert!(car_row.contains(r#"class="CAR""#), "{car_row}");
    }

    #[test]
    fn the_special_suffix_value_means_the_session_identity() {
        assert_eq!(suffix_for(SUFFIX_FROM_SESSION, "abc-123"), "abc-123");
    }

    #[test]
    fn a_configured_suffix_is_used_verbatim() {
        assert_eq!(suffix_for("bench-rig", "abc-123"), "bench-rig");
        // Which is why the containment guard exists: this value is operator
        // supplied and reaches the path.
        assert_eq!(suffix_for("../../etc", "abc-123"), "../../etc");
    }

    #[test]
    fn an_empty_session_identity_still_derives_a_suffix() {
        assert_eq!(suffix_for(SUFFIX_FROM_SESSION, ""), "");
        let (_, out) = {
            let mut logger = logger();
            let out = logger.start_session(TS, &suffix_for(SUFFIX_FROM_SESSION, ""));
            (logger, out)
        };
        assert!(
            out.actions.iter().any(|action| matches!(
                action,
                Action::CreateDir { path } if path == &format!("{ROOT}/{TS}-")
            )),
            "a session with no identity still opens a directory, got {:?}",
            out.actions
        );
    }

    #[test]
    fn a_system_line_carries_no_protocol_token() {
        // `SessionLog.cpp:228` prints `"SYS  " << msg`, where the other two
        // origins get `ISO` or `IEC`. The asymmetry is the C++'s, not a slip.
        let line = LogLine {
            origin: Origin::Sys,
            iso15118: false,
            text: "Session logging started.".to_string(),
        };
        assert_eq!(line.render(), "SYS  Session logging started.");
    }

    #[test]
    fn an_evse_line_and_a_car_line_name_the_protocol_and_the_car_line_is_indented() {
        let evse = LogLine {
            origin: Origin::Evse,
            iso15118: false,
            text: "Charger state: Idle->Wait for Auth".to_string(),
        };
        assert_eq!(evse.render(), "EVSE IEC Charger state: Idle->Wait for Auth");

        let car = LogLine {
            origin: Origin::Car,
            iso15118: true,
            text: "V2G SessionSetupReq".to_string(),
        };
        assert_eq!(
            car.render(),
            format!("{CAR_LINE_INDENT}CAR ISO V2G SessionSetupReq")
        );
        // `SessionLog.cpp:223` indents the car side by exactly this much, which
        // is what puts the two directions in separate terminal columns.
        assert_eq!(CAR_LINE_INDENT.len(), 36);
    }

    #[test]
    fn the_charger_states_are_spelled_as_the_cpp_spells_them() {
        // `Charger::evse_state_to_string` (`Charger.cpp:1871-1910`). Three of
        // these disagree with the variant name, and a transcript a human diffs
        // against a C++ one has to carry the C++ spelling.
        assert_eq!(
            charger_state_name(AcState::WaitingForAuthentication),
            "Wait for Auth"
        );
        assert_eq!(charger_state_name(AcState::ChargingPausedEv), "Car Paused");
        assert_eq!(
            charger_state_name(AcState::ChargingPausedEvse),
            "EVSE Paused"
        );
        assert_eq!(charger_state_name(AcState::Idle), "Idle");
        assert_eq!(charger_state_name(AcState::Disabled), "Disabled");
        assert_eq!(charger_state_name(AcState::Charging), "Charging");
        assert_eq!(charger_state_name(AcState::Finished), "Finished");
        assert_eq!(
            charger_state_name(AcState::PrepareCharging),
            "PrepareCharging"
        );
        assert_eq!(
            charger_state_name(AcState::StoppingCharging),
            "StoppingCharging"
        );
        // Its own name, not the name of the state it interrupts: a transcript
        // that read `Charging->Charging` would hide the break entirely.
        assert_eq!(charger_state_name(AcState::SwitchPhases), "SwitchPhases");
    }

    #[test]
    fn a_transition_line_names_both_ends_of_the_edge() {
        assert_eq!(
            state_transition_message(AcState::Charging, AcState::ChargingPausedEv),
            "Charger state: Charging->Car Paused"
        );
    }

    #[test]
    fn a_start_reason_is_spelled_as_the_wire_spells_it() {
        assert_eq!(
            start_reason_name(StartSessionReason::EvConnected),
            "EVConnected"
        );
        assert_eq!(
            start_reason_name(StartSessionReason::Authorized),
            "Authorized"
        );
    }

    #[test]
    fn an_enabled_logger_says_where_the_transcripts_go_and_that_none_exists_yet() {
        // The C++ says nothing about a subsystem nobody asked for, and here
        // there is nothing to say it with: a deployment with the feature off
        // holds no logger, so `main` announces only what it has.
        let announcement = logger().announcement();
        assert!(
            announcement.contains(ROOT),
            "an operator has to be told the root, got {announcement}"
        );
        assert!(
            announcement.contains("until a session starts"),
            "the silence before the first session is the defect this replaces, \
             so it has to be named, got {announcement}"
        );
        assert!(
            announcement.contains("ISO 15118"),
            "the same key drives a different subsystem, which is what confuses \
             an operator reading this, got {announcement}"
        );
    }

    #[test]
    fn an_owned_payload_borrows_the_four_representations_in_place() {
        // Filled by name. All four are strings and two of them are encodings of
        // the same bytes, so a positional literal would compile with the hex
        // and base64 forms swapped.
        let owned = OwnedPayload {
            xml: "<x/>".to_string(),
            xml_hex: "deadbeef".to_string(),
            xml_base64: "3q2+7w==".to_string(),
            json: "{}".to_string(),
        };
        let borrowed = owned.borrow();
        assert_eq!(borrowed.xml, "<x/>");
        assert_eq!(borrowed.xml_hex, "deadbeef");
        assert_eq!(borrowed.xml_base64, "3q2+7w==");
        assert_eq!(borrowed.json, "{}");
        assert!(!owned.is_empty());
        assert!(OwnedPayload::default().is_empty());
    }

    #[test]
    fn the_hex_and_base64_payloads_are_recorded_in_the_html_transcript() {
        let (mut logger, _) = started();
        let out = logger.evse(
            TS,
            true,
            "msg",
            Payload {
                xml: "<A/>",
                xml_hex: "809a",
                xml_base64: "gJo=",
                json: "",
            },
        );
        let row = appends_to(&out, HTML_INCOMPLETE)[0];
        assert!(row.contains("809a"), "{row}");
        assert!(row.contains("gJo="), "{row}");
        assert_well_formed_html(row);
    }

    #[test]
    fn a_quote_and_an_apostrophe_are_escaped_in_the_html_transcript() {
        // Divergence 5 claims five characters, and `assert_well_formed_html`
        // only ever proves three of them: it looks for bare ampersands and for
        // unbalanced tags, and a bare `"` or `'` in element content is well
        // formed, so dropping either arm of `html_escape` left every other
        // test in this file green.
        //
        // They are escaped anyway, and pinned here, because the C++ encoder
        // (`SessionLog.cpp:261-266`) escapes neither and this is one of the two
        // places the port deliberately does more. Whether a `"` is load bearing
        // depends on where the value lands: today every escaped value lands in
        // element content, but the one attribute in a row (`<tr class="...">`)
        // is filled from `Origin::as_str` and a later row that interpolated an
        // operator supplied value into an attribute would need this.
        assert_eq!(html_escape(r#"a"b"#), "a&quot;b");
        assert_eq!(html_escape("a'b"), "a&apos;b");

        let (mut logger, _) = started();
        let out = logger.evse(TS, false, r#"say "it's" so"#, Payload::default());
        let row = appends_to(&out, HTML_INCOMPLETE)[0];
        assert!(row.contains("&quot;it&apos;s&quot;"), "{row}");
        assert!(
            !row.contains(r#""it"#),
            "no bare quote may reach the row: {row}"
        );
    }

    /// The containment predicate itself, at the three boundaries a derived path
    /// can sit on.
    ///
    /// Driven directly rather than only through `start_session`, because two of
    /// the three are decided by a single clause each and the end to end tests
    /// above all fail for a different reason first: they escape the root so far
    /// that `normalize` collapses them well clear of it, which the length
    /// clause alone already rejects.
    #[test]
    fn containment_requires_a_strict_descendant_and_a_component_boundary() {
        let root = "/var/log/everest";
        assert!(is_contained(root, "/var/log/everest/2026-a"));
        assert!(is_contained(root, "/var/log/everest/2026-a/deeper"));

        // The root is not below itself. Nothing may be written into the shared
        // root directly: a transcript belongs to one session and the names
        // inside it are fixed, so two sessions writing there would overwrite
        // each other's `eventlog.csv`.
        assert!(!is_contained(root, root));
        assert!(!is_contained(root, "/var/log/everest/"));

        // A sibling that merely shares the textual prefix. `starts_with` alone
        // admits this, which is what the byte at `root.len()` is for.
        assert!(!is_contained(root, "/var/log/everest-evil/2026-a"));
        assert!(!is_contained(root, "/var/log/everestevil"));

        // And a path that shares no prefix at all but does carry a separator at
        // the same offset, which is what defeats the length and boundary
        // clauses on their own.
        assert!(!is_contained(root, "/etc/cron.d/xxxxxx/2026-a"));
        assert!(!is_contained(root, "/var/log/systemd/2026-a"));

        // A trailing separator on the configured root is not a component.
        assert!(is_contained("/var/log/everest/", "/var/log/everest/2026-a"));
    }

    #[test]
    fn a_suffix_that_lands_on_a_sibling_of_the_root_starts_no_session() {
        // Reachable end to end, and the case the byte boundary check exists
        // for. Note the escape depth: the derived name is
        // `{root}/{timestamp}-{suffix}`, so the FIRST `..` of a suffix is glued
        // to the timestamp into the single component `{timestamp}-..` and is
        // inert. The second pops that component and the third pops the root's
        // own last one, leaving a SIBLING of the root that still starts with
        // the root's full text. A guard that tested only `starts_with` would
        // create `/tmp/everest-session-logs-evil/x` and write a transcript
        // outside the configured tree.
        let mut logger = logger();
        let out = logger.start_session(TS, "../../../everest-session-logs-evil/x");

        assert!(
            out.actions.is_empty(),
            "a sibling of the root is not inside it, got {:?}",
            out.actions
        );
        assert!(!logger.is_session_active());
    }

    #[test]
    fn a_suffix_that_resolves_to_the_root_itself_starts_no_session() {
        // The other reachable boundary. Two `..` are needed, not one, for the
        // reason the sibling test above spells out: the first is absorbed into
        // `{timestamp}-..` and the second pops that whole component, so the
        // derived directory IS the root. Admitting it would put `eventlog.csv`
        // in the shared root, where the next session would truncate it.
        let mut logger = logger();
        let out = logger.start_session(TS, "../..");

        assert!(
            out.actions.is_empty(),
            "the root is not a session directory, got {:?}",
            out.actions
        );
        assert!(!logger.is_session_active());
    }

    #[test]
    fn the_start_actions_arrive_in_the_order_the_files_need() {
        // Asserted as a sequence, not as a set. Every other test here asks
        // whether an action is present, and a transcript depends on their order:
        // the header has to be appended AFTER the truncate that opens the file,
        // and each file's directory has to exist before it is created. Swapping
        // the truncate past the header silently produced an empty transcript
        // with every core test still green, because the only assertion that
        // read the file back lives in the boundary suite, which `test-core.sh`
        // strips.
        let (_, out) = started();
        let dir = format!("{ROOT}/{TS}-sessionid");

        let shapes: Vec<String> = out
            .actions
            .iter()
            .map(|action| match action {
                Action::CreateDir { path } => format!("mkdir {path}"),
                Action::Truncate { path } => format!("truncate {path}"),
                Action::Append { path, .. } => format!("append {path}"),
                Action::Rename { from, to } => format!("rename {from} -> {to}"),
            })
            .collect();

        assert_eq!(
            shapes,
            vec![
                format!("mkdir {ROOT}"),
                format!("mkdir {dir}"),
                format!("truncate {dir}/{CSV_INCOMPLETE}"),
                format!("truncate {dir}/{HTML_INCOMPLETE}"),
                format!("append {dir}/{HTML_INCOMPLETE}"),
                // The `Session logging started.` record, which lands after the
                // header for the same reason.
                format!("append {dir}/{CSV_INCOMPLETE}"),
                format!("append {dir}/{HTML_INCOMPLETE}"),
            ]
        );
    }

    #[test]
    fn stopping_a_session_that_never_started_produces_nothing() {
        // The `session.is_none()` half of the stop guard. Without it the
        // `expect("session present")` below it panics, and no test reached that
        // shape: `starting_a_second_session_stops_the_first` gets there through
        // `start_session`, which only calls `stop_session` when a session is
        // open, and every other stop follows a start.
        let mut logger = logger();
        let out = logger.stop_session(TS);
        assert_eq!(out, LogOutput::default());

        // And a second stop after a real one, which is the reachable shape: a
        // `SessionFinished` announced twice must not panic the logger.
        let (mut logger, _) = started();
        assert!(!logger.stop_session(TS).actions.is_empty());
        assert_eq!(logger.stop_session(TS), LogOutput::default());
    }

    #[test]
    fn a_failed_open_abandons_the_session_but_leaves_logging_on_for_the_next_one() {
        // The retry invariant, which is the difference between a transient
        // fault and a permanent loss of logging. The boundary suite asserts it
        // once, and `test-core.sh` strips that file, so on the core gate alone
        // a `note_failure` that also cleared `enabled` was indistinguishable.
        let (mut logger, start) = started();
        let create = start
            .actions
            .iter()
            .find(|action| matches!(action, Action::CreateDir { .. }))
            .expect("a start creates directories");

        logger.note_failure(create, "Read-only file system (os error 30)");

        assert!(!logger.is_session_active(), "this transcript is abandoned");
        // The next session tries again, which is what a logger that cannot be
        // disabled means: there is no `enabled` field left for `note_failure`
        // to clear.
        let again = logger.start_session(TS, "sessionid");
        assert!(!again.actions.is_empty());
        assert!(logger.is_session_active());
    }

    #[test]
    fn a_partly_filled_payload_is_not_empty() {
        // `&&` versus `||`, which neither existing assertion can see: they use
        // an all-filled payload and an all-empty one, and those two agree under
        // either operator. Only a mixed payload separates them, and mixed is
        // the normal case -- the ISO feed fills `exi`/`exi_base64` on every
        // message and `xml`/`v2g_json` only when the stack decoded one.
        for payload in [
            OwnedPayload {
                xml: "<A/>".to_string(),
                ..Default::default()
            },
            OwnedPayload {
                xml_hex: "809a".to_string(),
                ..Default::default()
            },
            OwnedPayload {
                xml_base64: "gJo=".to_string(),
                ..Default::default()
            },
            OwnedPayload {
                json: "{}".to_string(),
                ..Default::default()
            },
        ] {
            assert!(!payload.is_empty(), "{payload:?} carries something");
        }
    }

    #[test]
    fn a_system_record_names_no_peer() {
        // `Origin::Sys::target()` is the empty string, which is the one place
        // the three origins are not symmetric (`SessionLog.cpp:226-227` sets
        // `target` to `""` for `typ == 2`). It reaches the reader twice, in the
        // HTML direction cell and in the publication, and no test read either
        // for a system line -- so a `Sys` that named `CAR` as its peer would
        // have announced a message from the module to the vehicle that never
        // happened.
        let (mut logger, _) = started();
        let out = logger.sys(TS, "Session logging stopped.");

        let row = appends_to(&out, HTML_INCOMPLETE)[0];
        assert!(row.contains("<td>SYS&gt;</td>"), "{row}");
        assert_eq!(out.publications[0].origin, "SYS");
        assert_eq!(out.publications[0].target, "");
        // The message is in the EVSE column, as `typ == 2` puts it at
        // `SessionLog.cpp:239`.
        assert!(
            row.contains("<td><b>Session logging stopped.</b></td><td><b></b></td>"),
            "{row}"
        );
    }

    #[test]
    fn the_session_bracket_carries_the_publications_of_the_lines_it_writes() {
        // `LogOutput::absorb` merges three vectors and the start and stop
        // brackets are the only callers. Dropping the publications there left
        // every test green, because the only assertion on a publication reads
        // one straight out of `record` and never one that travelled through
        // `absorb`. The bodies are produced for an MQTT publish that has no
        // binding yet (see `docs/architecture.md`), so nothing in production
        // notices either; that is exactly why it needs pinning here.
        let mut logger = logger();

        let start = logger.start_session(TS, "sessionid");
        assert_eq!(
            start
                .publications
                .iter()
                .map(|publication| publication.msg.as_str())
                .collect::<Vec<_>>(),
            vec!["Session logging started."]
        );
        assert_eq!(
            start.lines.len(),
            1,
            "and the EVerest log line travels with it"
        );

        let stop = logger.stop_session(TS);
        assert_eq!(
            stop.publications
                .iter()
                .map(|publication| publication.msg.as_str())
                .collect::<Vec<_>>(),
            vec!["Session logging stopped."]
        );
    }

    /// The mutants in this file that no test can distinguish, and why.
    ///
    /// Recorded rather than asserted, because writing a test for any of them
    /// would mean asserting something the code does not promise. Each one was
    /// confirmed to survive the whole suite; none is a gap.
    ///
    /// 1 and 2 were `record`'s and `stop_session`'s `!self.enabled` early
    ///    returns, both recorded here as dead: `enabled` was set by `enable()`
    ///    and cleared by nothing, so `session.is_some()` implied it. They are
    ///    not mutants any more, because the field is gone:
    ///    `SessionLogger::for_settings` answers `None` for a deployment with
    ///    the feature off, so a logger that exists is enabled and there is
    ///    nothing left to test for. `stop_session`'s OTHER disjunct,
    ///    `self.session.is_none()`, is load bearing -- without it the `expect`
    ///    below it panics -- and is driven by
    ///    `stopping_a_session_that_never_started_produces_nothing`.
    ///
    /// 3. `record`'s early return for a closed session yields `out`, and
    ///    replacing that with `LogOutput::default()` is not a behaviour change
    ///    at all: `out` was constructed by `LogOutput::default()` two lines
    ///    above and nothing has been pushed into it. The two expressions are
    ///    the same value, so this mutant is degenerate rather than unmeasured.
    ///
    /// 4. `record`'s `origin != Origin::Sys` in the payload gate cannot fire on
    ///    a payload. `record` is private and its only `Sys` caller is `sys()`,
    ///    which hard codes `Payload::default()`, so `body` is empty on every
    ///    `Sys` record and `text.push_str(body)` is a no-op. The guard states
    ///    the C++'s asymmetry at `SessionLog.cpp:228` rather than causing it.
    ///
    /// 5. `note_failure`'s `Rename` arm cannot usefully clear `self.session`,
    ///    because a `Rename` is only ever produced by `stop_session`, which has
    ///    already `take()`n it. The session is `None` before the boundary can
    ///    report the failure.
    ///
    /// One near miss, recorded because it reads like a sixth and is not:
    /// `html_escape(now)` in the HTML row is unobservable for any timestamp the
    /// boundary can produce -- RFC 3339 carries no character this function
    /// rewrites -- but dropping it does not survive, because clippy rejects the
    /// `now.to_string()` it degrades to. It is killed by a lint and not by a
    /// test, which is the weaker result; the property that makes it harmless is
    /// pinned in `main.rs` by `a_short_write_is_not_reachable_from_a_test`.
    #[test]
    fn the_redundant_guards_are_redundant_and_not_merely_untested() {
        // 4: no `Sys` record can carry a payload, whatever the payload switch
        // says, because `sys()` is the only way to make one.
        let (mut logger, _) = started();
        let out = logger.sys(TS, "Session logging started.");
        assert_eq!(out.lines[0].text, "Session logging started.");
    }
}
