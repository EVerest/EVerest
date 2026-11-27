.. _everest_modules_handwritten_EvseManager:

.. ************************
.. EvseManager
.. ************************

The module ``EvseManager`` is a central module that manages one EVSE
(i.e. one connector to charge a car).
It may control multiple physical connectors if they are not usable at the same
time and share one connector id,
but one EvseManager always shows as one connector in OCPP for example. So in
general each connector should have a dedicated EvseManager module loaded.

The EvseManager contains the high level charging logic (Basic charging and
HLC/SLAC interaction), collects all relevant data for the charging session
(e.g. energy delivered during this charging session) and provides control over
the charging port/session. For HLC it uses two helper protocol modules that it
controls (SLAC and ISO15118).

Protocol modules such as OCPP or other APIs use EvseManagers to control the
charging session and get all relevant data.

The following charge modes are supported:

* AC charging: Basic Charging according to IEC61851/SAE J1772 and HLC according
  to ISO15118-2
* DC charging: ISO15118-2 and DIN SPEC 70121

Additional features:

* Autocharge support (PnC coming soon)
* Seamlessly integrates into EVerest Energy Management
* The lowest level IEC61851 state machine can be run on a dedicated
  microcontroller for improved electrical safety
* Support for seperate AC and DC side metering in DC application

Typical connections
===================

TODO: AC and DC module graphs and description

AC Configuration
----------------

Captive cable mode
~~~~~~~~~~~~~~~~~~

For AC sockets with a connector lock in fleet or private installations, the
cable can be kept permanently locked in the socket as theft protection. Set
``keep_cable_locked`` to ``true`` to enable this captive cable mode. It requires
a board_support driver that publishes ``ac_pp_ampacity`` on every change, also
outside of charging sessions, so that plug presence is known even in CP state A.

With the option enabled:

* The connector locks whenever a plug is present, in every CP state including
  state A with no EV attached.
* The lock is latched: losing plug presence without a preceding force unlock
  (for example the Proximity Pilot contact opening while the cable is pulled
  against the lock pin) does not release it.
* The only way to release the cable is the ``force_unlock`` command (for
  example an OCPP ``UnlockConnector``) or disabling the option again. After a
  force unlock the connector stays unlocked until the cable is removed; the next
  plug insertion locks it again.
* It takes precedence over ``lock_connector_in_state_b`` and
  ``unlock_when_deauthorized``.
* The normal locking rules still apply on top. For example, after a force
  unlock with the cable left in the socket, the connector is locked again for
  the next charging session as usual, before the relays close.

Captive cable mode only applies to AC charging with a socket and a connected
connector lock. With ``charge_mode`` other than ``AC``, without a connector
lock, or when the board_support driver reports a fixed attached cable
(``IEC62196Type2Cable``), the option is ignored with a warning and setting it at
runtime is rejected. The connector type is only known once the board_support
capabilities arrive, so the mode activates then; a runtime change made before
that is accepted and, if the driver reports a fixed cable, ignored with a
warning.

``keep_cable_locked`` can be changed at runtime through the configuration
service and is applied immediately without a restart.

``keep_cable_locked_lock_delay_ms`` (default ``500``) sets the delay in
milliseconds between detecting a plug and engaging the lock. It gives the plug
time to seat fully before the lock pin extends, so a lock triggered on the first
Proximity Pilot contact cannot jam a half-inserted plug. If the plug is pulled
back out within the delay, no lock is engaged. Set it to ``0`` to lock
immediately. The timer checks every 500 ms, so the effective delay is rounded up
to the next multiple of 500 ms.

DC Configuration
----------------

In DC applications, the EvseManager still has an AC side that behaves similar
to a normal AC charger. The board_support module therefore still has to report
AC capabilities which refer to the AC input of the AC/DC power supply. If an AC
side RCD is used it also belongs to the board_support driver.
An AC side power meter can be connected and it will be used for Energy
management.

In addition, on the DC side the following hardware modules can be connected:

* A DC powermeter: This will be used for billing purposes if present.
  If not connected, billing will fall back to the AC side power meter.
* Isolation monitoring: This will be used to monitor isolation during
  CableCheck, PreCharge and CurrentDemand steps.
* DC power supply: This is the AC/DC converter that actually charges the car.

Software over-voltage supervision is always active during DC charging. The configuration option
``internal_over_voltage_duration_ms`` defines for how long the measured DC voltage
must exceed the negotiated limit before EvseManager raises ``MREC5OverVoltage``.
Set it to ``0`` to trigger immediately once the threshold is crossed.

Welding detection voltage
~~~~~~~~~~~~~~~~~~~~~~~~~

During welding detection the EV checks its contactors against the voltage the EVSE reports in
``WeldingDetectionRes``. That has to be the side B voltage at the vehicle connector, measured with the
EVSE contactors open, within 10 V (IEC 61851-23:2023 CC.6.3, test CC.7.6.17). A power supply measuring
at its own output, behind the contactors, does not see it. The same holds for any meter or monitor
wired between the power supply and the contactors: choose the source that is physically connected on
the EV side of the EVSE output contactors, otherwise the EV is told the power supply output instead of
the voltage at its connector.

On the DC stop request the ISO module publishes ``dc_open_contactor``: EvseManager switches the DC
power supply off and withdraws the power permissive, and the board support reports the open contactor
(``PowerOff``), which EvseManager forwards as ``contactor_closed(false)``. The ISO module holds the
PowerDeliveryRes until then, so that side B is disabled before the EV learns of it (IEC 61851-23:2023
Table CC.9, t103 before t104), for at most 3 s on ISO 15118-2 and 1.4 s on DIN SPEC 70121 and
ISO 15118-20; without the report it sends the response after that and logs a warning.

