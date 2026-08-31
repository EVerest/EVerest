# slac_meow

The SLAC library (ISO 15118-3 data link negotiation, EVSE and EV side): the wire protocol plus two
state machines built on `fsm::v2` from `everest::util`, in the style libiso15118 uses - one class
per state, `feed()` dispatching to the next state.

It is self-contained. It sits beside `slac_neo`, the same behaviour on `boost::msm`, and does not
touch it: `slac_neo` stays exactly as upstream has it, so the two implementations can be diffed
against each other and against upstream. This library copied the protocol sources it needs rather
than sharing them, which is why **no target may link both `slac::meow` and `slac::neo`** - they
define the same protocol symbols. Nothing does.

The name is temporary. Nothing in a path or a namespace mentions `meow`, so once `slac_neo` goes
the library becomes plain `slac` with one directory rename and one line in
`cmake/ev-lib-dependencies.cmake`.

## Layout

Three areas, kept apart by directory rather than by library. The rule is small enough to state in
full: `protocol/` may not include from `evse/`, `ev/` or `io/`; the machines may include
`protocol/` and `time.hpp` and nothing else; `io/` may include `protocol/` only.

    include/everest/slac/
      time.hpp             TimePoint, TimeSource, Timer
      status.hpp           SlacState, D3State, Status
      protocol/            THE DEFINITIONS - no state machine may be named here
        defs.hpp           constants and the ISO 15118-3 Table A.1 timings
        messages.hpp       the wire structs
        homeplug_message.hpp
        types.hpp          MacAddress, Nmk, RunId
        utils.hpp          NMK/NID derivation, modem version strings
        builders.hpp       message construction shared by both sides
        format.hpp         MAC and NMK formatting
      evse/                THE EVSE STATE MACHINE
        event.hpp          Reset, Update, EnterBcd, LeaveBcd, Message
        states.hpp         StateID, Result, StateBase, StateTree
        context.hpp        Context, ContextCallbacks
        config.hpp         Config and its enums
        session_data.hpp   per matching session data
        fsm.hpp            facade - class EvseFSM
        state/*.hpp        one file per state
        detail/*.hpp       pure helpers, unit testable without a state machine
      ev/                  THE EV STATE MACHINE
        event.hpp          Reset, Update, TriggerMatching, Message
        states.hpp         StateID, Result, StateBase
        context.hpp        Context, ContextCallbacks, Config
        fsm.hpp            facade - class EvFSM
        state/*.hpp        one file per state
        detail/guards.hpp
      io/                  THE TRANSPORT - a dead end, see below
        socket.hpp  event.hpp

`src/` mirrors this path for path. Following iso15118 there is no `src/**/detail/` mirror: a
detail header's implementation sits beside its area, and `detail` marks an internal header
without appearing in the namespace.

**`io/` is deliberately a dead end.** Nothing in `protocol/`, `evse/` or `ev/` includes it, so the
state machines cannot reach the transport even by accident - they take messages in and hand
messages out, and it is up to the layer above to decide how those travel. It is here only so the
existing module wiring keeps working; lifting it into the SLAC module is a folder move and four
lines out of `target_sources`, with no source changes anywhere else.

Each side's event variant lists only what that machine can receive, so the type is an honest
statement of its inputs - the EVSE machine has no `TriggerMatching`, the EV machine no control
pilot events.

Namespaces follow the directories exactly: `everest::slac::evse{,::state,::state::session}`,
`everest::slac::ev{,::state}` and `everest::slac::io`. The protocol keeps the namespaces it
already had - `everest::slac::defs`, `::messages`, `::utils`, and `everest::slac` for the wire
types - so `protocol/` groups without adding a namespace component, as `common/` does in
ieee2030_1_1.

Because this library includes nothing from `slac_neo`, and in particular no header declaring
`everest::lib::slac::fsm`, unqualified `fsm::` resolves to the state machine library and needs no
leading `::`. The facades are named `EvseFSM` and `EvFSM` rather than `fsm`, to keep a second
meaning out of that name.

## The contract

Modelled on `iso15118/d20/states.hpp`, with two deliberate differences.

**Events carry their payload.** `fsm::v2::FSM::feed()` is a forwarding variadic template, so a
Homeplug frame travels with the event:

```cpp
// the EVSE side; the EV side has TriggerMatching in place of the pilot and count events
using SlacEvent = std::variant<event::Reset, event::Update, event::EnterBcd,
                               event::LeaveBcd, event::CountBc, event::Message>;
