.. SPDX-License-Identifier: Apache-2.0
.. Copyright Pionix GmbH and Contributors to EVerest

.. _everest_modules_handwritten_EvDriverSim:

EvDriverSim
===========

EvDriverSim simulates the world around an EvController in software-in-the-loop
(SIL). It connects the car to a simulated charging station, disconnects it, and
injects electrical faults. Hardware configurations leave it out: the physical
cable, pilot and electrical circuit drive those events.

Wiring
------

The car requires EvDriverSim's ``board_support`` implementation of
``ev_board_support``. EvDriverSim's single ``ev_board_support`` requirement
connects to YetiSimulator's EV board support implementation::

    EvController -> EvDriverSim/board_support -> YetiSimulator/ev_board_support

For example, the driver's configuration is:

.. code-block:: yaml

    ev_manager_driver:
      module: EvDriverSim
      config_module:
        ev_controller_module_id: ev_manager
      connections:
        ev_board_support:
          - module_id: yeti_driver
            implementation_id: ev_board_support

The car starts on the pilot and connects its ``ev_board_support``
requirement to module ``ev_manager_driver``, implementation ``board_support``.
``ev_controller_module_id`` selects the heartbeat and intent topics; it does not create
that interface connection.

The 37 simulation configurations under ``config/`` that instantiate
EvController use this wiring: ``config-sil.yaml``, ``config-sil-rpcapi.yaml``, ``config-sil-ac-d20.yaml``,
``config-sil-dc.yaml`` and its ``consumer-api``, ``d20``, ``isomux``, ``rpcapi``
and ``tls`` variants, ``config-sil-mcs.yaml``, ``config-sil-energy-management.yaml``,
``config-sil-ocpp.yaml`` and its ``API``, ``custom-extension`` and ``pnc`` variants,
``config-sil-ocpp201.yaml``, ``config-sil-ocpp201-pnc.yaml``, and the three
``config-sil-two-evse`` configurations (base, ``dc`` and ``rpcapi``).
The Ev15118 module-test configurations also use this pair: ``config-sil-evsev2g``,
``config-sil-dc-evsev2g``, ``config-sil-dc-tls-evsev2g``,
``config-sil-ac-d20-plaintext``, ``config-sil-dc-d20-plaintext``,
``config-sil-dc-d20-tls``, ``config-sil-ac-bpt-d20``, ``config-sil-dc-bpt-d20``
and ``config-sil-ac-der-d20`` (all with the ``.yaml`` extension). So do
``config-eebus``, ``config-sil-ac-d20-der-iec``, ``config-sil-ac-d20-der-sae``,
``config-sil-ac-soc``, ``config-sil-ac-temp-derating``,
``config-sil-dc-sae-v2g``, ``config-sil-dc-sae-v2h`` and
``config-sil-ocpp-multi``.
The 20 EvController configurations under
``tests/ocpp_tests/test_sets/everest-aux/config/`` use it as well. Each car has
its own driver named ``<car module id>_driver``, including configurations with
two cars. All of these drivers but ``config-sil-ac-temp-derating``'s start with
the pilot withheld; tests explicitly plug through the driver. The hardware
configurations ``config-CB-EVAL-EV.yaml`` and ``config-CB-EVAL-MCS-EV.yaml``
connect EvController to a physical board and use no driver.

.. mermaid:: images/wiring.mmd

Pilot gate
----------

By default, the pilot starts ``Withheld``. At readiness, the driver commands
board CP A and publishes one ``Disconnected`` event to the car. While withheld,
board events, measurements and EV information are dropped, and the car's CP
commands do not reach the board. Diode and RCD commands pass through unless a
matching world fault is active, as described below. Other board commands are
forwarded unchanged.

``plug`` changes the gate to ``Passed`` and publishes A to the car. Board events,
measurements and EV information then pass through. ``unplug`` withholds the
pilot, commands board CP A and publishes ``Disconnected``. With
``plugged_at_startup: true``, readiness announces A and starts with the gate open.