The ISO module reports the phase with ``welding_detection``: ``Started`` at PowerDelivery(Stop),
``Finished`` at SessionStop or session teardown. In between, EvseManager reports the voltage of the
source selected by ``ev_side_voltage_source`` instead of the power supply measurement:

* ``powermeter_car_side`` (default): ``voltage_V.DC`` of the car side power meter.
* ``over_voltage_monitor``: ``voltage_measurement_V`` of the over voltage monitor.
* ``isolation_monitor``: ``voltage_V`` of the isolation monitor.
* ``power_supply``: the power supply measurement, as outside welding detection.

If the selected source is not connected, the next connected one in the order ``powermeter_car_side``,
``over_voltage_monitor``, ``power_supply`` is used and a warning is printed at start up. The isolation
monitor is only used when configured. The power supply is always available.

The over voltage monitor and the isolation monitor are normally stopped when the power path opens.
If one of them is the source, that stop is deferred until the welding detection phase ends, and
isolation faults are not evaluated meanwhile. ``Started`` arrives on its own topic and may follow the
power path events; if no phase has started 2 s after they deferred the stop, the source is stopped. Note that IEC 61851-23:2023 Table CC.9 t103 asks for the
EVSE IMD to be disabled when side B is disabled, and the EV may enable its own IMD (t104): use
``isolation_monitor`` only with an IMD that can measure the voltage without its insulation test.

The ISO module holds every ``WeldingDetectionRes`` until two samples of the selected source arrived
after its request, for at most 1.4 s (1.5 s performance time), so that the reported voltage was
measured after the request: the EV opens its contactors only after the PowerDeliveryRes and may switch
them between two requests (CC.6.3, Table CC.9 t105 to t106). Two samples because the first one after
the request can still have been taken just before it, a polled meter delivers a sample up to one poll
interval after taking it. With one sample at the deadline the response carries it (logged as info),
with none it carries the last present voltage (warning). Every publication of the source must be a new
measurement, so a driver that polls a meter must not republish an unchanged sample (LemDCBM400600
does not). The source should publish at 5 Hz or more (CC.6.3: at most 200 ms from measurement to
communication); EvseManager warns once per session if it updates less often than every second. A
meter that refreshes once per second, as the LEM DCBM 400/600 does whatever its poll interval, cannot
meet that, and its value in the response can be up to about a second old. If ``Finished`` never
arrives, the phase also ends on D-LINK events, a failed HLC session, the next cable check or charge
loop, unplug, or 20 s after it started (CC.9 t104 to t107).

An EVSE-initiated stop ends the powermeter transaction as soon as the contactors are open. Stopping a
transaction blocks the LEM DCBM 400/600 for seconds while it signs the OCMF record, during which it
publishes no voltage, so EvseManager delays the ``stop_transaction`` until the welding detection phase
has ended (or the EV unplugged). An EV-initiated stop keeps the transaction open until unplug anyway.

Published variables
===================

session_events
--------------

EvseManager publishes the session_events variable whenever an event happens.
It does not publish its internal state but merely events that happen that can
be used to drive an state machine within another module.

Example: Write a simple module that lights up an LED if the evse is reserved.
This module requires an EvseManager and subscribes to the session_events
variable. Internally it has only two states: Reserved (LED on), NotReserved
(LED off).

The state machine transitions are driven by the two events from EvseManager:
ReservationStart and ReservationEnd.

All other events are ignored in this module as they are not needed.

powermeter
----------

EvseManager republishes the power meter struct that if it has a powermeter
connected. This struct should be used for OCPP and display purposes. It comes
from the power meter that can be used for billing (DC side on DC, AC side on
AC). If no powermeter is connected EvseManager will never publish this
variable.

In DC charge mode, the car side power meter may publish its ``capabilities``
(minimum measurable currents, e.g. due to calibration law accuracy limits).
These minimum currents are merged into the DC limits advertised to the EV over
ISO 15118 (ChargeParameterDiscovery and mid-session limit updates). They do not
affect cable check or precharge.

During current demand, an EV target current below the minimum current offered
to the EV in ChargeParameterDiscovery (the nominal power supply minimum if
reported, else the power supply minimum, raised to the power meter minimum) is
set to 0 A on the power supply, as required by IEC 61851-23:2023 CC.5.5.7. The
EV sees that minimum truncated to the resolution of the HLC encoding, so a
target up to 0.1 A below it counts as the minimum and gets it. The
EV target is compared before EVSE-side maximum limits are applied to it, and
only from the first target the EV sends during current demand on: the precharge
target is not compared. The HLC module marks each target with its origin
(``current_demand`` in ``DcEvTargetValues``), so this does not depend on the
order in which ``current_demand_started`` and the target are delivered. An
energy management limit below the offered minimum gives 0 A as well, and the EV
is sent 0 A and 0 W as EVSE maximum limits. The EVSE maximum power limit counts
at the present voltage: when it allows less than the offered minimum there, 0 A
is delivered. The ramp to 0 A before an ISO 15118-20 pause counts as such a
limit. For
an EV target at or above that minimum, the current ramp starts at the minimum
and the current set on the power supply is never below the power supply
minimum.
Outside current demand, currents below the power supply minimum are raised to
that minimum.

Power supply capabilities, external derating and power meter capabilities
published during current demand are sent to the EV as updated limits and
applied to the power supply setpoint within the next charger state machine
iteration, whether or not the EV lowers its request to the new limits.


Charging State Machine
======================

