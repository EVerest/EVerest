.. _everest_modules_handwritten_EvController:

============
EvController
============

Simplified EV simulator with a versioned, typed MQTT API and an ``fsm::v2``
state machine. Coexists with ``EvManager`` and is targeted as its replacement.
Acts as the EV-side counterpart of
``EvseManager`` for software-in-the-loop (SIL) testing: it drives the simulated
board support (``YetiSimulator``) and speaks to the ISO 15118 stack
(``EvseV2G`` / ``Evse15118D20``) through the standard ``ISO15118_ev`` and
``ev_slac`` interfaces.

Architecture overview
=====================

The module is a single epoll-loop process. One thread (``loop_thread``) owns
the FSM and the only mutable state. Four ``fd``\ s drive that loop:

- ``wake_fd`` — external producers (MQTT commands, peer subscriptions, curve
  points) push ``Event``\ s onto a ``thread_safe_queue`` and poke this fd.
- ``state_timer_fd`` — per-state deadline (Plugged ``iso_pwm_wait_s`` after
  nominal PWM, SlacMatching 4 s MCS wake pulse then 30 s, V2G 60 s, Paused
  1 h watchdog + 15 s deferred-resume fallback, Stopping 10 s fallback,
  BcbToggling 250 ms step).
- ``tick_fd`` — periodic ``tick_interval_ms`` while ``Charging``; drives
  ``RampInterpolator`` and ``SocIntegrator``.
- ``curve_timer_fd`` — one-shot used by ``CurvePlayer`` to advance
  through the session charging curve.

High level
----------

.. mermaid:: images/architecture_overview.mmd

Runtime internals
-----------------

.. mermaid:: images/architecture_runtime.mmd

The FSM never touches peer interfaces or MQTT publishers directly. Both are
injected into ``FsmContext`` as callbacks (``PeerActions`` for ``call_*``,
``Publisher`` for MQTT). This keeps the FSM decoupled from the ev-cli
generated ``*Intf`` types and makes every state unit-testable with the mocks
under ``tests/PeerMocks.{cpp,hpp}``.

Module layout
-------------

::

    modules/EV/EvController/
    ├── manifest.yaml              # provides ev_manager; requires ev_board_support, ISO15118_ev, ev_slac, kvs
    ├── EvController.{cpp,hpp}     # ev-cli generated module shell (init/ready/shutdown)
    ├── ev_manager/                # provided-interface impl (currently a thin stub)
    ├── main/
    │   ├── EvCtlRuntime.{cpp,hpp} # epoll loop owner, fd handlers
    │   ├── FsmContext.{cpp,hpp}   # shared state + publisher helpers + free helpers
    │   ├── StateBase.{cpp,hpp}    # FSM base; Result {unhandled, new_state}
    │   ├── Events.{cpp,hpp}       # EventKind + Event variant, kind_of, command_verb
    │   ├── EventDispatch.{cpp,hpp}# feed with exception isolation -> Faulted
    │   ├── CommandRouter.{cpp,hpp}     # m2e/* MQTT -> Event queue
    │   ├── PeerSubscriptions.{cpp,hpp} # BSP/ISO/SLAC subscribe_* -> Event queue
    │   ├── CurvePlayer.{cpp,hpp}# session charging curve playback
    │   ├── RampInterpolator.{cpp,hpp}  # linear interp of charging_current_a
    │   ├── SocIntegrator.{cpp,hpp}     # per-tick SoC accumulation
    │   └── states/
    │       Disabled / Unplugged / Plugged / SlacMatching / V2GNegotiating /
    │       BcbToggling / Charging / ChargingPwmPaused / Paused / Stopping /
    │       ChargerFault / Faulted
    └── tests/                     # Catch2 unit tests; helpers + mocks

Configuration
=============

``default_session``
    JSON using the ``configure_session`` payload schema. Used when no session
    has been latched by ``configure_session`` (including a restored session).
    Default: empty, selecting AcIec from ``max_current_a`` and ``three_phases``.
    For example, ``default_session: '{"mode":"DcIsoD20","params":{"mcs_enabled":true}}'``.
    Parsed during ``init()``; invalid JSON or payloads raise
    ``generic/CommunicationFault`` with the parse message in ``ready()`` and
    use the AcIec default. The same curve and peer validation as
    ``configure_session`` runs in ``ready()``; a rejected default also raises
    ``CommunicationFault`` and falls back to AcIec.

``iso_pwm_wait_s``
    Seconds to wait for 5 percent PWM after an ISO session sees nominal PWM.
    Default: ``10``. AC ISO falls back to basic AcIec charging on expiry; DC enters ``Faulted`` with ``Internal`` and ``no 5 % PWM``.
    Nonpositive values expire after the minimum timer interval of 1 ms.

``connector_id``
    Connector id of the EVSE manager to which this simulator is connected to.

``ac_nominal_voltage``
    Nominal AC voltage between phase and neutral in Volt. Default: ``230``.

``max_current_a``
    AC max current in Ampere. Default: ``16``.

``three_phases``
    Support three phase. Default: ``true``.

``dc_max_current_limit``
    Maximum current allowed by the EV. Default: ``300``.

``dc_max_power_limit``
    Maximum power allowed by the EV. Default: ``150000``.

``dc_max_voltage_limit``
    Maximum voltage allowed by the EV. Default: ``900``.

``dc_min_voltage_limit``
    Minimum voltage accepted by the EV in Volt. Sent to the ISO peer for DC
    sessions. Default: ``150``.

``dc_energy_capacity``
    Energy capacity of the EV. Default: ``60000``.

``dc_target_current``
    Target current requested by the EV. Default: ``5``.

``dc_target_voltage``
    Target voltage requested by the EV. Default: ``200``.

``soc_initial_pct``
    SoC at start of a simulated charging process. Default: ``30``.

``departure_time_s``
    Departure time in seconds after the session starts. Default: ``86400``.

``e_amount_wh``
    Energy amount in Wh that should be charged during the session. Default: ``0``.

``force_payment_option``
    Force the use of the selected payment option. Default: ``false``.

``keep_cross_boot_plugin_state``
    Persist the configured session across boot; pilot presence is never
    restored. Requires a ``kvs`` connection. Default: ``false``.

``publish_bsp_measurements``
    Whether to publish synthetic BSP measurements. Default: ``false``.

``tick_interval_ms``
    FSM tick interval in milliseconds (drives ``RampInterpolator`` and
    ``SocIntegrator`` while ``Charging``). Default: ``100``.

``on_battery_full``
    Policy applied by ``SocIntegrator`` when SoC crosses
    ``battery_full_threshold_pct``. Default: ``clamp``. Values:

    - ``clamp`` — silently cap energy at ``battery_capacity_wh``; SoC plateaus
      at the threshold, no FSM event fires (legacy behavior).
    - ``idle_at_full`` — zero positive charge power while at/above threshold;
      session stays in ``Charging``, discharge (BPT / V2X reverse current)
      continues to subtract energy normally.
    - ``stop_session`` — rising-edge enqueues ``StopSession``; ``Charging``
      transitions to ``Stopping`` in the ISO modes and ends the session at once
      in ``AcIec`` (``Charging::on_event``, ``StopSession``). Either way the EV then stays
      in ``Plugged`` with no session, CP=B, until it is unplugged or plugged
      again.
    - ``pause_if_iso`` — rising-edge enqueues ``PauseSession`` when
      ``charge_mode`` is one of ``AcIso2`` / ``AcIsoD20`` / ``AcIsoD20DerIec`` /
      ``AcIsoD20DerSae`` / ``DcIso2`` / ``DcIsoD20`` (``Charging`` → ``Paused``); falls back to
      ``idle_at_full`` semantics in ``AcIec`` (no FSM event, charge power
      zeroed).

    The value is declared as a manifest ``enum``, so an unrecognized string
    is rejected by the framework's config validation when the config loads,
    before the module is constructed. ``parse_on_battery_full`` still
    throws on an unknown value; it is now the second line of defence rather
    than the first, and it is what a programmatic ``Conf`` (a unit fixture)
    still hits.

    The edge is one-shot per crossing: ``vars.was_full`` latches when SoC
    reaches the threshold and clears when SoC drops back below, so a
    discharge-then-recharge cycle re-arms the policy.

    .. note::
       ``clamp`` and ``idle_at_full`` are indistinguishable at the default
       ``battery_full_threshold_pct`` of ``100``. They differ only by the
       post-integration trim back to the threshold energy, and that trim is
       guarded by ``battery_charge_wh > threshold_wh``, a condition the
       preceding hard clamp to ``battery_capacity_wh`` makes unreachable
       when ``threshold_wh == capacity_wh``. Choosing ``idle_at_full`` is
       therefore only meaningful with a threshold strictly below 100.

