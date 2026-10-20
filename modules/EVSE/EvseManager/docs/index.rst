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
source selected by ``welding_detection_voltage_source`` instead of the power supply measurement:

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

The ISO module holds the first ``WeldingDetectionRes`` until a value of the selected source arrived,
for at most 1 s. EvseManager sends the first sample of the source, or after 500 ms its last value of
this session, which is valid because side B stays at the battery voltage until the EV opens its
contactors. The
source should publish at 5 Hz or more (CC.6.3: at most 200 ms from measurement to communication);
EvseManager warns once per session if it updates less often than every second. Over voltage
monitors often publish only at about 1 Hz. If ``Finished`` never arrives, the phase also ends on
D-LINK events, a failed HLC session, the next cable check or charge loop, unplug, or 20 s after it
started (CC.9 t104 to t107).

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
ISO 15118 (ChargeParameterDiscovery and mid-session limit updates), but they
never affect internal power supply control such as cable check, precharge or
setpoint clamping.


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

Reservation
-----------

Reservation handling logic is implemented in the Auth module. If the Auth
module wants to reserve a specific EvseManager (or cancel the reservation) it
needs to call the reserve/cancel_reservation commands. EvseManager does not
check reservation id against the token id when it should start charging, this
must be handled in Auth module. EvseManager only needs to know whether it is
reserved or not to emit an ReservatonStart/ReservationEnd event to notify other
modules such as OCPP and API or e.g. switch on a specific LED signal on the
charging port.

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

Error Handling
==============

The control flow of this module can be influenced by the error implementation of its requirements. This section documents
the side effects that can be caused by errors raised by a requirement.

This module subscribes to all errors of the following requirements:

* evse_manager
* evse_board_support
* connector_lock
* ac_rcd
* isolation_monitor
* power_supply_DC
* powermeter (if the config option fail_on_powermeter_errors is set true)

A raised error can cause the EvseManager to stop the charging session and become inoperative. Charging is not possible until the error is cleared.
If no charging session is currently running, it will prevent sessions from being started. If a charging session is currently running and an error is raised
this will interrupt the charging session.

Almost all errors that are reported from the requirements of this module cause the EvseManager to become Inoperative until the error is cleared.
The following sections provide an overview of the errors that do **not** cause the EvseManager to become Inoperative.

evse_manager
-------------

* evse_manager/Inoperative
* evse_manager/MREC11CableCheckFault


evse_board_support
------------------

* evse_board_support/MREC3HighTemperature
* evse_board_support/MREC18CableOverTempDerate
* evse_board_support/VendorWarning

connector_lock
--------------

* connector_lock/VendorWarning

ac_rcd
------

* ac_rcd/VendorWarning

isolation_monitor
-----------------

* isolation_monitor/VendorWarning

power_supply_DC
---------------

* power_supply_DC/VendorWarning

powermeter
----------

Powermeter errors cause the EvseManager to become Inoperative, if fail_on_powermeter_errors is configured to true. If it is configured to false, errors from the powermeter will not cause the EvseManager to become Inoperative.

* powermeter/CommunicationFault
* powermeter/VendorError

Note that ``powermeter/VendorWarning`` is explicitly ignored by the EvseManager's inoperative logic (similar to other ``VendorWarning`` errors)
and will not block charging even if ``fail_on_powermeter_errors`` is set to true. It should be used to signal non-fatal conditions such as
high temperature warnings from the powermeter.

When a charging session is stopped because of an error, the EvseManager differentiates between **Emergency Shutdowns** and **Error Shutdowns**. The severity of the
error influences the type of the shudown. Emergency shutdowns are caused by errors with `Severity::High` and error shutdowns are caused by errors with `Severity::Medium` or `Severity::Low`.

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
