#!/usr/bin/env python3
"""Proves that the unreachable constructions this module used to accept no
longer compile.

Four pieces of work on this port were finished, correct and unreachable, and
nothing failed: no test, no gate, no log line. `scripts/reachability.py` is the
census that notices some of them after the fact. This script is the other half
of the answer, for the classes where absence became a type: it writes each
mistake back into a scratch copy of `src/` and requires the compiler to refuse
it.

A refactor that merely could catch a mistake is not evidence. Each case below
names the construction, the class it comes from, and the diagnostic that must
appear. The scratch copy is `core` and `boundary` only, the same `[lib]` shape
`test-core.sh` builds, so this needs no framework and no CMake tree.

Usage:
    scripts/unconstructable.py           run every case, exit 1 if any compiles
    scripts/unconstructable.py --list    name the cases and exit
    scripts/unconstructable.py -v        also print each diagnostic
"""

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

MODULE = pathlib.Path(__file__).resolve().parent.parent

# Versions match modules/EVSE/RsEvseManager/Cargo.toml, as test-core.sh's do.
MANIFEST = """\
[package]
name = "rs_evse_manager_core"
version = "0.1.0"
edition = "2021"

[lib]
path = "src/lib.rs"

[dependencies]
anyhow = "1.0.82"
log = "0.4.20"
serde = { version = "1.0.200", features = ["derive"] }
serde_json = "1"

[workspace]
"""


class Case:
    """One unreachable construction, and the diagnostic that must refuse it.

    `edits` is a list of `(path, before, after)`; `before` must appear exactly
    once in the file, which is what stops a case from silently going stale when
    the code it patches moves. `after` may be empty, for a case that deletes.
    """

    def __init__(self, name, klass, why, expect, edits, appends=()):
        self.name = name
        self.klass = klass
        self.why = why
        self.expect = expect
        self.edits = edits
        self.appends = appends

    def apply(self, root):
        for rel, before, after in self.edits:
            path = root / rel
            text = path.read_text()
            if text.count(before) != 1:
                raise SystemExit(
                    f"{self.name}: the anchor below appears "
                    f"{text.count(before)} times in {rel}, not once. The code it "
                    f"patches moved; update the case.\n---\n{before}\n---"
                )
            path.write_text(text.replace(before, after, 1))
        for rel, tail in self.appends:
            path = root / rel
            path.write_text(path.read_text() + tail)