.. mermaid::

   stateDiagram-v2
       direction TB

       [*] --> Idle

       %% Enable / Disable
       Idle --> Disabled : disable
       Disabled --> Idle : enable

       %% Happy path
       Idle --> WaitingForAuthentication : EV plugged in
       WaitingForAuthentication --> PrepareCharging : Authorized
       PrepareCharging --> Charging : Contactor close allowed
       Charging --> StoppingCharging : Stop condition
       StoppingCharging --> Finished : Transaction end
       Finished --> Idle : EV unplugged

       %% Early exit / Errors
       WaitingForAuthentication --> Finished : Fatal error, stop requested, or EV unplugged
       PrepareCharging --> StoppingCharging : Fatal error, deauth, or EV unplugged

       %% Pauses
       Charging --> ChargingPausedEV : EV moves to state B
       ChargingPausedEV --> PrepareCharging : Power requested (BCB/Car)
       ChargingPausedEV --> ChargingPausedEVSE : No power (AC BASIC)
       ChargingPausedEV --> StoppingCharging : Deauth or unplugged
       
       ChargingPausedEVSE --> PrepareCharging : Power available / Errors cleared
       ChargingPausedEVSE --> StoppingCharging : Deauth or unplugged

       %% Post-Stop logic
       StoppingCharging --> ChargingPausedEV : EV-initiated pause

State Transitions
-----------------

**Basic Flow**

* ``Idle`` -> ``WaitingForAuthentication``: EV plugged in.
* ``WaitingForAuthentication`` -> ``PrepareCharging``: Authorized by EIM or PnC.
* ``WaitingForAuthentication`` -> ``Finished``: Fatal error, stop requested (e.g. via
  ``request_stop_transaction`` or ``disable``), or EV unplugged.
* ``PrepareCharging`` -> ``Charging``: Contactor close allowed. With a nominal duty cycle and a
  SLAC matching in progress, state C closes the contactor only once it lasted 1 s or the matching ended:
  the EV toggles B-C-B to validate the matching (CM_VALIDATE), and a toggle is no charge request.
* ``Charging`` -> ``StoppingCharging``: Triggered by any **Stop Condition** (see below).
* ``StoppingCharging`` -> ``Finished``: No transaction, EV unplugged, or not authorized.
* ``Finished`` -> ``Idle``: EV unplugged.

**Pause & Resume**

* ``ChargingPausedEV`` -> ``PrepareCharging``: BCB toggle or ``CarRequestedPower``.
* ``ChargingPausedEV`` -> ``ChargingPausedEVSE``: No power available (AC BASIC only).
* ``ChargingPausedEVSE`` -> ``PrepareCharging``: Power available, no EVSE pause and errors cleared.
* ``StoppingCharging`` -> ``ChargingPausedEV``: EV-initiated pause after stop sequence.
* ``ChargingPausedEV`` / ``ChargingPausedEVSE`` -> ``T_step_X1`` -> ``T_step_EF`` -> same pause state: an
  established data link was lost while basic AC charging at a nominal duty cycle is paused ([V2G3-M07-05]).

**ISO 15118-20 EVSE pause**

An EVSE pause of an ISO 15118-20 session follows the control mode the EV selected:

* Dynamic control mode: the SECC may only ask for a pause at 0 kW ([V2G20-2115]).
  ``StoppingCharging`` ramps the DC setpoint down at 100 A/s while the charge loop
  continues, requests the pause once the measured output current is below 1 A and keeps
  the setpoint at 0 A until the EV has paused. A ramp that does not reach 0 A within
  35 s ends in the hard stop.
* Scheduled control mode: a pause may only be notified while the applied entry of the
  EV's power profile is 0 kW ([V2G20-1198]). The pause is requested from ``Charging``,
  the HLC stack holds the notification back and publishes ``pause_notified`` once it
  has gone out; charging continues until then. A resume in between withdraws the
  request.

Once notified the EV has ``NotificationMaxDelay``, fixed at 60 s ([V2G20-1850]), to
pause; ``StoppingCharging`` waits 65 s before the hard stop.

**Stop Conditions**

The transition ``Charging`` -> ``StoppingCharging`` occurs if:
    * Fatal error
    * Deauthorization
    * EVSE pause requested (ISO 15118-20 scheduled control mode: once notified to the EV)
    * EV unplugged
    * IEC contactor opened
    * No power available (Immediate for AC BASIC; timeout for HLC).

.. note::

   Transient helper states (``T_step_EF``, ``T_step_X1``, and ``SwitchPhases``) are omitted 
   for readability. Transitions passing through these are shown as direct arrows.

Session Events
--------------

These are the ``SessionEvent`` values published by the EvseManager with respect to the state machine
states and transitions.

.. list-table::
   :header-rows: 1
   :widths: 35 30 35

   * - State / Transition
     - Event
     - Trigger
   * - ``Disabled``
     - ``Disabled``
     - On entry
   * - ``Idle``
     - ``Enabled``
     - When EVSE is enabled via ``enable_disable()``
   * - ``WaitingForAuthentication``
     - ``SessionStarted``
     - On entry (new session)
   * - ``WaitingForAuthentication``
     - ``AuthRequired``
     - On entry
   * - ``WaitingForAuthentication``
     - ``Authorized``
     - When ``authorize()`` is called externally
   * - ``WaitingForAuthentication``
     - ``TransactionStarted``
     - After auth is accepted, just before leaving state
   * - ``PrepareCharging``
     - ``PrepareCharging``
     - On entry
   * - ``Charging``
     - ``ChargingStarted``
     - On entry
   * - ``ChargingPausedEV``
     - ``ChargingPausedEV``
     - On entry
   * - ``ChargingPausedEVSE``
     - ``ChargingPausedEVSE``
     - When the set of pause reasons changes
   * - ``StoppingCharging``
     - ``StoppingCharging``
     - On entry
   * - ``Finished``
     - ``ChargingFinished``
     - On entry (if transaction was active)
   * - ``Finished``
     - ``TransactionFinished``
     - On entry (if transaction was active)
   * - ``Finished`` -> ``Idle``
     - ``SessionFinished``
     - When EV unplugs and ``stop_session()`` is called
   * - Startup
     - ``SessionResumed``
     - A transaction of the previous run was found in storage, see
       `Resuming transactions after a restart`_
   * - ``WaitingForAuthentication``
     - ``TransactionResumed``
     - On entry, instead of ``SessionStarted``, ``AuthRequired`` and ``TransactionStarted``,
       when the EV of a resumed transaction is plugged in