``plug`` while already passed is rejected with reason ``already plugged``.
``unplug`` while withheld is rejected with reason ``already unplugged``.
Neither rejection sends a board command or a car event, including when CP E is
active.

World API
---------

Topics use the prefix ``everest_api/1/ev_driver_sim/<module id>/``. Commands
arrive on ``m2e/<command>``:

.. list-table:: Commands
   :header-rows: 1
   :widths: 20 35 45

   * - Command
     - Payload
     - Effect
   * - ``plug``
     - None
     - Pass the pilot and announce A.
   * - ``unplug``
     - None
     - Withhold the pilot and announce disconnection.
   * - ``inject_fault``
     - ``{"type":"DiodeFail"}``
     - Set the board's diode failure flag.
   * - ``inject_fault``
     - ``{"type":"RcdError","rcd_mA":30.0}``
     - Set residual current in mA; a finite, nonnegative value is required.
   * - ``inject_fault``
     - ``{"type":"CpErrorE"}``
     - Command board CP E; the board's echo delivers E to the car.
   * - ``clear_fault``
     - None
     - Undo the active fault.
   * - ``run_scenario``
     - ``{"name":"AcIecBasic","timing":{"stop_after_ms":1000}}``
     - Run a preset after the controller heartbeat; see Scenarios below.

``inject_fault`` also accepts an optional string ``message`` for the caller's
annotation. Only one fault can be active; a second injection is rejected.
Diode and RCD faults may be injected while unplugged and remain set across
unplug. Each world fault owns its board state until cleared. During DiodeFail,
car ``diode_fail`` commands are remembered without reaching the board; during
RcdError, the same holds for car ``set_rcd_error`` commands. Without a matching
world fault, these commands are forwarded and remembered. ``clear_fault``
restores the car's last remembered value, defaulting to ``false`` for the diode
and ``0`` for RCD current if the car has not commanded a value.
After forwarding car ``enable(false)``, the driver reapplies the active world
fault (including its injected RCD current) and then CP A if the pilot is withheld,
since disabling YetiSimulator resets its board state.

CP E injection while withheld is rejected with reason ``unplugged``. While CP E
is active, car CP commands are remembered and each command re-sends board CP E.
``clear_fault`` restores the latest car CP command. Unplug clears CP E because
the cable is gone, then applies the usual CP A and disconnection sequence.
YetiSimulator clears its E latch when commanded to A, B, C or D.

The board is the only source of E events; EvDriverSim does not synthesize them.
Each reassertion of E can produce another board echo, so the car may receive E
more than once while the fault is active.

YetiSimulator does not report forced F to the EV side. When the EVSE forces
``pwm_error_f``, YetiSimulator ignores an E command without latching the fault
or publishing an E event. The driver still accepts the injection and retains
the world fault. After F clears, the next car CP command re-sends E so the
fault reaches the board. Until that command, an E injection dropped under F
has not reached the board; the driver cannot retry on F's release alone.

.. mermaid:: images/pilot_gate.mmd

The driver publishes:

* ``e2m/world_state`` at readiness and on every change, for example
  ``{"pilot":"Passed","fault":"DiodeFail"}``. The ``fault`` field is omitted
  when no fault is active.
* One ``e2m/command_ack`` publication attempt for every command, including
  malformed fault payloads.
  Accepted example: ``{"command":"plug","status":"Accepted"}``.
  Rejected example:
  ``{"command":"inject_fault","status":"Rejected","reason":"unplugged"}``.
* ``e2m/heartbeat`` as a JSON unsigned counter, starting at zero.

If publishing ``command_ack`` throws, the external MQTT callback logs the failure
and returns so later commands can be handled. The request retains its chosen
local state, whether accepted or rejected; an active scenario continues and a
terminal run remains terminal. The driver makes no retry or second acknowledgement.
A transport failure cannot guarantee that the acknowledgement reached the client.

The optional bridge communication check uses ``m2e/communication_check`` with a
JSON boolean, following the other EVerest API modules.

Plug, charge, unplug
--------------------