CASES = [
    Case(
        "a_disabled_high_level_communication_configuration",
        "T30-shaped: a collaborator that exists and is switched off",
        "`HlcConfig` carried an `enabled: bool` and thirty guards read it. A "
        "configuration that says it is off is what made every emission in that "
        "file reachable-or-not by a flag.",
        "no associated function or constant named `from_settings`",
        [
            (
                "src/core/config.rs",
                "    let hlc =\n"
                "        HlcConfig::for_deployment(settings, wiring)"
                ".map(|c| HlcPort::new(Arc::new(c), settings.charge_mode));",
                "    let hlc = Some(HlcPort::new(\n"
                "        Arc::new(HlcConfig::from_settings(settings, false)),\n"
                "        settings.charge_mode,\n"
                "    ));",
            )
        ],
    ),
    Case(
        "a_runtime_guard_inside_the_high_level_communication_port",
        "T30-shaped: a collaborator that exists and is switched off",
        "One of the thirty `if !self.config.enabled { return Vec::new(); }` "
        "guards, put back. Each one hid code that only a person reading it "
        "could tell was reachable.",
        "no field `enabled`",
        [
            (
                "src/core/hlc/mod.rs",
                "    pub fn on_plug_in(&mut self) -> Vec<Effect> {",
                "    pub fn on_plug_in(&mut self) -> Vec<Effect> {\n"
                "        if !self.config.enabled {\n"
                "            return Vec::new();\n"
                "        }",
            )
        ],
    ),
    Case(
        "a_use_site_that_assumes_every_deployment_has_a_stack",
        "T30-shaped: a collaborator that exists and is switched off",
        "The reachability mistake itself, at a call site: work written against "
        "a collaborator not every deployment has. It compiled, ran on every "
        "port, and did nothing on the ones with the flag off.",
        "no method named `on_data_link` found for enum `std::option::Option<T>`",
        [
            (
                "src/core/mod.rs",
                "        if let Some(hlc) = self.hlc.as_mut() {\n"
                "            effects.extend(hlc.on_data_link(request));\n"
                "        }",
                "        effects.extend(self.hlc.on_data_link(request));",
            )
        ],
    ),
    Case(
        "two_adjacent_monitor_flags_on_the_dc_path",
        "T17: a monitor claimed by a flag, with nothing to talk to",
        "`Dc::new(config, imd_wired, over_voltage_monitor_wired)` accepted any "
        "pair of bools, including a port that claimed a monitor it held "
        "nothing to talk to, and including the two transposed.",
        "expected `Option<IsolationMonitor>`, found `bool`",
        [
            (
                "src/core/config.rs",
                "                IsolationMonitor::for_wiring(wiring, "
                "settings.cable_check_options()),\n"
                "                OverVoltageMonitor::for_wiring(wiring),",
                "                wiring.over_voltage_monitor,\n"
                "                wiring.imd,",
            )
        ],
    ),
    Case(
        "cable_check_options_on_a_port_with_no_isolation_monitor",
        "T17-adjacent: three manifest keys with no reader",
        "The census reported `set_cable_check_options` as `TEST-ONLY-FN` for "
        "the whole life of the setter, and all three manifest keys behind it "
        "parsed into `DcSettings` and reached nothing. Options belong to the "
        "monitor that runs the steps they name.",
        "no method named `set_cable_check_options`",
        [
            (
                "src/core/config.rs",
                "            path: Box::new(Dc::new(\n"
                "                settings.dc_config(),\n"
                "                IsolationMonitor::for_wiring(wiring, "
                "settings.cable_check_options()),\n"
                "                OverVoltageMonitor::for_wiring(wiring),\n"
                "            )),",
                "            path: Box::new({\n"
                "                let mut dc = Dc::new(settings.dc_config(), None, None);\n"
                "                dc.set_cable_check_options(settings.cable_check_options());\n"
                "                dc\n"
                "            }),",
            )
        ],
    ),
    Case(
        "a_fifth_reader_of_the_cable_check_options",
        "T17-adjacent: three manifest keys with no reader",
        "`imd_asks` is spelled as a conjunction with the monitor's presence, "
        "so a reader added outside the cable check sequence gets the honest "
        "answer instead of `CableCheckOptions::default()`.",
        "no field `options` on type `&mut Dc`",
        [
            (
                "src/core/path/dc.rs",
                "        if self.imd_asks(|options| options.wait_below_60v_before_finish) {",
                "        if self.options.wait_below_60v_before_finish {",
            )
        ],
    ),
    Case(
        "over_voltage_limits_written_outside_the_module_that_owns_the_monitor",
        "T17: an effect variant with no production producer",
        "`Effect::OverVoltageLimits` spent six days as a variant with a "
        "written derivation and no producer at all. Its payload's fields are "
        "private to `core::path::dc` and `OverVoltageMonitor::thresholds` is "
        "the one constructor, so no other module can write the effect.",
        "fields `emergency_v` and `error_v` of struct `OverVoltageThresholds` are private",
        [],
        appends=[
            (
                "src/core/effect.rs",
                "\n/// A producer outside the module that owns the monitor.\n"
                "pub fn unreachable_over_voltage_limits() -> Effect {\n"
                "    Effect::OverVoltageLimits(OverVoltageThresholds {\n"
                "        emergency_v: 1100.0,\n"
                "        error_v: 920.0,\n"
                "    })\n"
                "}\n",
            )
        ],
    ),
    Case(
        "a_threshold_derived_on_a_port_with_no_over_voltage_monitor",
        "T17: an effect variant with no production producer",
        "Inside `core::path::dc` the derivation takes the monitor as an "
        "argument, so it cannot be called on a port that has none: there would "
        "be nothing to hand the answer to.",
        "argument #1 of type `&OverVoltageMonitor` is missing",
        [
            (
                "src/core/path/dc.rs",
                "                match &self.over_voltage {\n"
                "                    Some(monitor) => vec![Effect::OverVoltageLimits(\n"
                "                        self.over_voltage_thresholds(monitor),\n"
                "                    )],\n"
                "                    None => Vec::new(),\n"
                "                }",
                "                vec![Effect::OverVoltageLimits("
                "self.over_voltage_thresholds())]",
            )
        ],
    ),
    Case(
        "a_dc_path_that_does_not_say_whether_it_is_charging_over_iso_15118",
        "T21: a mode fact inherited from a default nobody read",
        "The T21 shape exactly. `hlc_charging_active` lived on `AcHlc`, the "
        "mode independent readers above the trait asked every path for it, and "
        "`Dc` inherited `false` from the trait's default body. The stop "
        "signalling was written, correct and inert on DC.",
        "not all trait items implemented, missing: `hlc_charging_active`",
        [],
    ),
    Case(
        "a_fifth_power_path_that_answers_none_of_the_mode_facts",
        "T21: a mode fact inherited from a default nobody read",
        "A path written the way the first four were: everything the reducer "
        "needs, nothing about the mode. It compiled, and silently gave the AC "
        "basic answer to three questions the readers above the trait ask every "
        "path.",
        "not all trait items implemented, missing: `target_voltage_v`, "
        "`hlc_charging_active`, `presents_fake_dc`",
        [],
    ),
    Case(
        "a_fixture_that_mints_an_identity_the_core_never_issued",
        "T-effect-id: an identity the core never issued",
        "`EffectId(pub u64)` let any file write an identifier. Test fixtures "
        "wrote `EffectId(0)`, `EffectId(7)` and `EffectId(9999)`, and a written "
        "identity is exactly how two allocators could both name zero.",
        "cannot initialize a tuple struct which contains private fields",
        [],
        appends=[
            (
                "src/core/path/dc.rs",
                "\n/// A construction site naming an identity nothing allocated.\n"
                "pub fn unissued_identity() -> EffectId {\n"
                "    EffectId(7)\n"
                "}\n",
            )
        ],
    ),
    Case(
        "the_core_awaiting_an_identity_a_power_path_issued",
        "T-effect-id: an identity the core never issued",
        "The defect a5c83b520 removed at run time, offered to the compiler: the "
        "core putting a path's identity into its own await slot. One space "
        "makes the numbers distinct; the owner tag is what makes the slot "
        "refuse the wrong one.",
        "expected `Issued<ByCore>`, found `Issued<ByPath>`",
        [
            (
                "src/core/mod.rs",
                "                let id = self.effect_ids.allocate();",
                "                let id = self.effect_ids.delegate().allocate();",
            )
        ],
    ),
    Case(
        "the_core_awaiting_the_identity_a_completion_carried_back",
        "T-effect-id: an identity the core never issued",
        "The other way in: rather than allocating, take the identity the loop "
        "delivered and call it the awaited one. `answer_transaction_start` "
        "would then answer every completion, which is what its runtime guard "
        "against `awaiting_transaction_start` exists to stop.",
        "expected `Option<Issued<ByCore>>`, found `Option<EffectId>`",
        [
            (
                "src/core/mod.rs",
                "        self.awaiting_transaction_start = None;\n\n"
                "        let EffectOutcome::Failed(reason) = outcome else {",
                "        self.awaiting_transaction_start = id;\n\n"
                "        let EffectOutcome::Failed(reason) = outcome else {",
            )
        ],
    ),
    Case(
        "a_second_effect_identity_space_on_the_dc_path",
        "T-effect-id: an identity the core never issued",
        "`Dc::new` built a private `EffectIds::default()` and `adopt_effect_ids` "
        "overwrote it. A `Dc` driven without a core therefore had a counter of "
        "its own that began at zero, which is the second space itself.",
        "no associated function or constant named `default` found for struct "
        "`EffectIds<O>`",
        [
            (
                "src/core/path/dc.rs",
                "            effect_ids: None,",
                "            effect_ids: Some(EffectIds::default()),",
            )
        ],
    ),
    Case(
        "the_boundary_starting_a_space_of_its_own",
        "T-effect-id: an identity the core never issued",
        "The loop carries identities and chooses none. `one_space` is "
        "`pub(super)`, so `boundary` and `main.rs` cannot start a space at all: "
        "there is no counter outside the one `Core::new` roots.",
        "associated function `one_space` is private",
        [],
        appends=[
            (
                "src/boundary/event_loop.rs",
                "\n/// The loop starting an identity space of its own.\n"
                "pub fn loop_effect_ids() -> crate::core::effect::EffectIds<\n"
                "    crate::core::effect::ByCore,\n"
                "> {\n"
                "    crate::core::effect::EffectIds::one_space()\n"
                "}\n",
            )
        ],
    ),
    Case(
        "a_third_effect_identity_owner_declared_outside_the_roster",
        "T-effect-id: an identity the core never issued",
        "Ownership is a tag beside one counter, so the tags have to be a closed "
        "set: a third one declared wherever someone needed it is where a second "
        "space would arrive next. `EffectOwner` is sealed, so the roster is "
        "`ByCore` and `ByPath` or nothing.",
        "the trait bound `ByBoundary: Sealed` is not satisfied",
        [],
        appends=[
            (
                "src/core/path/dc.rs",
                "\n/// A third correlator, declared where it was needed.\n"
                "#[derive(Clone, Copy, Debug, Eq, PartialEq)]\n"
                "pub struct ByBoundary;\n"
                "\nimpl crate::core::effect::EffectOwner for ByBoundary {}\n",
            )
        ],
    ),
    Case(
        "a_session_logger_that_is_built_and_left_switched_off",
        "T30: 447 lines with a green suite and no constructor call",
        "`SessionLogger::new` built a logger with `enabled: false` and "
        "`enable()` turned it on. Leaving the second call out is what T30 was, "
        "and from the filesystem it is indistinguishable from a working "
        "feature.",
        "no associated function or constant named `new`",
        [],
        appends=[
            (
                "src/core/session_log.rs",
                "\n/// The boundary's old wiring: build one, then decide.\n"
                "pub fn boot_logger(settings: &LoggingSettings) -> SessionLogger {\n"
                "    let mut logger = SessionLogger::new(&settings.session_logging_path);\n"
                "    logger.set_xml_output(settings.session_logging_xml);\n"
                "    if settings.session_logging {\n"
                "        logger.enable();\n"
                "    }\n"
                "    logger\n"
                "}\n",
            )
        ],
    ),
    Case(
        "a_runtime_guard_inside_the_session_logger",
        "T30: 447 lines with a green suite and no constructor call",
        "One of the three `if !self.enabled` early returns, put back. Two of "
        "the three were already recorded in that file as dead code no test "
        "could reach.",
        "no field `enabled` on type `&mut SessionLogger`",
        [
            (
                "src/core/session_log.rs",
                "    pub fn start_session(&mut self, now: &str, suffix: &str) -> LogOutput {\n"
                "        let mut out = LogOutput::default();",
                "    pub fn start_session(&mut self, now: &str, suffix: &str) -> LogOutput {\n"
                "        let mut out = LogOutput::default();\n"
                "        if !self.enabled {\n"
                "            return out;\n"
                "        }",
            )
        ],
    ),
    Case(
        "a_second_reader_choosing_which_tariff_the_meter_signs",
        "absence and selection as a type",
        "`TariffMessages` holds the verdict's messages in wire order and "
        "`text()` is the one reader, because the interface says the first one "
        "is what opens the metering transaction. A second site picking its own "
        "message out of the list is how a signed metrology record and a "
        "receipt end up quoting different prices.",
        "field `contents` of struct `TariffMessages` is private",
        [
            (
                "src/core/mod.rs",
                "                    tariff_text: self.session.authorized_tariff.text()"
                ".map(str::to_owned),",
                "                    tariff_text: self.session.authorized_tariff.contents"
                ".last().cloned(),",
            )
        ],
    ),
]