Resuming transactions after a restart
=====================================

While a transaction is active, EvseManager keeps a record of it in the ``kvs`` connected as ``store``:
the session ID, the id token, the start time, the signed and unsigned start meter values, the boot ID of the kernel (``/proc/sys/kernel/random/boot_id``), a last-alive time and
whether EvseManager shut down cleanly. Each EvseManager keeps its own record under the key
``<module id>_session``, so every EVSE resumes or ends its transaction independently. Without a
``store`` connection nothing is recorded and nothing can be resumed.

At startup, EvseManager publishes ``SessionResumed`` with the stored session ID and then decides
whether to continue the transaction. By default (``resume_transaction_on_startup: Never``) it ends
the transaction: it stops the power meter transaction and publishes ``TransactionFinished`` with
reason ``PowerLoss`` and the stored session ID.

``resume_transaction_on_startup`` is a comma-separated list of the situations in which the
transaction is continued instead. The situation is derived from the boot ID and the
clean-shutdown mark of the stored record:

.. list-table::
   :header-rows: 1
   :widths: 15 45 40

   * - Value
     - Situation
     - Detected by
   * - ``Never``
     - Never resume. Must be the only entry.
     -
   * - ``Always``
     - Every situation below, and also when the situation cannot be determined because a boot ID
       is missing. Must be the only entry.
     -
   * - ``PowerLoss``
     - The system rebooted without a clean shutdown: power cut, kernel panic, hardware watchdog.
     - Boot ID changed, no clean shutdown
   * - ``Crash``
     - EVerest stopped without a clean shutdown while the OS kept running: crash, ``kill -9``,
       software watchdog.
     - Same boot ID, no clean shutdown
   * - ``Restart``
     - EVerest was stopped cleanly and restarted while the OS kept running, for example
       ``systemctl restart``.
     - Same boot ID, clean shutdown
   * - ``Reboot``
     - The OS was rebooted after EVerest was stopped cleanly.
     - Boot ID changed, clean shutdown

Examples: ``PowerLoss``, ``PowerLoss,Crash``, ``Always``. Whitespace around entries is ignored and
the values are case-sensitive. An unknown entry, an empty value, or ``Never`` or ``Always``
combined with other entries is logged as an error and treated as ``Never``.

EvseManager marks the record in its ``shutdown()`` handler. The manager only calls it with
``--graceful-shutdown`` (see the manager lifecycle explanation); by default it terminates the
modules on SIGINT or SIGTERM, so every stop is detected as ``Crash`` (OS kept running) or
``PowerLoss`` (OS rebooted), and ``Restart`` and ``Reboot`` never apply. The mark is also lost if
the ``kvs`` provider has already shut down when EvseManager writes it.

The record is written to ``kvs`` by a thread of its own, so that a slow ``kvs`` provider does not
hold up the charging state machine; only the latest version of the record is written.

In addition, a transaction is only resumed if

* the record contains an id token (records written by earlier versions contain the session ID
  only and are always ended), and
* the outage, the time from the stored last-alive time to the startup, is at most
  ``resume_transaction_max_outage_s``, 300 s by default (0 disables this check). A longer outage
  ends the transaction. If the system clock is earlier than
  the last-alive time, for example on a system without RTC before time synchronisation, the
  outage is treated as unknown and the transaction is not resumed. On such a system set
  ``resume_transaction_max_outage_s`` to 0, or make sure the time is synchronised before
  EvseManager starts. The last-alive time is
  written when the transaction starts or is resumed and then every
  ``resume_transaction_alive_interval_s`` (0 disables the periodic writes).

When the transaction is to be resumed, the power meter transaction stays open and EvseManager
waits up to ``resume_transaction_ev_detect_timeout_ms``, counted from the start of the state
machine, for an EV:

* EV plugged in: the session continues with the stored session ID, id token and signed start
  meter value. EvseManager publishes ``TransactionResumed`` followed by ``Authorized``, but
  neither ``SessionStarted``, ``AuthRequired`` nor ``TransactionStarted``, so consumers do not
  open a second transaction. ``TransactionResumed`` carries ``transaction_started`` with the id
  token, the signed start meter value and the meter value at the original start of the
  transaction. The session then continues like an authorised plug-in: ISO 15118 sessions
  restart with SLAC and a new V2G session (including cable check and pre-charge for DC), basic
  charging restarts the PWM. The transaction ends through the usual paths, with the original
  session ID.
* No EV in time, or the connector is disabled: the transaction ends as above with reason
  ``PowerLoss``.

Authorisations received while waiting for the EV are ignored.

Limitations:

* The ``powermeter`` interface cannot report whether the meter still has the transaction open. A
  meter that lost it during the outage reports an error when the transaction is stopped, and the
  transaction may end without a signed stop meter value. Installations under calibration law
  (for example Eichrecht) must check whether continuing a transaction across a power loss is
  permitted with their meter before enabling this feature.
* Not restored: a pause (by the user, the CSMS or the EVSE), so charging starts again on
  resumption; the session log; tariff and validation messages of the authorisation; the
  reservation ID.
* ISO 15118 needs a new SLAC and V2G session. An EV that ended its session when the EVSE went
  dark may refuse it; the transaction then ends through the usual paths. The new session offers
  external payment only, so a transaction started by Plug and Charge continues as authorised by
  external identification (EIM). Resuming with real EVs has not been tested.
* The CSMS gets no explicit notification that the transaction continues, except with OCPP 2.1
  and ``TxCtrlr.ResumptionTimeout`` set (trigger reason ``TxResumed``). OCPP 1.6 reports the
  connector as ``Preparing`` again; OCPP 2.0.1 sends a **TransactionEvent.req** only if the
  charging state changes.