``battery_full_threshold_pct``
    SoC percentage at which ``on_battery_full`` fires (edge-triggered).
    Default: ``100`` (physical full). Set lower to simulate strategies like
    "charge to 80%".

``cfg_communication_check_to_s``
    Communication check timeout in seconds. Default: ``0`` (check disabled).
    When ``0`` no external heartbeat is required. When ``> 0``, the test driver
    must publish ``m2e/communication_check true`` faster than this timeout or
    EvController raises a ``CommunicationFault`` on its ``ev_manager`` interface
    (surfaced by evse_manager as connector ``OtherError``). Disabled by default
    to match historical EvManager behavior; opt in by setting ``> 0``.

``cfg_heartbeat_interval_ms``
    Heartbeat publish interval in milliseconds. Default: ``1000``.

``enabled_at_startup``
    Auto-enable the simulator at module startup (equivalent to publishing
    ``m2e/enable true`` immediately after ready). Default: ``true``. Set
    ``false`` for tests that need to observe the initial ``Disabled`` state
    before driving enable.

Migrating a config from EvManager
---------------------------------

EvController deliberately has no ``auto_exec`` / ``auto_exec_commands``
config keys. EvManager took its whole session script as one
semicolon-separated string inside the EVerest config, which put test
choreography into the shipped configuration and gave the module a runtime
string parser.

The script now lives on the driving side instead. ``EverestTestController``
in everest-testing holds the command string for each of its ``plug_in_*``
helpers and plays it over the external MQTT API through
``EvControllerDriver.play_dsl`` (``sim_registry.py``), which keeps EvManager's
command-string grammar. A config carries no session script at all.

A production config drives the simulator over the `External MQTT API`_, or
uses EvDriverSim to run preset curve playbacks.

For HIL, connect EvController directly to the physical board. For example,
``config/config-CB-EVAL-EV.yaml`` replaces the session script with:

.. code-block:: yaml

   ev_manager:
     module: EvController
     config_module:
       connector_id: 1
       default_session: '{"mode":"DcIso2"}'
       dc_target_current: 5
       dc_target_voltage: 444
     connections:
       ev_board_support:
         - module_id: ev_bsp_1
           implementation_id: main
       ev:
         - module_id: iso15118_ev
           implementation_id: ev
       slac:
         - module_id: ev_slac
           implementation_id: main

The physical pilot starts the session. After a stop the EV started, unplug
and plug again for another session; after a stop the charger started, the
charger's 5 percent PWM wake-up starts it. No EvDriverSim is needed. The MCS counterpart uses
``default_session: '{"mode":"DcIsoD20","params":{"mcs_enabled":true}}'``
and connects ``slac`` to ``mcs_ev_datalink``.

Interfaces
==========

Provides
--------

``ev_manager`` (``interface: ev_manager``)
    Republishes ``bsp_event`` and ``ev_info`` from the simulated EV so peer
    in-process modules (typically ``EvAPI``) see a drop-in replacement for the
    ``ev_manager`` interface offered by the legacy ``EvManager`` module.

Requires
--------

``ev_board_support`` (``interface: ev_board_support``, **required**, 1)
    Drives the board-support peer (``YetiSimulator``). Subscribes to
    ``bsp_event``, ``bsp_measurement``, ``ev_info``. Invokes
    ``set_cp_state``, ``allow_power_on``, ``set_ac_max_current``,
    ``set_three_phases``, ``enable``. The
    peer gates its simulation loop on ``enable``, so ``set_cp_state`` only
    reaches the EVSE while it is armed.

``ev`` (``interface: ISO15118_ev``, optional, 0..1)
    EV-side ISO 15118 peer (``Ev15118``), which talks to the charger's
    ``EvseV2G`` / ``Evse15118D20``. Subscribes to
    ``ev_power_ready``, ``stop_from_charger``, ``v2g_session_finished``,
    ``dc_power_on``, ``pause_from_charger``, ``ac_evse_max_current``,
    ``ac_evse_target_power``, ``dc_evse_present_current``,
    ``dc_evse_present_voltage``, ``der_negotiated_functions``,
    ``der_control_received``, ``v2g_messages``. Invokes
    ``start_charging``, ``stop_charging``, ``pause_charging``,
    ``abort_charging``, ``update_soc``, ``enable_sae_j2847_v2g_v2h``,
    ``set_bpt_dc_params``, ``set_dc_params``, ``cp_state_changed``,
    ``update_present_values``.

``slac`` (``interface: ev_slac``, optional, 0..1)
    PLC SLAC peer. Subscribes to ``state`` (translated to a string). Invokes
    ``reset`` and ``trigger_matching``.

``kvs`` (``interface: kvs``, optional, 0..1)
    Persistent key/value store. Used only when ``keep_cross_boot_plugin_state``
    is ``true``, to persist ``PersistedState`` (the last accepted
    ``configure_session`` spec). With that flag set and no ``kvs`` connected,
    ``init()`` logs an error and fails the module.

External MQTT API
=================

The module exposes a versioned, typed MQTT API. All topics share the prefix:

::

    <mqtt_external_prefix>everest_api/1/ev_controller/<module_id>/<m2e|e2m>/<suffix>