# The two `PowerPath` cases delete rather than replace, so their edits are
# built here where the whole method body can be found by brace matching.
FIFTH_PATH = '''
/// A fifth power path, written the way the first four were before the trait
/// required its three mode facts.
pub struct AcMedium;

impl PowerPath for AcMedium {
    fn name(&self) -> &'static str {
        "AcMedium"
    }
    fn on_startup(&mut self) -> Vec<Effect> {
        Vec::new()
    }
    fn on_session_start(&mut self, _s: &Session, _n: Instant) -> Vec<Effect> {
        Vec::new()
    }
    fn on_authorized(&mut self, _s: &Session, _n: Instant) -> Vec<Effect> {
        Vec::new()
    }
    fn on_bsp(&mut self, _s: &Session, _e: &BspEvent, _n: Instant) -> Vec<Effect> {
        Vec::new()
    }
    fn on_limits_changed(&mut self, _s: &Session, _n: Instant) -> Vec<Effect> {
        Vec::new()
    }
    fn on_stop(&mut self, _s: &Session, _r: StopReason, _n: Instant) -> Vec<Effect> {
        Vec::new()
    }
    fn on_timer(&mut self, _s: &Session, _t: TimerId, _n: Instant) -> Vec<Effect> {
        Vec::new()
    }
    fn take_entered_states(&mut self) -> Vec<AcState> {
        Vec::new()
    }
    fn state(&self) -> AcState {
        AcState::Idle
    }
    fn signalled_current_a(&self) -> f64 {
        0.0
    }
    fn on_effect_done(
        &mut self,
        _s: &Session,
        _i: Option<EffectId>,
        _o: &EffectOutcome,
        _n: Instant,
    ) -> Vec<Effect> {
        Vec::new()
    }
    fn on_path_event(&mut self, _s: &Session, _e: PathEvent, _n: Instant) -> Vec<Effect> {
        Vec::new()
    }
    fn take_session_duties(&mut self) -> Vec<SessionDuty> {
        Vec::new()
    }
    fn to_safe_state(&mut self) -> Vec<Effect> {
        Vec::new()
    }
}
'''