* When a transaction is resumed, other open power meter transactions are not closed at startup.
* Consumers of ``session_events`` see ``TransactionResumed`` instead of ``SessionStarted`` and
  ``TransactionStarted``. The Auth module treats the EVSE as occupied by the transaction, and the
  token that started it stops it. OCPP 1.6 keeps the transaction open at startup and reports the
  connector as ``Preparing`` again. OCPP 2.x continues the transaction only with
  ``InternalCtrlr.ResumeTransactionsOnBoot`` set to ``true`` or ``TxCtrlr.ResumptionTimeout``
  greater than zero, see the OCPPmulti documentation.
* The timestamp of ``TransactionResumed`` is the start time of the original transaction. The
  ``evse_manager_consumer_API`` reports it as the transaction start; RpcApi and the API module
  report the time of the resumption. All of them count the energy from the original start.

Resuming transactions after a reset
-----------------------------------

A reset that keeps the transactions, such as the OCPP 2.1 reset type ``ImmediateAndResume``, calls
the command ``prepare_resume_after_reset`` before the system restarts. EvseManager then pauses
energy transfer (as ``pause_charging`` does) and marks the stored record. At the next startup a
marked transaction is resumed whatever ``resume_transaction_on_startup`` and
``resume_transaction_max_outage_s`` say, as OCPP 2.1 B13.FR.23 requires; only the id token must be
stored. With ``allow_energy_transfer`` set to ``false`` the resumed transaction starts paused by
the EVSE, so no energy is transferred until ``resume_charging`` is called (OCPP 2.1
``TxCtrlr.AllowEnergyTransferResumption``). An EVSE pause that is set before charging starts moves
the session from ``PrepareCharging`` straight to ``ChargingPausedEVSE``, so no contactor closes and
no DC power is switched on. Without an ISO 15118 data link the PWM is switched off (X1); in an
ISO 15118 session that is already under way the EV is told first (``stop_charging``, or
``pause_charging`` with ISO 15118-20) and the PWM stays on so that the EV ends the session in
order. The EV must still be plugged in within
``resume_transaction_ev_detect_timeout_ms``; if it is not, the transaction ends with reason
``EVDisconnected`` instead of ``PowerLoss``.

The command returns ``false`` if a transaction is active but cannot be kept, which is the case
without a ``store`` connection; OCPPmulti then stops that transaction instead. It returns ``true``
if no transaction is active. A transaction that is still waiting for its EV after a restart is
kept as well, and a transaction resumed after a reset with resumption stays marked until its EV
is back, so a further restart in between does not lose it. The connector stays locked until the
restart: the cable is not unlocked before the reset.

If the reset does not take place after all, ``cancel_resume_after_reset`` undoes the command: the
mark is removed from the stored record and the EVSE pause is lifted, unless the transaction was
already paused by the EVSE before the first ``prepare_resume_after_reset``.

A resumed transaction is already authorised, so a new ISO 15118 session offers only external
payment, as for a session that started authorised; a Plug and Charge transaction continues as EIM.

Authentication
==============

The Auth modules validates tokens and assignes tokens to EvseManagers, see Auth
documentation. It will call ``Authorize(id_tag, pnc)`` on EvseManager to
indicated that the EvseManager may start the charging session.
Auth module may revoke authorization (``withdraw_authorization`` command) if
the charging session has not begun yet (typically due to timeout), but not once
charging has started.


Autocharge / PnC
----------------

Autocharge is fully supported, PnC support is coming soon and will use the same
logic. The car itself is a token provider that can provide an auth token to be
validated by the Auth system (see Auth documentation for more details).
EvseManager provides a ``token_provider`` interface for that purpose.

If external identification (EIM) is used in HLC (no PnC) then Autocharge is
enabled by connecting the ``token_provider`` interface to Auth module. When the
car sends its EVCCID in the HLC protocol it is converted to Autocharge format
and published as Auth token. It is based on the following specification:

https://github.com/openfastchargingalliance/openfastchargingalliance/blob/master/autocharge-final.pdf

To enable PnC the config option ``payment_enable_contract`` must be set to
true. If the car selects Contract instead of EIM PnC will be used instead of
Autocharge.

Some EVs use PnC for the authorization only and never enter the HLC charging
loop: they close the TCP connection or send a SessionStop afterwards and expect
basic charging. EvseManager keeps the 5 % duty cycle (or X1 after an X1 start)
and the session until the data link ends, which the ISO 15118 stack bounds with
``V2G_SECC_Sequence_Timeout``. The end of the link then switches to the nominal
duty cycle with the authorization kept: through X1 and E/F for a lost link
(D-LINK_ERROR, [V2G3-M07-04], [V2G3-M07-05]), through E/F for a
SessionStop(Terminate).
In X1 without a data link, e.g. once the matching retries are exhausted, a PnC
authorization switches to the nominal duty cycle directly.

Reservation
-----------

Reservation handling logic is implemented in the Auth module. If the Auth
module wants to reserve a specific EvseManager (or cancel the reservation) it
needs to call the reserve/cancel_reservation commands. EvseManager does not
check reservation id against the token id when it should start charging, this
must be handled in Auth module. EvseManager only needs to know whether it is
reserved or not to emit an ReservatonStart/ReservationEnd event to notify other
modules such as OCPP and API or e.g. switch on a specific LED signal on the
charging port. For a reservation made for this specific EVSE, Auth passes the
reservation details (id, id token, parent id token and expiry time) with the
reserve command, and the ReservationStart event carries them.

Energy Management
=================

EvseManager seamlessly intergrates into the EVerest Energy Management.
For further details refer to the documentation of the EnergyManager module.

EvseManager has a grid facing Energy interface which the energy tree uses to
provide energy for the charging sessions. New energy needs to be provided on
regular intervals (with a timeout).