using Message = std::reference_wrapper<messages::HomeplugMessage const>;
```

libiso15118 parks the payload on the context and pulls it back out inside the state. Carrying it
on the event keeps a state's inputs explicit, removes the temporal coupling of "write, feed,
read back", and lets one message reach every concurrent matching session without a copy. The
message is borrowed for the duration of the call and never stored, which is what the
`reference_wrapper` says out loud.

States do not unwrap it by hand: `get_if_message(ev)` returns the message itself, or `nullptr`
for any other event, so the three tests in a `feed()` body read alike -
`std::get_if<event::Update>(&ev)`, `get_if_message(ev)`, `std::get_if<event::Reset>(&ev)`.

**`Result` can say "handled, no transition".** The reference implementation in the `fsm::v2`
tests has that constructor; libiso15118 omits it and therefore reports every non-transitioning
`feed` as unhandled. Composite states here need the distinction.

## Where boost::msm had features fsm::v2 does not

| boost::msm | here |
|---|---|
| Orthogonal regions | sequential code in one `feed()`, in the same order msm processed the regions |
| Exit pseudo states | the state inspects its own progress and returns the next state |
| Anonymous (completion) transitions | the choice of initial sub-state, made on entry |
| Compile time reflection for telemetry | `describe()` / `signature()`, implemented by the states |
| Machine scoped members | the EV `Context`; per-composite data lives on the composite |

Two msm semantics are load bearing and are called out in comments where they apply:

- **Row priority is reverse declaration order**, so give-up is checked before retry, and the EV
  `Sounding` checks timeout, then burst-complete, then send-next.
- **An internal transition does not re-arm the state's deadline; an external self transition
  does.** The EV sounding burst relies on the first, the retries on the second.

## Known deviations from slac_neo

- **Telemetry tree shape.** The published `SlacFsmState` differs. msm derived its names from C++
  type names (`Matching_def`), surfaced its orthogonal region states (`Init`, `Listen`, `Pipe`)
  as a nesting level, and keyed `submachines` by the last visited child rather than the
  submachine - reproducing that would mean inventing region names and copying a bug. The shape
  (`states` / `submachines` / `sessions`) and the type are unchanged; the contents are the real
  state hierarchy. No test covers this.
- **WaitForLink with an unsupported modem** fails one tick later than msm did, because
  `fsm::v2` has no completion event and `enter()` cannot transition.
- **Sounding ends as soon as the last M-Sound arrives**, rather than always sitting out the full
  `TT_EVSE_match_MNBC` window. ISO 15118-3 V2G3-A09-44 computes the average when all M-Sounds are
  received *or* the timer expires, and the legacy `lib/everest/slac` does exactly that, logging a
  warning when it has to fall back on the timeout. slac_neo cannot: its attenuation profiles are
  consumed by an internal transition on `Sounding`, which in boost::msm outranks the machine's own
  `enough_sounds` row, so that guard never sees the tenth profile and matching always costs the
  full 600 ms. This is the one place slac_meow deliberately behaves differently from slac_neo -
  it restores the legacy and standard behaviour, and saves roughly 470 ms per matching session.

Two msm behaviours are reproduced deliberately, not fixed here, so that the two implementations
stay comparable:

- `Matched` to `Failed` signals the error routine twice when `ac_mode_five_percent` is set.
- `ResetChip` never completes for a modem that is neither Qualcomm nor Lumissil; nothing is sent
  and nothing answers. `chip_reset.timeout_ms` is configured but never read, in either
  implementation.

## Tests

The three `slac_neo` state machine test binaries are carried over with two changed lines each -
the include and a using directive - so both implementations are held to the same behaviour.

    ctest -R "slac_meow|slac_neo"