A session as the world sees it. The car's own CP and power commands reach the
board only while the pilot is passed; EvseManager drives the PWM through
YetiSimulator's charger side.

.. mermaid:: images/plug_charge_unplug.mmd

Scenarios
---------

``m2e/run_scenario`` takes ``{"name":"AcIecBasic","timing":{"stop_after_ms":1000}}``.
The ``timing`` object is optional. The driver refuses the request until it
has seen the selected EvController's heartbeat, while another preset is
pending or running, and while the pilot is already passed. Rejection publishes
both a Rejected ``command_ack`` with ``reason`` and a Rejected ``scenario_event`` with ``message``. Malformed payloads
produce a rejected acknowledgement; an unknown name cannot form a typed event.

Every preset publishes ``configure_session`` to the car and waits for its
Accepted acknowledgement before attempting the initial plug. The pending wait
is armed before publication. The driver listens to the paired car's
``everest_api/1/ev_controller/<ev_controller_module_id>/e2m/command_ack`` topic
and ignores acknowledgements for other commands. A Rejected acknowledgement
fails the run with the car's reason. No acknowledgement within 10 seconds fails
the run with a timeout reason. Neither path passes the pilot.

The protocol has no request id. The single pending run owns the next
``configure_session`` acknowledgement; concurrent independent publishers of that
command to the same car cannot be correlated. A late acknowledgement after a
timeout can also be mistaken for a subsequent run's reply.

Preset t=0 is the configuration acceptance and successful plug point, so
acknowledgement latency does not consume timed offsets.
Intent steps use
``everest_api/1/ev_controller/<ev_controller_module_id>/m2e/<verb>``.
World steps use the same pilot and fault path as the driver's MQTT commands.
Internal steps do not produce command acknowledgements, so they cannot claim
a reply belonging to an external command.

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Scenario
     - Step sequence
   * - ``AcIecBasic``
     - t=0: ConfigureSession(AcIec, 16A, 3ph). t=30s: StopSession.
   * - ``AcIecPauseResume``
     - t=0: ConfigureSession(AcIec, 16A, 3ph). t=30s: Pause. t=60s:
       Resume. t=120s: Stop. t=125s: Unplug.
   * - ``AcIsoBasic``
     - t=0: ConfigureSession(AcIso2, 16A, 3ph). t=60s: StopSession.
   * - ``AcIsoD20Basic``
     - t=0: ConfigureSession(AcIsoD20, 16A, 3ph). t=120s: Stop. t=125s:
       Unplug.
   * - ``DcIsoBasic``
     - t=0: ConfigureSession(DcIso2). No scripted stop.
   * - ``DcIsoD20Basic``
     - t=0: ConfigureSession(DcIsoD20). t=180s: Stop. t=185s: Unplug.
   * - ``DcIsoPauseResume``
     - t=0: ConfigureSession(DcIso2). t=45s: Pause. t=75s: Resume.
       t=180s: Stop. t=185s: Unplug.
   * - ``DcIsoBpt``
     - t=0: ConfigureSession(DcIsoD20, BptParams 50A/11kW discharge,
       30A target, 20% min SoC). t=180s: Stop. t=185s: Unplug.
   * - ``DcIsoMcs``
     - t=0: ConfigureSession(DcIsoD20, mcs_enabled=true). t=180s: Stop.
       t=185s: Unplug.
   * - ``DiodeFailSmoke``
     - t=0: ConfigureSession(AcIec, 32A, 3ph). t=15s: InjectFault
       (DiodeFail). t=20s: ClearFault.
   * - ``AcIecRampUp``
     - t=0: ConfigureSession(AcIec, 0A, 3ph, ChargingCurve
       [2s→8A/2s-ramp, 8s→16A/2s-ramp, 20s→32A/4s-ramp]). t=60s: Stop.
       t=65s: Unplug.
   * - ``DcIsoTaper``
     - t=0: ConfigureSession(DcIso2, ChargingCurve
       [0→100A, 30s→50A/10s-ramp, 60s→20A/10s-ramp, 90s→5A/5s-ramp]).
       t=120s: Stop. t=125s: Unplug.