If the supplied energy limits time out, EvseManager will stop charging.
This prevents e.g. overload conditions when the network connection drops
between the energy tree and EvseManager.

EvseManager will send out its wishes at regular intervals: It sends a
requested energy schedule into the energy tree that is merged from hardware
capabilities (as reported by board_support module), EvseManager module
configuration settings
(max_current, three_phases) and external limts (via ``set_external_limits``
command) e.g. set by OCPP module.

Note that the ``set_external_limits`` should not be used by multiple modules,
as the last one always wins. If you have multiple sources of exernal limits
that you want to combine, add extra EnergyNode modules in the chain and
feed in limits via those.

The combined schedule sent to the energy tree is the minimum of all energy
limits.

After traversing the energy tree the EnergyManager will use this information
to assign limits (and a schedule)
for this EvseManager and will call enforce_limits on the energy interface.
These values will then be used
to configure PWM/DC power supplies to actually charge the car and must not
be confused with the original wishes that
were sent to the energy tree.

The EvseManager will never assign energy to itself, it always requests energy
from the energy manager and only charges
if the energy manager responds with an assignment.

Limits in the energy object can be specified in ampere (per phase) and/or watt.
Currently watt limits are unsupported, but it should behave according to that
logic:

If both are specified also both limits will be applied, whichever is lower.
With DC charging, ampere limits apply
to the AC side and watt limits apply to both AC and DC side.

Energy Management: 1ph/3ph switching
====================================

EVerest has support for switching between 1ph and 3ph configurations during AC
charging (e.g. for solar charging when sometimes charging with less then 4.2kW (6A*230V*3ph)
if desired).

Be warned: Some vehicles (such as first generation of Renault Zoe) may be permanently
damaged when switching from 1ph to 3ph during charging. Use at your own risk!

To use this feature several things need to be enabled:

- In EvseManager, adjust two config options to your needs: ``switch_3ph1ph_delay_s``, ``switch_3ph1ph_cp_state``
- In the BSP driver, set ``supports_changing_phases_during_charging`` to true in the reported capabilities.
  If your bsp hardware detects e.g. the Zoe, you can set that flag to false and publish updated capabilities any time.
- BSP driver capabilities: Also make sure that minimum phases are set to one and maximum phases to 3
- BSP driver: make sure the ``ac_switch_three_phases_while_charging`` command is correctly implemented
- EnergyManager: Adjust ``switch_3ph1ph_while_charging_mode``, ``switch_3ph1ph_max_nr_of_switches_per_session``,
  ``switch_3ph1ph_switch_limit_stickyness``, ``switch_3ph1ph_power_hysteresis_W``, ``switch_3ph1ph_time_hysteresis_s``
  config options to your needs

If all of this is properly set up, the EnergyManager will drive the 1ph/3ph switching. In order to do so,
it needs an (external) limit to be set. There are two options: The external limit can be in Watt (not in Ampere),
even though we are AC charging. This is the preferred option as it gives the freedom to the EnergyManager to
decide when to switch. The limit can come from OCPP schedule or e.g. via an additional EnergyNode.

The second option is to set a limit in Ampere and set a limitation on the number of phases (e.g. min_phase=1, max_phase=1).
This will enforce switching and can be used to decide the switching time externally. EnergyManager does not have the
freedom to make the choice in this case.

Take care especially with the power(watt) and time based hysteresis settings. They should be adjusted to the
actual use case to avoid relays wearing due too a lot of switching cycles. Consider also to limit the maximum
number of switching cycles per charging session.

DER (grid support) advertising
==============================

EvseManager exposes a ``set_der_available`` command that records, per EVSE, whether DER directive support
(a ``grid_support`` provider) is wired for that EVSE. This is a boot-time fact asserted by whichever module
provides the ``grid_support`` connection (the OCPP module is one such provider, asserting it from the presence
of a ``grid_support`` connection); the EV's runtime DER capability is never sent here.

Whether DER is available and which ISO 15118-20 AC DER annex the EVSE speaks are separate axes. The annex is a
static per-EVSE choice made with the ``iso15118_der_flavor`` config option: ``NONE`` (the default, no AC DER
advertised), ``IEC`` (Annex L, ``AC_DER_IEC``) or ``SAE`` (Annex M, ``AC_DER_SAE``). The two annexes are
mutually exclusive per EVSE, so AC DER is opt-in even where DER availability is asserted.

When DER is available, the EVSE is export-capable (its hardware capabilities report a non-zero export current
and at least one export phase) and a flavor is configured, EvseManager folds that one AC DER energy transfer
mode into the set it advertises, never both. This is in addition to ``AC_BPT`` (advertised whenever
``supported_iso_ac_bpt`` is set and the EVSE is export-capable). ISO 15118-20 has no combined ``AC_BPT_DER``
service category, so "AC_BPT_DER supported" is conveyed by advertising both ``AC_BPT`` and the configured AC
DER mode as separate energy transfer modes; the EV selects one per session.

The flavor is matched exactly. An unrecognized value advertises no AC DER and logs a warning naming it, a
branch normally unreachable because the manifest enum is validated at config load.

Configuring ``SAE`` is necessary but not sufficient. The :ref:`Evse15118D20 <everest_modules_Evse15118D20>`
module withholds the SAE DER limits until AC parameters carrying a positive nominal frequency and a positive
nominal voltage arrive, and the library then strips ``AC_DER_SAE`` from the advertised services. Neither value
has a fallback. It is self-correcting, since the limits are derived again on every change to the energy
services, the AC limits or the AC parameters, so an operator configuring ``SAE`` may briefly see no DER
service advertised.

The command returns ``NoHlc`` when no HLC is enabled for the EVSE (and does nothing), and ``Accepted`` otherwise.

Nominal grid frequency
----------------------