def delete_method(root, rel, signature):
    """Removes the method starting at `signature`, up to its closing brace."""
    path = root / rel
    text = path.read_text()
    if text.count(signature) != 1:
        raise SystemExit(f"{rel}: `{signature}` is not there exactly once")
    start = text.index(signature)
    end = text.index("\n    }\n", start) + len("\n    }\n")
    path.write_text(text[:start] + text[end:])


def prepare(case, root):
    case.apply(root)
    if case.name == "a_dc_path_that_does_not_say_whether_it_is_charging_over_iso_15118":
        delete_method(
            root, "src/core/path/dc.rs", "    fn hlc_charging_active(&self) -> bool {"
        )
    if case.name == "a_fifth_power_path_that_answers_none_of_the_mode_facts":
        path = root / "src/core/path/mod.rs"
        path.write_text(path.read_text() + FIFTH_PATH)


def build(case, verbose):
    with tempfile.TemporaryDirectory(prefix="unconstructable-") as work:
        root = pathlib.Path(work)
        shutil.copytree(MODULE / "src", root / "src")
        (root / "src/main.rs").unlink(missing_ok=True)
        (root / "Cargo.toml").write_text(MANIFEST)
        prepare(case, root)
        result = subprocess.run(
            ["cargo", "build", "--offline"],
            cwd=root,
            capture_output=True,
            text=True,
            env={
                **__import__("os").environ,
                "CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER": "cc",
            },
        )
        output = result.stderr
        if verbose:
            for line in output.splitlines():
                if line.startswith("error") or "-->" in line:
                    print(f"        {line}")
        if result.returncode == 0:
            return f"IT COMPILED. The mistake is accepted again."
        if case.expect not in " ".join(output.split()):
            return (
                "refused, but not for the stated reason. Expected "
                f"`{case.expect}`, got:\n"
                + "\n".join(
                    f"        {line}"
                    for line in output.splitlines()
                    if line.startswith("error")
                )
            )
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="name the cases")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    by_class = {}
    for case in CASES:
        by_class.setdefault(case.klass, []).append(case)

    if args.list:
        for klass, cases in by_class.items():
            print(klass)
            for case in cases:
                print(f"    {case.name}")
        return 0

    failures = []
    for klass, cases in by_class.items():
        print(klass)
        for case in cases:
            problem = build(case, args.verbose)
            if problem is None:
                print(f"    refused  {case.name}")
            else:
                print(f"    ACCEPTED {case.name}: {problem}")
                failures.append(case.name)
        print()

    if failures:
        print(f"{len(failures)} construction(s) the compiler no longer refuses:")
        for name in failures:
            print(f"  {name}")
        return 1
    print(f"{len(CASES)} unreachable construction(s), all refused.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