The optional timing fields are ``pause_at_ms``, ``resume_at_ms``,
``stop_after_ms``, ``unplug_after_ms``, ``fault_at_ms`` and
``clear_fault_at_ms``. Values replace offsets from preset start. They must
be positive, apply to a phase that the preset contains, and preserve the
strict ordering of timed steps. Omitted fields retain their preset defaults.

``e2m/scenario_event`` carries ``name``, ``kind`` and optional ``message``.
Kinds are ``Started``, ``StepFired``, ``Finished`` and ``Rejected``.
An Accepted ``run_scenario`` command acknowledgement means the start was accepted
for processing; there is exactly one command acknowledgement publication attempt
per request, subject to the transport limit above.
The run may subsequently emit Rejected if configuration is refused, its
acknowledgement times out, or a world step fails. Rejected carries the reason
and cancels remaining steps. A failed step emits no StepFired, and a failed
run emits no Finished. Started means the car accepted the initial configuration
and the initial plug succeeded. A StepFired message names the successful world
step or published intent verb. Finished means all preset steps succeeded or
were dispatched as intents; it does not mean the car has finished charging.
For example, DcIsoBasic finishes immediately after configuring and plugging.
A rejected overlapping request leaves the original preset running.

Exceptions from scenario actions or event publication cancel the run. The driver
attempts to publish Rejected with the failed step, paired car and exception reason.
If rejection reporting also fails, it logs that reporting failure separately;
the run remains cancelled and MQTT command handling continues. A failed world
operation is explicitly reported as ``world state may be partially applied``:
a transport exception cannot establish which board or car effects arrived.
The driver's gate and fault latches retain their last local state, so
``world_state`` is not confirmation of delivery after such a failure. The driver
does not retry or roll back side effects automatically. Recovery must explicitly
re-establish board and car state before another preset; local idempotency guards
still apply to world commands.

ScenarioRunner
~~~~~~~~~~~~~~

``main/ScenarioRunner.{hpp,cpp}`` contains the preset table and
``TimingResolver``. The module's scenario timer thread waits for each
absolute deadline, including the configuration timeout, on its state monitor.
A configuration acknowledgement wakes the timer to recalculate its deadline.
Callbacks execute under the same monitor as MQTT world commands, maintaining the pilot and fault ownership
rules. Late callbacks dispatch all due steps in order. A car session ending
or unplugging does not reset the driver timeline. Charging curves remain in
EvController's CurvePlayer and end with the car's session.

Configuration
-------------

.. list-table:: Module configuration
   :header-rows: 1
   :widths: 35 15 50

   * - Key
     - Default
     - Meaning
   * - ``ev_controller_module_id``
     - ``ev_manager``
     - Controller id whose first heartbeat is required.
   * - ``plugged_at_startup``
     - ``false``
     - Start with the pilot passed.
   * - ``heartbeat_wait_s``
     - ``10``
     - Startup wait in seconds; zero checks immediately at readiness.
   * - ``cfg_heartbeat_interval_ms``
     - ``1000``
     - Driver heartbeat interval; zero disables it.
   * - ``cfg_communication_check_to_s``
     - ``0``
     - Bridge communication check timeout; zero disables it.

The driver subscribes to
``everest_api/1/ev_controller/<ev_controller_module_id>/e2m/heartbeat``.
If none arrives within the startup wait, it raises ``generic/CommunicationFault``
naming the controller id. The first heartbeat clears that startup error. This
startup check does not monitor subsequent controller heartbeat loss.

Tests
-----

Configure with ``BUILD_TESTING=ON``, build ``everest-core_EvDriverSim_tests``,
and run ``ctest -R EvDriverSim --output-on-failure``. The Catch2 cases carry
``[evdriver]`` and cover the pure pilot gate, electrical faults and framework
adapter boundary, all 12 presets, timing overrides, heartbeat and overlap
gates, scenario events and the real module timer, without starting a broker or a SIL configuration.