``ac_nominal_frequency`` (default ``50``) is passed to the HLC stack on every AC parameters update.
Informational for plain AC and ``AC_BPT``. For AC DER it is advertised at ChargeParameterDiscovery as the
mandatory ``GridNominalFrequency`` the EV adopts as the frequency of the grid it is connecting to, so it must
match the installation.

It is not a denormalization base. The frequency-trip curves and the enter-service band are in absolute Hz and
are not scaled by it, unlike the voltage curves, which are a percentage of the nominal voltage. Changing it
from ``50`` to ``60`` moves no frequency threshold; the default grid code carries 50 Hz-family constants, so a
60 Hz operator supplying a real setup config must supply 60 Hz curve values with it.

It is also load-bearing: the SAE DER limits are only derived once a positive nominal frequency and a positive
nominal voltage have arrived, as described above.

CP state in packet captures
===========================

``debug_emit_cp_state_hpav_frames`` is a debugging aid, off by default. When set to ``true``, the
EvseManager sends a HomePlug AV ``STP_CPSTATE.IND`` vendor MME (ethertype 0x88E1, ST/IoTecha OUI
00:80:E1) on ``debug_cp_state_hpav_device`` each time the CP state reported by the board support
module or the PWM duty cycle commanded to it changes. Wireshark decodes the frame natively as
``CP State Change: B, 5%``. The dsV2Gshark plugin additionally derives the X1/X2 sub-state and the
AC current limit from it and plots the CP state in its I/O graph. Capturing on the PLC modem
interface therefore shows CP transitions interleaved with the SLAC and ISO 15118 traffic.

The frame carries the CP state, the duty cycle in percent (100 while no PWM is generated, i.e. in
X1 and in states E and F), 1000 Hz while PWM is active and a nominal CP voltage for the state
(12, 9, 6 and 3 V for A to D, 0 V for E and F). It is sent unicast from the interface's own MAC address to the local
modem address ``00:b0:52:00:00:01``, the destination used by the dSPACE reference captures shipped
with dsV2Gshark.

Sending raw Ethernet frames requires the ``CAP_NET_RAW`` capability. Without it the module logs a
warning at startup and charges normally, but no frames are sent. Grant it in the configuration:

.. code-block:: yaml

   evse_manager:
     module: EvseManager
     capabilities:
       - CAP_NET_RAW
     config_module:
       debug_emit_cp_state_hpav_frames: true
       debug_cp_state_hpav_device: eth1

Do not enable this option in production.

Error Handling
==============

The control flow of this module is influenced by the errors of its requirements and by its own errors. An error that blocks
charging stops a running charging session and prevents new sessions from being started. While at least one blocking error is
active, the EvseManager is Inoperative and raises ``evse_manager/Inoperative``. Charging is possible again, and
``evse_manager/Inoperative`` is cleared, when the last blocking error is cleared.