``m2e/*`` carries inbound commands (manager-to-EV); ``e2m/*`` carries outbound
state and events (EV-to-manager). Payload shapes for every suffix are defined
in the typed schema at
``lib/everest/everest_api_types/include/everest_api_types/ev_controller/API.hpp``.

Plugging, unplugging and electrical fault injection are world operations served
only by the paired EvDriverSim's ``m2e/plug``, ``m2e/unplug``,
``m2e/inject_fault`` and ``m2e/clear_fault``. HIL uses the physical pilot.
EvController starts on pilot presence. It publishes its software faults and,
when the charger signals E/F on the pilot, a ``ChargerFault``.

Inbound suffixes (``m2e/*``):

.. list-table::
   :header-rows: 1
   :widths: 20 18 62

   * - Suffix
     - Resulting event
     - Payload
   * - ``enable``
     - ``Enable`` (true) / ``Disable`` (false)
     - ``bool``
   * - ``configure_session``
     - ``ConfigureSession`` (intercepted pre-FSM)
     - ``SessionConfigParams`` — a mode-tagged object
       ``{ "mode": <ChargeMode>, "params": { … } }``. ``mode`` selects one of
       ``AcIec`` / ``AcIso2`` / ``AcIsoD20`` / ``AcIsoD20DerIec`` /
       ``AcIsoD20DerSae`` / ``DcIso2`` / ``DcIsoD20`` and ``params`` only carries
       the fields valid for that mode:
       ``AcIec`` → ``charging_current_a?``, ``three_phases?``, ``curve?``;
       ``AcIso2`` adds ``payment?``, ``departure_time_s?``, ``e_amount_wh?``;
       ``AcIsoD20`` = ``AcIso2`` + ``bpt?``;
       ``AcIsoD20DerIec`` and ``AcIsoD20DerSae`` carry the same fields as
       ``AcIso2``, with no ``bpt`` or ``flavor`` field; the mode selects the annex;
       ``DcIso2`` → ``payment?``, ``departure_time_s?``, ``e_amount_wh?``,
       ``curve?``; ``DcIsoD20`` = ``DcIso2`` + ``bpt?`` + ``mcs_enabled``.
       Unknown ``mode`` is rejected; fields foreign to a mode are ignored.
       Accepted in **any** state and **not** fed to the FSM: intercepted on
       the loop thread, validated (curve non-empty + strictly monotonic;
       ISO/SLAC peer presence for ISO modes), then latched into
       ``ctx.configured_session`` with an **Accepted** ``command_ack``
       (or **Rejected** with a reason, leaving the prior latch intact).
       The latched config is consumed at every ``plug``; reconfiguring
       replaces it and applies at the next plug. A ``plug`` with no prior
       ``configure_session`` synthesizes a bare ``AcIec`` session from
       ``cfg.max_current_a`` / ``cfg.three_phases``.
   * - ``stop_session``
     - ``StopSession``
     - ``StopSessionParams`` (abort?). ``{}`` is a clean stop; ``abort: true``
       makes ``Stopping`` call ``abort_charging`` instead of ``stop_charging``.
   * - ``pause_session``
     - ``PauseSession``
     - —
   * - ``resume_session``
     - ``ResumeSession``
     - —
   * - ``set_charging_current``
     - ``SetChargingCurrent``
     - ``SetChargingCurrentParams`` (current_a, three_phases, ramp_ms?)
   * - ``set_soc``
     - ``SetSoc``
     - ``SetSocParams`` (soc_pct)
   * - ``set_present_values``
     - ``SetPresentValues``
     - ``SetPresentValuesParams`` (present_voltage?, present_active_power?).
       Per field: an unset field leaves that override alone rather than clearing
       it. An override that is never set reports the value the EVSE delivered.
       Cleared on entry to ``Unplugged``.
   * - ``bcb_toggle``
     - ``BcbToggle``
     - ``BcbToggleParams``
   * - ``query_state``
     - ``QueryStateCmd``
     - — (FSM handles via ``handle_query_state``, re-publishing
       ``e2m/state``; no transition)
   * - ``communication_check``
     - — (direct: ``comm_check.set_value``, bypasses the queue)
     - ``bool``
   * - ``raise_error``
     - ``RaiseErrorCmd``
     - ``Error`` (parsed on the MQTT thread; the queued event applies
       ``p_ev_manager->raise_error`` on the loop thread)
   * - ``clear_error``
     - ``ClearErrorCmd``
     - ``Error`` (parsed on the MQTT thread; the queued event applies
       ``p_ev_manager->clear_error`` on the loop thread)

Outbound suffixes (``e2m/*``):

- ``state`` — current ``FsmState``
- ``ev_info`` — synthesized ``EvInfo`` (SoC, capacity, charge, target
  current/voltage)
- ``bsp_event`` — peer BSP events as a typed ``BspEventKind``
- ``slac_state`` — SLAC peer state as a typed ``SlacStateKind``
- ``iso_session_event`` — ``IsoSessionEvent`` (kind + optional ``dc_voltage_v``
  and ``dc_current_a``) on each ISO lifecycle edge the ISO peer reports:
  ``PowerReady``, ``DcPowerOn``, ``StopFromCharger``, ``PauseFromCharger`` and
  ``V2GFinished``. Published on the way past the FSM, so an edge a state ignores
  still reaches the topic.
- ``der_negotiated_functions`` — ``DerNegotiatedFunctions``: ``flavor``
  (``IEC`` or ``SAE``) and ``functions`` (negotiated function names), forwarded
  from the ISO peer.
- ``der_control`` — ``DerControl``: ``flavor`` and ``source``
  (``ChargeParameterDiscovery`` or ``ChargeLoop``), plus the ISO peer's optional
  ``permit_service``, ``enabled_functions``, ``problems``, ``dso_q_setpoint``
  (var) and ``dso_cos_phi_setpoint``. Absent fields remain absent.
- ``fault`` — ``FaultReport`` (type + optional message + optional
  ``rcd_mA``) on entry to ``Faulted``, and with type ``ChargerFault`` and
  message ``pilot E`` or ``pilot F`` when a pilot E/F enters ``ChargerFault``
- ``command_ack`` — ``{command, status, reason}``. Out-of-state commands are
  always acknowledged with ``status: "Rejected"`` and a human-readable
  ``reason``; never silently dropped. Accepted commands also publish
  ``status: "Accepted"`` where applicable.
- ``heartbeat`` — monotonic counter published every
  ``cfg_heartbeat_interval_ms``
- ``bsp_measurement`` — only when ``publish_bsp_measurements: true``

State machine
=============

The FSM is an ``fsm::v2::FSM<StateBase>``. Each state is a class deriving from
``StateBase`` that overrides ``enter()``, ``on_event(Event)``, and optionally
the protected ``on_leave()`` hook. ``feed`` dispatches to ``on_event`` and
records pilot presence before the FSM enters the next state. ``leave()``
itself is non-virtual: it runs ``on_leave()`` and then cancels the state timer,
so no state can skip that cleanup. ``feed`` returns a ``StateBase::Result``:

- ``{unhandled=false, new_state=nullptr}`` — handled, stay in current state.
- ``{unhandled=false, new_state=<S>}`` — handled, transition to ``S``.
- ``{unhandled=true,  new_state=nullptr}`` — event ignored by this state.

``pilot_present`` tracks BSP events in every state, including ``Disabled``:
``Disconnected`` clears it, A through D set it, and E/F and contactor events
leave it unchanged: F is the charger signalling a fault, and E is a pilot at
0 V, the charger off or the pilot shorted (EvDriverSim's ``CpErrorE``
produces it). Neither says whether the EV is plugged. If the first pilot event is E or F, the EV stays
unplugged even when enabled; a later A, B or C provides the plug edge.
Pilot A, B or C moves ``Unplugged`` to
``Plugged`` only when the pilot was absent. An echo of the car's own A while
presence is already known is ignored.

Entering ``Unplugged`` with the pilot already present queues
an internal ``BeginSession`` to enter ``Plugged`` without depending on a
board echo.
This starts a session when an attached car is enabled. Entry leaves CP
unchanged until ``Plugged`` commands B, avoiding an A-then-B pulse toward the charger. Without presence, entry
still commands A. A disconnect before the queued event cancels the start
and commands A.
Persisted session configuration is retained, but a persisted plug does not
start a session without physical presence.

Entering ``Unplugged`` resets the optional SLAC peer before a later
``trigger_matching``. The eight session states (``Plugged``, ``SlacMatching``,
``V2GNegotiating``, ``BcbToggling``, ``Charging``, ``ChargingPwmPaused``,
``Paused`` and ``Stopping``), and ``Unplugged`` while a start on a present
pilot is pending, treat E/F alike as a charger fault, whichever produced it:
they call ``abort_charging``,
``allow_power_on(false)``, CP B and SLAC ``reset``, in that order, publish
``e2m/fault`` with type ``ChargerFault`` and message ``pilot E`` or
``pilot F``, then enter ``ChargerFault``. Optional ISO and SLAC peers are
skipped when absent. ``Disabled`` and ``Faulted`` ignore E/F.

``ChargerFault`` waits for A or a BSP measurement with 4 < duty < 97 percent,
then enters ``Plugged`` with a fresh session using the latched configuration.
CP B lets the charger restart its PWM once the fault clears. Repeated E/F
events have no further effects. ``Disconnected`` moves each session state
and ``ChargerFault`` or ``Faulted`` to ``Unplugged``.

ISO sessions stay in ``Plugged`` until a BSP measurement
reports 4 < duty < 7 percent. Nominal PWM (7 < duty < 97) arms
``iso_pwm_wait_s`` once; repeated measurements do not extend it. A 5 percent
measurement cancels the wait and enters ``SlacMatching``. On expiry, AC ISO
becomes AcIec and follows the basic charging entry path; DC faults with
``Internal`` and ``no 5 % PWM``.

A stop the charger started (``IsoStopFromCharger`` in ``V2GNegotiating`` or
``Charging``) restarts on the charger's wake-up, as ISO 15118-2 and DIN have
no charger pause: the charger ends the session and resumes with 5 percent
PWM. ``Stopping`` remembers the charger stop; when it completes, the EV
resets SLAC and enters ``Plugged`` without ``hold_session``, so
``BeginSession`` takes the same configured session again and the resumed
5 percent PWM starts a new SLAC match and V2G session. The wake-up is an
edge: the charger keeps 5 percent PWM on for about 1.5 s after the stop
([V2G-DC-968]), so ``Plugged`` sets ``awaiting_pwm_wake`` and ignores
5 percent until a measurement outside 4 < duty < 7 has been seen. A charger
that never wakes the EV leaves it waiting in ``Plugged``. A stop the EV
started (``stop_session``, an ``on_battery_full`` stop) ends held in
``Plugged`` with no session.

.. mermaid:: images/stop_outcome.mmd

For an MCS session, ``SlacMatching`` holds CP C for 4 s,
then commands B before ``trigger_matching`` (IEC 61851-23-3 CC.5.2.4).
The pilot lifecycle's SLAC reset precedes this pulse. The 30 second SLAC
matching deadline starts after the pulse, and leaving the state cancels it.

Top-level lifecycle
-------------------

Boot, plug-in, disable, and fault entry/exit. Charging detail in the two
diagrams below.

.. mermaid:: images/state_machine_top.mmd

ISO 15118 charging flow
-----------------------

SLAC matching → V2G negotiation → Charging, with pause/resume (BCB
toggling) and stop paths. Covers AcIso2, AcIsoD20, AcIsoD20DerIec,
AcIsoD20DerSae, DcIso2, DcIsoD20.

An ISO session finish (``IsoV2GFinished``) received in ``Charging`` enters
``Stopping`` even without a charger stop notification or an E/F pilot event.
Stopping turns EV power off and sets CP B; that finish was the session's
last, so it reaches ``Plugged`` on its 10 s deadline. The session ended
without an EV stop, so it restarts like a charger stop: SLAC is reset and the
next 5 percent PWM edge starts the next session. An EV-requested stop
enters ``Stopping`` before the finish arrives. In ``Paused`` the finish ends the
paused session's teardown and releases a deferred resume, if one is pending.

.. mermaid:: images/state_machine_iso.mmd

AC IEC charging flow
--------------------

PWM duty-cycle driven; no SLAC or V2G negotiation. Direct
Plugged → Charging on duty in (7%, 97%); PWM-pause when duty exits that
band.

.. mermaid:: images/state_machine_ac_iec.mmd

Per-state reference
-------------------

.. list-table::
   :header-rows: 1
   :widths: 20 12 12 56

   * - State
     - State timer
     - Tick
     - Entry actions
   * - ``Disabled``
     - —
     - —
     - publish state
   * - ``Unplugged``
     - —
     - —
     - Enable BSP, CP=A unless pilot presence is already known, disable power,
       stop ISO, reset SLAC and session variables, clear the live hold, and
       stop curve playback. A present pilot queues ``BeginSession``;
       otherwise a fresh A/B/C edge enters ``Plugged``.
   * - ``Plugged``
     - ``iso_pwm_wait_s`` after nominal PWM (pilot ISO)
     - —
     - Bump ``plug_generation`` and enqueue ``BeginSession`` tagged with it
       unless ``hold_session`` is set (plugged with no session, after a
       stop the EV started), then CP=B, ``allow_power_on(false)``,
       reset the resume gate
       (``iso_session_active``/``resume_pending``/``bcb_pending``),
       ``BeginSession`` is queued ahead of the CP change, so
       anything a peer sends in reply comes after it. One tagged with an
       earlier plug's generation, or arriving while ``hold_session`` is set,
       begins nothing and is not acknowledged
   * - ``SlacMatching``
     - 4 s MCS pulse, then 30 s
     - —
     - For MCS, CP C then B after 4 s. Then
       ``slac_trigger_matching``, ``vars.slac_state="MATCHING"``; on failure
       enqueues a synthetic ``InternalFault`` whose ``message`` is
       ``"no ev_slac peer"`` (peer absent) or
       ``"ev_slac trigger_matching rejected"`` (wired peer returned false),
       routing to ``Faulted`` without arming the 30 s deadline
   * - ``V2GNegotiating``
     - 60 s
     - —
     - ``iso_start_charging(mode, …)`` (if ``charge_mode`` set); on rejection
       enqueues a synthetic ``InternalFault`` with
       ``message="iso_start_charging rejected"`` → ``Faulted`` (no 60 s wait)
   * - ``BcbToggling``
     - 250 ms (re-armed)
     - —
     - default ``bcb_remaining=6``, CP=B; each deadline toggles B↔C and
       decrements
   * - ``Charging``
     - —
     - ``tick_interval_ms``
     - CP=C, ``allow_power_on(true)``, arm SoC tick, re-apply the AC current
       clamped to any EVSE ceiling received during ``Plugged`` (AC modes),
       append the session's ``pending_curve`` to the ``CurvePlayer``
   * - ``ChargingPwmPaused``
     - —
     - —
     - CP=B, ``allow_power_on(false)`` (tick disarmed by ``Charging::leave``)
   * - ``Paused``
     - 1 h watchdog; 15 s deferred-resume fallback
     - —
     - ``allow_power_on(false)``, ``iso_pause_charging``; CP is set to **B
       only when no ISO session is still active** — during an ISO pause
       teardown CP is **held at C** until the EVCC receives ``PowerDeliveryRes``
       from the SECC (``e2m`` peer var ``v2g_messages`` → ``V2gMessage``),
       then lowered to B. ``IsoV2GFinished`` re-asserts B as a backstop for
       peers that publish no per-message signal.
   * - ``Stopping``
     - 10 s fallback
     - —
     - ``iso_end_session`` (``abort_charging`` when the stop asked to
       abort, otherwise ``stop_charging``), ``allow_power_on(false)``, CP=B.
       Ends on ``IsoV2GFinished`` or the 10 s deadline, which first calls
       ``iso_stop_charging`` again. A disconnect enters ``Unplugged``
       immediately. A completed charger stop resets SLAC and ends in
       ``Plugged`` ready for a new session on the next 5 percent PWM edge;
       any other completed stop ends in ``Plugged`` with no session
       (``hold_session``). The session's charging curve ends with it; the
       independent EvDriverSim preset can continue.
   * - ``ChargerFault``
     - —
     - —
     - Entered with power off and the charger fault already reported on
       ``e2m/fault``. Reset session variables and stop holding a completed
       session; publish state. A or running PWM starts a fresh session; ``Disconnected``
       enters ``Unplugged``; ``Disable`` enters ``Disabled``. Session commands
       are rejected with ``charger fault``.
   * - ``Faulted``
     - —
     - —
     - Drop the session and power permission, stop ISO, publish the software
       ``FaultReport`` and state. Only ``Disconnected`` returns to ``Unplugged``.
       World faults and their board actions belong to EvDriverSim.

The free helpers in ``FsmContext.cpp`` are reused across states:

- ``transition_to_fault(ctx, p)`` — convert ``InternalFault`` to ``FaultReport``
  and enter ``Faulted``. The diagnostic travels in the queued event;
  ``rcd_mA`` is unset. Software fault types are ``SlacTimeout``, ``V2GTimeout``
  and ``Internal``; ``ChargerFault`` comes only from the E/F handler and
  never enters ``Faulted``.
- ``transition_to_disabled(ctx)`` — CP=A, power off, stop ISO, clear session
  state and stop curve playback, then disable BSP. It leaves world faults
  under EvDriverSim's control.
- ``handle_query_state(ctx, s)`` — re-publish ``e2m/state`` for late
  subscribers; do not transition. Every state answers, ``Faulted``
  included: the command backs operator logging and current-state display,
  so a faulted simulator is precisely when the answer matters. The
  property is asserted over all twelve state classes at once in
  ``StateTransitionsTest.cpp`` rather than left to per-state sections.

Pause / resume gating
---------------------

Pausing and resuming an ISO session is gated on whether the underlying V2G
communication session is still live, so a resume never races a half-torn-down
session.

- **``iso_session_active`` gate** — set when ``iso_start_charging`` succeeds
  (``FsmContext::iso_start_charging``) and cleared on ``IsoV2GFinished``
  (``EvCtlRuntime``, before the FSM feed). It distinguishes a live ISO session from one that
  has already torn down.
- **CP release on ``PowerDeliveryRes``** — ``Paused`` holds CP=C until the ISO
  peer reports the pause's ``PowerDeliveryRes`` has arrived, then drops to B
  (``Paused.cpp``). The SECC's ISO 15118-2 implementation waits up to 1.5 s
  for CP=B after a WeldingDetectionReq (or a non-pause SessionStopReq),
  holding the response until CP=B arrives or that timer expires
  (``lib/everest/iso15118/src/iso15118/d2/state/welding_detection.cpp``).
  Dropping CP on ``PowerDeliveryRes`` lets the SECC observe B within this window.
  ``IsoV2GFinished`` re-asserts B as a backstop for
  peers that publish no per-message signal (``Paused::on_event``, ``IsoV2GFinished``).
- **Deferred ISO resume** — in ``Paused``, a ``ResumeSession`` received while
  ``iso_session_active`` does not resume immediately; it sets ``resume_pending``
  and ``bcb_pending=6`` and arms a 15 s fallback timer (``Paused::on_event``, ``ResumeSession``).
  The resume is released on ``IsoV2GFinished`` (``Paused::on_event``); the
  15 s timer is only a best-effort backstop.
- **``BcbToggle`` gating + bounds** — an explicit ``BcbToggle`` in ``Paused``
  validates ``count`` (``> 0`` and ``<= 1000``) and, while
  ``iso_session_active``, defers via ``bcb_pending`` (``Paused::on_event``, ``BcbToggle``);
  ``BcbToggling::enter`` carries a reseed guard for a zero remaining count
  (``BcbToggling::enter``).
- **DC resume skips SLAC re-match** — re-entry to ``SlacMatching`` happens only
  when ``vars.slac_unmatched`` is set (on a SLAC ``UNMATCHED`` event,
  ``EvCtlRuntime``); a DC ``D-LINK_PAUSE`` keeps the data link and
  goes straight to ``V2GNegotiating`` (``BcbToggling::on_event``, ``StateDeadline``).
- **``V2GNegotiating`` resume path** — when ``resume_awaiting_pwm`` is set,
  ``V2GNegotiating`` arms the 60 s timer, re-asserts CP=B, and defers
  ``start_charging`` until a PWM-running BSP measurement arrives
  (``V2GNegotiating::enter``, and ``BspMeasurement`` in its ``on_event``).
- **DC ``dc_power_on`` → ``Charging``** — ``IsoDcPowerOn`` transitions to
  ``Charging`` (``V2GNegotiating::on_event``); the prior ``IsoPowerReady`` for
  DC asserts CP=C and power-on but stays in ``V2GNegotiating``
  (same handler, ``IsoPowerReady``).

Drive mechanisms
================

The simulator can be driven by four independent producers, all of which land
on the same FSM event queue:

.. mermaid:: images/drive_mechanisms.mmd

1. **External MQTT commands** — ``CommandRouter`` (``main/CommandRouter.cpp``)
   subscribes to each ``m2e/<verb>`` topic. Each handler deserializes the
   payload via ``everest_api_types`` codecs, constructs an ``Event``, and
   calls ``EvCtlRuntime::enqueue``. ``communication_check`` is the only verb
   that bypasses the queue, routed directly to ``comm_check``;
   ``raise_error`` / ``clear_error`` parse on the MQTT thread but enqueue a
   ``RaiseErrorCmd`` / ``ClearErrorCmd`` so the ``p_ev_manager`` interaction
   runs on the loop thread.

2. **Peer-module variables** — ``PeerSubscriptions``
   (``main/PeerSubscriptions.cpp``) registers ``subscribe_*`` callbacks on
   the BSP, ISO 15118-ev, and SLAC requirements. ISO and SLAC are guarded by
   ``min_connections: 0``; if unconnected they are silently skipped.

3. **Charging curves** — ``CurvePlayer`` schedules the configured session's
   current updates while the session is active.

4. **Internal timers**:

   - ``state_timer_fd`` (per-state deadline) → enqueues a ``StateDeadline``
     event from ``EvCtlRuntime::on_state_timer``.
   - ``tick_fd`` (only while ``Charging``) → calls
     ``ramp_step`` then ``soc_step`` directly on the
     loop thread; no FSM event produced.
   - ``curve_timer_fd`` → ``CurvePlayer::on_timer_fire``.

Event ingress / egress
----------------------

Command path
~~~~~~~~~~~~

Generic m2e command lifecycle: external publisher → MQTT topic →
``CommandRouter`` → ``Event`` queue → ``EvCtlRuntime`` flush → FSM
``feed``.

.. mermaid:: images/event_ingress_command.mmd

Session sequence
~~~~~~~~~~~~~~~~

Peer-driven progression from the internal ``BeginSession`` (enqueued by
``Plugged::enter`` to consume the latched config or configured default) through ``SlacMatching``,
``V2GNegotiating``, and into ``Charging``. Per-tick activity in ``Charging``
covered separately below. The pilot path includes
the PWM gate and the optional MCS pulse described above.

.. mermaid:: images/event_ingress_session.mmd

Per-tick activity
~~~~~~~~~~~~~~~~~

``tick_fd`` fires every ``tick_interval_ms`` while ``Charging``. The loop
thread runs ``ramp_step`` then ``soc_step`` directly
— no FSM event is produced.

.. mermaid:: images/event_ingress_tick.mmd

Preset curve playbacks run in :ref:`EvDriverSim <everest_modules_handwritten_EvDriverSim>`.
EvController accepts session intent and plays the configured charging curve.

Session API templates
=====================

Copy-paste recipes for driving the most common session shapes. Each
template gives the ``EvControllerTestController`` form (Python integration
tests) and the equivalent raw ``m2e/*`` MQTT publish sequence (any
language / manual ``mosquitto_pub``). Raw payloads use the suffixes from
`External MQTT API`_; topic prefix is
``<mqtt_external_prefix>everest_api/1/ev_controller/<module_id>/m2e/``.
``run_scenario`` exists only on EvDriverSim, at
``<mqtt_external_prefix>everest_api/1/ev_driver_sim/<driver module_id>/m2e/``; the SIL
configs name that driver ``<module_id>_driver``. On the SIL configs, raw ``plug`` and
``unplug`` go there too because the cars start only on the pilot.

All recipes assume the simulator was enabled (``m2e/enable`` → ``true``,
done implicitly by ``controller.start()``) and a compatible SIL config is
running (see `SIL configurations`_).

AC DER — IEC or SAE
-------------------

Configure an ISO 15118-20 AMD1 AC DER session before pilot presence:

.. code-block:: json

    {"mode":"AcIsoD20DerIec","params":{"charging_current_a":16,"three_phases":true}}

Publish this payload to ``m2e/configure_session``. Use mode ``AcIsoD20DerSae``
for SAE DER. The mode selects ``AC_DER_IEC`` or ``AC_DER_SAE`` on the ISO peer;
payment, departure time, requested energy and AC current/curve options behave
as in ``AcIsoD20``. The ISO peer supplies its configured DER profile. A rejected
``start_charging`` transitions to ``Faulted`` with type ``Internal`` and message
``iso_start_charging rejected``. The ISO command returns only a boolean, so a
profile-specific diagnostic remains in the ISO peer's log.

Negotiated functions and received controls appear on
``e2m/der_negotiated_functions`` and ``e2m/der_control``.

AC IEC — basic charge
---------------------

PWM-driven AC, no SLAC/V2G. ``plug_in()`` defaults to 32 A / 3-phase.

.. code-block:: python

    ctrl.start()
    ctrl.plug_in()                                      # blocks until Charging
    assert ctrl.state_collector.wait_for_soc_progress(timeout=30)
    ctrl.plug_out()
    assert ctrl.state_collector.wait_for_state("Unplugged", timeout=10)

.. code-block:: text

    configure_session {"mode":"AcIec","params":{"charging_current_a":32.0,"three_phases":true}}
    plug              {}
    # ... charge ...
    unplug          {}

AC IEC — pause / resume
-----------------------

``pause_session`` moves ``Charging`` → ``Paused`` directly, and
``resume_session`` returns to ``Charging`` with no BCB toggling: there is no
HLC to re-establish.

Separately and independently, a BSP duty cycle leaving the (7 %, 97 %) band
moves ``Charging`` → ``ChargingPwmPaused`` and back. That path is EVSE-driven
and rejects the ISO pause verbs with ``"AC IEC: ISO verbs not applicable"``.

.. code-block:: python

    ctrl.start()
    ctrl.plug_in()
    assert ctrl.state_collector.wait_for_state("Charging", timeout=30)
    ctrl.pause_session()
    assert ctrl.state_collector.wait_for_state_not("Charging", timeout=10)
    ctrl.resume_session()
    assert ctrl.state_collector.wait_for_state("Charging", timeout=15)
    ctrl.plug_out()

.. code-block:: text

    configure_session {"mode":"AcIec","params":{"charging_current_a":32.0,"three_phases":true}}
    plug              {}
    pause_session   {}
    resume_session  {}
    unplug          {}

AC ISO 15118-2
--------------

SLAC → V2G → Charging. Optional ``payment`` (``"ExternalPayment"`` / ``"Contract"``).

.. code-block:: python

    ctrl.start()
    ctrl.plug_in_ac_iso(payment_type="ExternalPayment")
    assert ctrl.state_collector.wait_for_state("Charging", timeout=60)
    ctrl.plug_out_iso()                                 # graceful stop_session + unplug

.. code-block:: text

    configure_session {"mode":"AcIso2","params":{"charging_current_a":16.0,"three_phases":true,"payment":"ExternalPayment"}}
    plug              {}
    stop_session    {}
    unplug          {}

DC ISO 15118-2 — basic
----------------------

The workhorse DC route. ``plug_in_dc_iso`` carries no current params; the
EVSE drives the setpoint.

.. code-block:: python

    ctrl.start()
    ctrl.plug_in_dc_iso(payment_type="ExternalPayment")
    assert ctrl.state_collector.wait_for_state("Charging", timeout=30)
    assert ctrl.state_collector.wait_for_soc_progress(timeout=30)
    ctrl.plug_out_iso()

.. code-block:: text

    configure_session {"mode":"DcIso2","params":{"payment":"ExternalPayment"}}
    plug              {}
    stop_session    {}
    unplug          {}

DC ISO 15118-2 — EV-initiated pause / resume
--------------------------------------------

ISO pause-from-EV (distinct from the AC PWM pause).

.. code-block:: python

    ctrl.start()
    ctrl.plug_in_dc_iso()
    assert ctrl.state_collector.wait_for_state("Charging", timeout=30)
    ctrl.pause_session()
    assert ctrl.state_collector.wait_for_state_not("Charging", timeout=10)
    ctrl.resume_session()
    assert ctrl.state_collector.wait_for_state("Charging", timeout=15)
    ctrl.plug_out_iso()

DC ISO 15118-2 — fault then recover
-----------------------------------

Inject a fault mid-charge, then unplug, clear it and start another session.
EvDriverSim ``inject_fault`` accepts ``DiodeFail``, ``RcdError`` and
``CpErrorE``; ``diode_fail()`` is the ``"DiodeFail"`` shortcut.

.. code-block:: python

    ctrl.start()
    ctrl.plug_in_dc_iso()
    assert ctrl.state_collector.wait_for_state("Charging", timeout=30)
    ctrl.inject_fault("DiodeFail")
    assert ctrl.state_collector.wait_for_state_not("Charging", timeout=10)
    ctrl.plug_out()
    assert ctrl.state_collector.wait_for_state("Unplugged", timeout=10)
    ctrl.clear_fault()
    ctrl.plug_in_dc_iso()
    assert ctrl.state_collector.wait_for_state("Charging", timeout=30)

.. code-block:: text

    configure_session {"mode":"DcIso2","params":{}}
    plug              {}
    inject_fault      {"type":"DiodeFail"}
    unplug            {}
    clear_fault       {}
    configure_session {"mode":"DcIso2","params":{}}
    plug              {}

DC ISO 15118-20 (D20)
---------------------

.. code-block:: python

    ctrl.start()
    ctrl.plug_in_dc_d20(payment_type="ExternalPayment")
    assert ctrl.state_collector.wait_for_state("Charging", timeout=60)
    ctrl.plug_out_iso()

.. code-block:: text

    configure_session {"mode":"DcIsoD20","params":{"payment":"ExternalPayment"}}
    plug              {}
    stop_session    {}
    unplug          {}

DC BPT (bidirectional, ISO-20)
------------------------------

Bidirectional power transfer. ``bpt=True`` sends ``DEFAULT_BPT``;
discharge phases produce negative power in the ``SocIntegrator``.

.. code-block:: python

    ctrl.start()
    ctrl.plug_in_dc_d20(bpt=True)                       # or bpt={...}
    assert ctrl.state_collector.wait_for_state("Charging", timeout=60)
    assert ctrl.state_collector.wait_for_soc_progress(timeout=60)
    ctrl.plug_out_iso()

.. code-block:: text

    configure_session {"mode":"DcIsoD20","params":{"bpt":{ ... }}}
    plug              {}

DC MCS (ISO-20)
---------------

Megawatt Charging System. ``mcs_enabled`` is a boolean flag on the wire.

.. code-block:: python

    ctrl.start()
    ctrl.plug_in_dc_d20(mcs=True)
    assert ctrl.state_collector.wait_for_state("Charging", timeout=60)
    assert ctrl.state_collector.wait_for_soc_progress(timeout=60)
    ctrl.plug_out_iso()

.. code-block:: text

    configure_session {"mode":"DcIsoD20","params":{"mcs_enabled":true}}
    plug              {}

Declarative charging curve
--------------------------

Run a piecewise current profile. ``play_charging_curve`` latches one
``configure_session`` carrying the whole curve; ``Charging::enter`` appends
its points to the ``CurvePlayer``, which then emits one
``set_charging_current`` per point. ``loop=True`` marks the appended range as a
loop segment, so the dispatcher rewinds and replays it.

The curve is consumed at the **next plug**, so latch it before plugging and
plug with ``plug()`` — the ``plug_in_*`` helpers configure-then-plug in one
call and would overwrite it. Note: in any mode other than ``AcIec`` /
``AcIso2`` the EVSE owns the setpoint, so each ``set_charging_current`` is
acknowledged ``Rejected`` with
``"set_charging_current not supported in DC/ISO-20 mode"`` — the ack stream is
still the observable contract.

.. code-block:: python

    ctrl.start()
    ctrl.play_charging_curve(
        points=[
            {"t_offset_ms": 0, "current_a": 100.0, "three_phases": False},
            {"t_offset_ms": 30000, "current_a": 50.0, "three_phases": False,
             "ramp_ms": 10000},
        ],
        loop=False,
        mode="DcIso2",
    )
    ctrl.plug()
    assert ctrl.state_collector.wait_for_state("Charging", timeout=30)

Or run a built-in curve preset by name (see :ref:`EvDriverSim <everest_modules_handwritten_EvDriverSim>`):

.. code-block:: python

    ctrl.run_scenario("DcIsoTaper")
    assert ctrl.state_collector.wait_for_state("Charging", timeout=30)

Runtime current ramp
--------------------

Ad-hoc linear ramp to a new current over a window (valid where the EV owns
the setpoint, e.g. ``AcIsoBasic`` / AC IEC).

.. code-block:: python

    ctrl.start()
    ctrl.run_scenario("AcIsoBasic")
    assert ctrl.state_collector.wait_for_state("Charging", timeout=30)
    ctrl.ramp_to_current(target_a=16, three_phases=True, duration_s=5)
    assert ctrl.state_collector.wait_for_soc_progress(timeout=30)

.. code-block:: text

    set_charging_current  {"current_a":16.0,"three_phases":true,"ramp_ms":5000}

Fully scenario-driven
---------------------

A preset is a self-contained timed step list (plug → … → unplug); the
controller only needs ``run_scenario`` and a state assertion. Smallest
possible end-to-end driver:

.. code-block:: python

    ctrl.start()
    ctrl.run_scenario("DcIsoD20Basic")
    assert ctrl.state_collector.wait_for_state("Charging", timeout=60)

.. code-block:: text

    run_scenario    {"name":"DcIsoD20Basic"}

Subsystems
==========

RampInterpolator
----------------

``ramp_step(ctx, now)`` interpolates ``vars.charging_current_a``
linearly between ``ActiveRamp::start_a`` and ``ActiveRamp::target_a`` between
``start_at`` and ``end_at``. Triggered when ``SetChargingCurrent`` carries a
non-zero ``ramp_ms``. Each tick calls
``ctx.set_desired_ac_params(current, three_phases)``; on ``now >= end_at`` it
snaps to target and clears ``vars.active_ramp``. Called before
``soc_step`` so SoC integration sees the freshly interpolated
current on the same tick.

SocIntegrator
-------------

``soc_step(ctx)`` integrates power into
``vars.battery_charge_wh`` once per tick while ``Charging``:

- AC modes: ``power_w = effective_ac_current_a * ac_nominal_voltage * (three_phases ? 3 : 1)``,
  where ``effective_ac_current_a`` clamps the desired current to the
  EVSE-advertised AC ceiling (``evse_ac_max_current_a``) when present.
  An aggregate power target is divided across the configured phases. When
  separate L2 and L3 targets are present, ``target_active_power`` is the L1
  target and is divided only by voltage to obtain the current ceiling.
- DC modes: ``power_w = effective_dc_current_a() * vars.dc_present_voltage_v``,
  where ``effective_dc_current_a()`` returns the live measured
  ``vars.evse_dc_present_current_a`` (an ``optional``) when a present-current
  event has arrived, else the open-loop fallback
  ``clamp(cfg.dc_target_current, 0, cfg.dc_max_current_limit)``.
  ``vars.dc_present_voltage_v`` is seeded from ``cfg.dc_target_voltage`` at
  construction.
- ``delta_wh = power_w * (tick_ms / 3.6e6)``

Power may be negative when BPT / V2X discharge is active (negative
``charging_current_a`` for AC or a negative live ``evse_dc_present_current_a``
for DC; the open-loop DC fallback is floored at ``0`` and never discharges);
the integrator subtracts energy and the post-clamp floor at ``0`` handles
underflow symmetrically to the overshoot clamp at ``battery_capacity_wh``.

After integrating and clamping, SoC is derived from
``battery_charge_wh / battery_capacity_wh * 100``. ``e2m/ev_info`` and
internal ``ev_info`` are published, and ``iso_update_soc(soc_pct)`` is called
every tick; the call is a no-op unless the ISO peer is connected.

``on_battery_full`` policy
~~~~~~~~~~~~~~~~~~~~~~~~~~

When SoC reaches ``cfg.battery_full_threshold_pct``, the integrator applies
the policy named by ``cfg.on_battery_full`` (see the Configuration section
for value semantics). For every policy except ``clamp``, accumulated charge
is trimmed back to the threshold energy *after* integration (positive power
only; discharge is never trimmed), so the threshold is reached exactly on
the crossing tick with no one-tick overshoot past it. ``vars.was_full`` is
an edge latch: it is set the first tick the threshold is crossed and cleared
when SoC drops below, so the ``stop_session`` / ``pause_if_iso`` FSM events
fire exactly once per rising edge — including the crossing tick itself.

Because the trim is the *only* difference between ``clamp`` and
``idle_at_full``, and the trim cannot fire at a threshold of 100 (see the
note under ``on_battery_full`` in the Configuration section), those two
policies diverge only below 100. ``tests/core_tests/evctl_battery_full_test.py``
is the in-tree consumer that selects a non-default policy and drives it end to
end, over ``config-sil.yaml`` with ``EVCTL_BATTERY_FULL_SESSION`` applied.

CurvePlayer
-----------

``CurvePlayer`` plays the session's charging curve. On entry to ``Charging``,
the pending curve's points are appended for playback. The
``curve_timer_fd`` dispatches the next point into the car's event queue;
points fire in due-time order, with insertion order breaking ties. A looping
curve repeats its points until the session ends. ``reset_session_vars``
drops the pending points and loop, so playback cannot outlive its session.
Preset timing belongs to EvDriverSim and is independent of curve playback.

Command rejection
=================

Out-of-state commands are not silently dropped. They are acknowledged on
``e2m/command_ack`` with ``status: "Rejected"`` and a ``reason`` string
identifying why the command was refused. Accepted commands likewise publish
``status: "Accepted"`` on the same channel where applicable.

Reason strings include ``"module disabled"``, ``"no session active"``,
``"slac matching in progress"``, ``"negotiation in progress"``,
``"BCB toggling in progress"``, ``"session paused"``, ``"session stopping"``,
``"charger fault"``,
``"AC IEC: ISO verbs not applicable"``.

Persistence
===========

When ``keep_cross_boot_plugin_state`` is ``true``, a ``kvs`` requirement must
be connected: ``init()`` refuses the flag without one, logging an error and
failing the module, rather than run with persistence silently off. Each
accepted ``configure_session`` then persists the configured session as JSON.
``kvs_load()`` runs at module ``ready()`` and applies the same validation as
``configure_session``. Invalid or malformed session data is discarded.

Legacy ``plugged_in`` and ``hold_session`` keys are ignored. The current pilot
alone decides the first start after a restart. A session the EV stopped remains
held in ``Plugged`` until the pilot is disconnected and connected again, or a
pilot E/F; that hold is not persisted.

Tests
=====

C++ unit tests
--------------

Built into the ``everest-core_EvController_tests`` Catch2 binary when
``BUILD_TESTING`` is set (which enables the internal
``EVEREST_CORE_BUILD_TESTING`` gate the module CMake checks).
Production state-machine sources are compiled directly into the test
binary; no MQTT broker is required.

.. list-table::
   :header-rows: 1
   :widths: 26 14 60

   * - File
     - Tags
     - Coverage
   * - ``BcbTogglingTest.cpp``
     - ``[evctl][group2]``
     - ``BcbToggling`` entry/exit, 6× ``StateDeadline`` produces 3 B↔C
       round-trips and transitions to ``V2GNegotiating``;
       ``Paused::on_event(ResumeSession)`` seeds ``bcb_remaining=6``.
   * - ``RampInterpolatorTest.cpp``
     - ``[evctl][ramp]``
     - Linear interp against an injected clock; clamp on overshoot;
       three-phases flag propagation; no-op without ``active_ramp``.
   * - ``CurvePlayerTest.cpp``
     - ``[evctl][curve][curve]``,
       ``[evctl][curve][curve][timing]``,
       ``[evctl][curve][controlled-clock]``,
       ``[evctl][curve][loop-guard]``,
       ``[evctl][curve][rearm]``,
       ``[evctl][curve][order]``,
       ``[evctl][session-end][curve][curve]``
     - Charging curve validation, playback, looping and session cleanup;
       points fire in due-time order, ties in insertion order.
   * - ``SocIntegratorTest.cpp``
     - ``[evctl][soc]``
     - Parity with legacy ``EvManager`` formula for AC 1-/3-phase and DC
       ISO-2; capacity clamp; ``iso_update_soc`` routing; ``ev_info``
       publish on both topics.
   * - ``StateTransitionsTest.cpp``
     - ``[evctl][helpers]``,
       ``[evctl][group1]``,
       ``[evctl][group2]``,
       ``[evctl][group3]``,
       ``[evctl][fault_isolation]``,
       ``[evctl][query_state]``
     - ``FsmContext`` helpers + per-state transitions across all charge
       modes: ``Disabled``/``Unplugged``/``Plugged`` (``group1``), then BCB,
       ``Paused``, ``Charging`` and ``Faulted`` (``group2``/``group3``); BSP
       duty-cycle thresholding; SLAC-absent rejection; the
       ``feed_with_fault_isolation`` reroot to ``Faulted``; and the
       ``QueryState`` property over all twelve states.
   * - ``AcLimitTest.cpp``
     - ``[evctl][aclimit]``
     - AC charging current clamped to the EVSE-advertised ceiling
       (``effective_ac_current_a``).
   * - ``CommandRouterTest.cpp``
     - ``[evctl][command_router]``
     - ``m2e/*`` decode contract: malformed payloads rejected.
   * - ``CommCheckHandlerTeardownTest.cpp``
     - ``[evctl][comm_check]``
     - Heartbeat / communication-check teardown: ``stop_heartbeat`` joins the
       thread, is idempotent and no-op-safe; destructor stops and joins an
       active heartbeat.
   * - ``ConfigureSessionTest.cpp``
     - ``[evctl][configure_session]``
     - ``configure_session`` interceptor latch / reject contract.
   * - ``EventsTest.cpp``
     - ``[evctl][events]``
     - ``kind_of`` maps every ``Event`` variant alternative to its
       ``EventKind`` and round-trips.
   * - ``EveryEventKindTest.cpp``
     - ``[evctl][events]``
     - Every state answers every ``EventKind`` without throwing.
   * - ``IsoEvCapabilitiesTest.cpp``
     - ``[evctl][iso][cp]``, ``[evctl][iso][present-values]``,
       ``[evctl][iso][abort]``, ``[evctl][iso][session-boundary]``
     - ``cp_state_changed`` reports, ``update_present_values`` defaults and
       overrides, abort versus clean stop, and what a fresh session does not
       inherit.
   * - ``PausedResumeGateTest.cpp``
     - ``[evctl][group2]``
     - Pause / resume gating: ``BcbToggle`` and ``ResumeSession`` defer while a
       V2G session is live and release on ``IsoV2GFinished``; deferred resume
       falls back to ``BcbToggling`` on ``StateDeadline``; an idle ``Paused``
       deadline still stops the session.
   * - ``RuntimeTickTimerTest.cpp``
     - ``[evctl][runtime][tick]``,
       ``[evctl][runtime][passthrough]``,
       ``[evctl][runtime][curve-timer]``,
       ``[evctl][runtime][state-timer]``
     - ``EvCtlRuntime`` fd compositions: passthrough-vars routing, ``on_tick``
       ramp + SoC, curve-timer dispatch, state-timer ``StateDeadline`` feed.
   * - ``SilentFailureFixesTest.cpp``
     - ``[evctl][kvs]``, ``[evctl][kvs][optional]``,
       ``[evctl][config]``, ``[evctl][slac]``,
       ``[evctl][v2g]``, ``[evctl][fault]``,
       ``[evctl][errors]``, ``[evctl][peers]``,
       ``[evctl][codec][to_json]``
     - Regression guards for the silent-failure fixes: ``kvs_load``
       missing / empty / corrupt; enum and ``on_battery_full`` validation;
       ``SlacMatching`` / ``V2GNegotiating`` fault routing;
       ``transition_to_fault`` message preservation; ``raise_error`` /
       ``clear_error`` loop-thread routing.
   * - ``SessionEndTest.cpp``
     - ``[evctl][session-end]``
     - Plugged idle after a session ends, pilot cycling, stopping timeout,
       unplug during ISO charging and stale BeginSession suppression.
   * - ``CrossBootPlugTest.cpp``
     - ``[evctl][kvs]``,
       ``[evctl][kvs][pilot-only]``,
       ``[evctl][kvs][config]``
     - Configured-session persistence and validation; old plug/hold flags
       cannot start a session or override the current pilot; the init
       refusal of ``keep_cross_boot_plugin_state`` without a ``kvs``
       connection.
   * - ``PilotStartTest.cpp``
     - ``[evctl][pilot-start]``
     - A/B/C presence starts a session, disconnect cancels queued starts
       and ends every connected state including ``Faulted``. Pilot faults
       interrupt charging, also from a pending start, with power off and a
       ``ChargerFault`` report, and recover through ``ChargerFault``.
   * - ``StartRulesTest.cpp``
     - ``[evctl][start-rules]``,
       ``[evctl][start-rules][restart]``
     - Pilot start rules: the 5 percent gate, the bounded nominal PWM wait
       and its AcIec or fault fallback, the MCS wake pulse, default session
       parsing and validation, and the restart after a charger stop next to
       the hold after an EV stop.
   * - ``TimerFdFlushTest.cpp``
     - ``[evctl][timer_fd]``,
       ``[evctl][fd_event_handler]``
     - ``timer_fd`` pending-fire / ``EAGAIN`` / stale-fire semantics;
       ``fd_event_handler`` rejects duplicate fd registration.

Test helpers under ``tests/`` (``TestFixture.hpp``, ``PeerMocks.{cpp,hpp}``,
``PublisherSink.{cpp,hpp}``, ``TimerSink.{cpp,hpp}``) wire mock
``PeerActions``, an MQTT-publisher sink that records ``(topic, payload)``
pairs, and a timer sink that records every arm/cancel call into a fresh
``FsmContext`` per ``SECTION``.

Python test controller
----------------------

Integration tests drive the simulator through
``EvControllerTestController`` (in
``applications/utils/everest-testing/src/everest/testing/core_utils/controller/evctl_test_controller.py``).
It resolves the EvController module id from the active config, publishes to
``m2e/*`` and subscribes to ``e2m/+`` through a paho client, and exposes a
``StateCollector`` with a ``threading.Condition`` for ``wait_for_state`` /
``wait_for_state_not``.

Minimal usage:

.. code-block:: python

    from everest.testing.core_utils.controller.evctl_test_controller import EvControllerTestController

    def test_my_scenario(everest_core, evctl_test_controller):
        evctl_test_controller.start()
        evctl_test_controller.plug_in_dc_iso(payment_type="ExternalPayment")
        assert evctl_test_controller.state_collector.wait_for_state("Charging", timeout=30)
        evctl_test_controller.plug_out_iso()

For a declarative charging profile, run a preset by name:

.. code-block:: python

    evctl_test_controller.run_scenario("DcIsoTaper")

For an ad-hoc current change with linear ramp:

.. code-block:: python

    evctl_test_controller.ramp_to_current(target_a=16, three_phases=True, duration_s=5)

Selected controller methods:

- ``start`` / ``stop``
- ``plug_in`` / ``plug_in_ac_iso`` / ``plug_in_dc_iso`` /
  ``plug_in_dc_d20`` (``bpt`` and ``mcs`` options)
- ``plug_out`` / ``plug_out_iso``
- ``pause_session`` / ``resume_session``
- ``inject_fault(type)`` / ``diode_fail`` / ``clear_fault``
- ``run_scenario(name)`` / ``play_charging_curve(points, loop, mode)``
- ``set_soc(soc_pct)`` / ``set_charging_current(current_a, three_phases, ramp_ms=None)``
- ``ramp_to_current(target_a, three_phases, duration_s)``
- ``query_state`` (publishes ``m2e/query_state`` and returns the answering
  ``e2m/state`` value, or ``None`` on timeout) /
  ``last_observed_state`` (passive cache read, no publish)

Python smoke tests (``tests/core_tests/``):

- ``evctl_dc_iso2_test.py`` — DC ISO 15118-2 round trips, EVSE-side stop,
  EV-side pause/resume.
- ``evctl_curve_test.py`` — ``DcIsoTaper`` curve via ``run_scenario`` plus
  runtime ``ramp_to_current``.
- ``evctl_d20_test.py`` — ``DcIsoD20Basic`` end-to-end.
- ``evctl_bpt_mcs_test.py`` — ``DcIsoBpt`` over ``config-sil-dc-d20.yaml`` and
  ``DcIsoMcs`` against ``config-sil-mcs.yaml``.
- ``evctl_battery_full_test.py`` — ``on_battery_full: stop_session`` at an
  80 % threshold, plus the ``m2e/query_state`` wire round trip.
- ``evctl_ac_iec_test.py`` — AC IEC 61851 basic charge plus PWM pause/resume,
  no SLAC / V2G negotiation, and a bare plug with no prior
  ``configure_session``, which is the only path that exercises the FSM
  synthesizing a session from the module's own config defaults.
- ``evctl_timing_override_test.py`` — pause/resume driven by a compressed
  ``run_scenario`` ``timing`` override, plus rejection of an inapplicable
  timing override.

SIL configurations
==================

The 37 SIL configurations under ``config/`` that use EvController and the 20
OCPP auxiliary configurations under
``tests/ocpp_tests/test_sets/everest-aux/config/`` connect each car through its
own EvDriverSim. The driver gates the pilot between the car and YetiSimulator:

.. code-block:: text

    EvController -> EvDriverSim/board_support -> YetiSimulator/ev_board_support

Each driver is named ``<car module id>_driver``. For example:

.. code-block:: yaml

    active_modules:
      ev_manager:
        module: EvController
        config_module:
          connector_id: 1
        connections:
          ev_board_support:
            - module_id: ev_manager_driver
              implementation_id: board_support
      ev_manager_driver:
        module: EvDriverSim
        config_module:
          ev_controller_module_id: ev_manager
        connections:
          ev_board_support:
            - module_id: yeti_driver
              implementation_id: ev_board_support
      yeti_driver:
        module: YetiSimulator
        config_module:
          connector_id: 1

These configurations start unplugged, except ``config-sil-ac-temp-derating.yaml``,
whose driver sets ``plugged_at_startup: true``. Tests configure the car's session and
send ``plug`` to the driver through the Python controller. A configuration that
needs a cable connected at startup can set ``plugged_at_startup: true`` on the
driver. The car's persisted session does not control the pilot.
See :ref:`everest_modules_handwritten_EvDriverSim` for the world API.

The suites run on the mainline SIL configs under ``config/``, adjusted at test
time by ``tests/core_tests/evctl_config_adjustment.py``. The adjustment connects
a persistent store, disables the charger's Node-RED interface for headless runs,
and applies the suite's car and charger settings.

.. list-table::
   :header-rows: 1
   :widths: 40 34 26

   * - Suite
     - Config
     - Adjustment
   * - ``evctl_ac_iec_test.py``
     - ``config-sil.yaml``
     - defaults
   * - ``evctl_timing_override_test.py``
     - ``config-sil.yaml``
     - defaults
   * - ``evctl_curve_test.py``
     - ``config-sil.yaml`` / ``config-sil-dc.yaml``
     - ``EVCTL_DC_SESSION`` for the DC case
   * - ``evctl_battery_full_test.py``
     - ``config-sil.yaml``
     - ``EVCTL_BATTERY_FULL_SESSION``
   * - ``evctl_dc_iso2_test.py``
     - ``config-sil-dc.yaml``
     - ``EVCTL_DC_SESSION``, DIN 70121 off
   * - ``evctl_d20_test.py``
     - ``config-sil-dc-d20.yaml``
     - ``EVCTL_DC_SESSION``
   * - ``evctl_bpt_mcs_test.py``
     - ``config-sil-dc-d20.yaml`` / ``config-sil-mcs.yaml``
     - ``EVCTL_DC_SESSION``, ``DC_BPT,DC`` services