The EvseManager subscribes to all errors of the following requirements and re-evaluates the Inoperative state on every raise
and clear. It does the same whenever it raises or clears one of its own ``evse_manager/*`` errors.

* ``bsp`` (``evse_board_support``)
* ``hlc`` (``ISO15118_charger``)
* ``connector_lock`` (``connector_lock``)
* ``ac_rcd`` (``ac_rcd``)
* ``imd`` (``isolation_monitor``)
* ``powersupply_DC`` (``power_supply_DC``)
* the billing powermeter (``powermeter``), only if the config option ``fail_on_powermeter_errors`` is ``true``
* ``slac`` (``slac``)
* ``over_voltage_monitor`` (``over_voltage_monitor``)

Every active error from these sources blocks charging unless its type is listed as non-blocking in the tables below. The
decision depends only on the error type: neither the severity of an error nor the combination of active errors changes it.
The severity only selects the kind of shutdown, see below. Errors raised by modules that are not requirements of the
EvseManager, and errors of requirements not listed above, never make the EvseManager Inoperative.

The following tables list every error type each source can raise and whether it blocks charging.

evse_manager
------------

Errors raised by the EvseManager itself.

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``evse_manager/Internal``
     - yes
   * - ``evse_manager/MREC4OverCurrentFailure``
     - yes
   * - ``evse_manager/MREC5OverVoltage``
     - yes
   * - ``evse_manager/MREC9AuthorizationTimeout``
     - yes
   * - ``evse_manager/PowermeterTransactionStartFailed``
     - yes
   * - ``evse_manager/Inoperative``
     - no
   * - ``evse_manager/MREC22ResistanceFault``
     - yes
   * - ``evse_manager/MREC11CableCheckFault``
     - yes
   * - ``evse_manager/VoltagePlausibilityFault``
     - yes

``evse_manager/Inoperative`` is the result of the evaluation, not a cause.

evse_board_support
------------------

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``evse_board_support/DiodeFault``
     - yes
   * - ``evse_board_support/VentilationNotAvailable``
     - yes
   * - ``evse_board_support/BrownOut``
     - yes
   * - ``evse_board_support/EnergyManagement``
     - yes
   * - ``evse_board_support/PermanentFault``
     - yes
   * - ``evse_board_support/MREC2GroundFailure``
     - yes
   * - ``evse_board_support/MREC3HighTemperature``
     - no
   * - ``evse_board_support/MREC4OverCurrentFailure``
     - yes
   * - ``evse_board_support/MREC5OverVoltage``
     - yes
   * - ``evse_board_support/MREC6UnderVoltage``
     - yes
   * - ``evse_board_support/MREC8EmergencyStop``
     - yes
   * - ``evse_board_support/MREC10InvalidVehicleMode``
     - yes
   * - ``evse_board_support/MREC14PilotFault``
     - yes
   * - ``evse_board_support/MREC15PowerLoss``
     - yes
   * - ``evse_board_support/MREC17EVSEContactorFault``
     - yes
   * - ``evse_board_support/MREC18CableOverTempDerate``
     - no
   * - ``evse_board_support/MREC19CableOverTempStop``
     - yes
   * - ``evse_board_support/MREC20PartialInsertion``
     - yes
   * - ``evse_board_support/MREC23ProximityFault``
     - yes
   * - ``evse_board_support/MREC24ConnectorVoltageHigh``
     - yes
   * - ``evse_board_support/MREC25BrokenLatch``
     - yes
   * - ``evse_board_support/MREC26CutCable``
     - yes
   * - ``evse_board_support/TiltDetected``
     - yes
   * - ``evse_board_support/WaterIngressDetected``
     - yes
   * - ``evse_board_support/EnclosureOpen``
     - yes
   * - ``evse_board_support/VendorError``
     - yes
   * - ``evse_board_support/VendorWarning``
     - no
   * - ``evse_board_support/CommunicationFault``
     - yes

The ``evse_board_support`` interface can also raise the ``ac_rcd`` error types. Raised through the ``bsp`` requirement, they
block charging exactly as they do when raised through the ``ac_rcd`` requirement:

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``ac_rcd/Selftest``
     - yes
   * - ``ac_rcd/DC``
     - yes
   * - ``ac_rcd/AC``
     - yes
   * - ``ac_rcd/MREC2GroundFailure``
     - yes
   * - ``ac_rcd/VendorError``
     - yes
   * - ``ac_rcd/VendorWarning``
     - no

connector_lock
--------------

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``connector_lock/ConnectorLockCapNotCharged``
     - yes
   * - ``connector_lock/ConnectorLockUnexpectedOpen``
     - yes
   * - ``connector_lock/ConnectorLockUnexpectedClose``
     - yes
   * - ``connector_lock/ConnectorLockFailedLock``
     - yes
   * - ``connector_lock/ConnectorLockFailedUnlock``
     - yes
   * - ``connector_lock/MREC1ConnectorLockFailure``
     - yes
   * - ``connector_lock/VendorError``
     - yes
   * - ``connector_lock/VendorWarning``
     - no

ac_rcd
------

Errors raised through the ``ac_rcd`` requirement.

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``ac_rcd/Selftest``
     - yes
   * - ``ac_rcd/DC``
     - yes
   * - ``ac_rcd/AC``
     - yes
   * - ``ac_rcd/MREC2GroundFailure``
     - yes
   * - ``ac_rcd/VendorError``
     - yes
   * - ``ac_rcd/VendorWarning``
     - no

isolation_monitor
-----------------

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``isolation_monitor/DeviceFault``
     - yes
   * - ``isolation_monitor/CommunicationFault``
     - yes
   * - ``isolation_monitor/VendorError``
     - yes
   * - ``isolation_monitor/VendorWarning``
     - no

power_supply_DC
---------------

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``power_supply_DC/CommunicationFault``
     - yes
   * - ``power_supply_DC/HardwareFault``
     - yes
   * - ``power_supply_DC/OverTemperature``
     - yes
   * - ``power_supply_DC/UnderTemperature``
     - yes
   * - ``power_supply_DC/UnderVoltageAC``
     - yes
   * - ``power_supply_DC/OverVoltageAC``
     - yes
   * - ``power_supply_DC/UnderVoltageDC``
     - yes
   * - ``power_supply_DC/OverVoltageDC``
     - yes
   * - ``power_supply_DC/OverCurrentAC``
     - yes
   * - ``power_supply_DC/OverCurrentDC``
     - yes
   * - ``power_supply_DC/VendorError``
     - yes
   * - ``power_supply_DC/VendorWarning``
     - no

powermeter
----------

Only the billing powermeter is evaluated: ``powermeter_car_side`` if it is connected, otherwise ``powermeter_grid_side``.
If ``fail_on_powermeter_errors`` is ``true``, ``powermeter/CommunicationFault`` and ``powermeter/VendorError`` block
charging. If it is ``false``, none of them do. ``powermeter/VendorWarning`` never blocks charging.

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``powermeter/CommunicationFault``
     - yes, if ``fail_on_powermeter_errors`` is ``true``
   * - ``powermeter/VendorError``
     - yes, if ``fail_on_powermeter_errors`` is ``true``
   * - ``powermeter/VendorWarning``
     - no

slac
----

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``generic/CommunicationFault``
     - yes
   * - ``generic/VendorError``
     - yes
   * - ``generic/VendorWarning``
     - no

ISO15118_charger
----------------

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``generic/CommunicationFault``
     - yes
   * - ``generic/VendorError``
     - yes
   * - ``generic/VendorWarning``
     - no

over_voltage_monitor
--------------------

.. list-table::
   :header-rows: 1

   * - Error type
     - Blocks charging
   * - ``over_voltage_monitor/MREC5OverVoltage``
     - yes
   * - ``over_voltage_monitor/DeviceFault``
     - yes
   * - ``over_voltage_monitor/CommunicationFault``
     - yes
   * - ``over_voltage_monitor/VendorError``
     - yes
   * - ``over_voltage_monitor/VendorWarning``
     - no

Shutdown types
--------------

When a charging session is stopped because of an error, the EvseManager differentiates between **Emergency Shutdowns** and **Error Shutdowns**. The severity of the
error influences the type of the shutdown. Emergency shutdowns are caused by errors with `Severity::High` and error shutdowns are caused by errors with `Severity::Medium` or `Severity::Low`.

In case of an **Emergency Shutdown** the EvseManager will immediately:

* Signal PWM state F if HLC is not active
* Turn off the PWM
* Turn off the DC power supply in case of DC
* Signal to open the contactor

In case of an **Error Shutdown** the EvseManager will:

* Signal PWM state F if HLC is not active
* Keep the PWM on if HLC is active
* Turn off the DC power supply in case of DC
* Signal to open the contactor
* Report the error via HLC to the EV (if HLC active)
