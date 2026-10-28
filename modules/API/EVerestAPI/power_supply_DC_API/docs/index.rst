.. _everest_modules_handwritten_power_supply_DC_API:

.. *******************************************
.. power_supply_DC_API
.. *******************************************

The complete API specification can be found in the

``docs/source/reference/EVerest_API/power_supply_DC_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/power_supply_DC_API/index.html>`_ automatically generated from it.

Setpoints below the minimum current
===================================

When the EV or an energy limit asks for less than the minimum current offered
to the EV, EVerest sends a current below ``min_export_current_A`` or
``min_import_current_A``, usually 0 A. This means: deliver no current (0 A,
+/- 0.15 A, IEC 61851-23:2023 CC.5.5.2). Do not raise it to the minimum. If the
hardware cannot regulate to 0 A, switch its output off, keep the mode, and switch
it on again when a current at or above the minimum is requested.
During cable check and precharge, EVerest never sends less than the minimum.

Measurement rate
================

Publish ``voltage_current`` at regular intervals, at least once per second and
ideally at 4 Hz, also when the values did not change. EVerest waits for fresh
values: before it switches the supply on for the cable check it needs a new
measurement below 60 V, and it follows the voltage ramp during cable check and
precharge on these values. A supply that publishes only when a value changes
cannot start a session.

Sequence of a DC session
========================

EVerest requests ``Export`` with charging phase ``CableCheck``, then
``PreCharge``, then ``Charging``; ``Off`` ends every phase and carries the phase
that was active. Each setpoint on ``export_voltage_current`` arrives shortly
before the matching mode request, so keep the last setpoint while ``Off`` and
apply it when ``Export`` is requested. The cable check uses the maximum voltage
the EV reported with at least 2 A; precharge uses the EV target voltage with at
least 2 A and not below ``min_export_current_A``; the reported voltage must reach
the setpoint within 10 s, otherwise the cable check fails with
``MREC11CableCheckFault`` and the EVSE becomes inoperative. From the first
current-demand target of the EV on, the EV target applies, including the 0 A
rule. At the end of a session EVerest requests ``Off`` before it opens the output
contactors. The ``mode`` the supply reports is informational only.

Errors are identified by type and sub type: ``clear_error`` must repeat the sub
type of the ``raise_error``, a second raise of an active error and a clear of an
inactive error are ignored without reply.

``tests/async_api_tests/test_power_supply_dc_api.py`` exercises the module in the DC
software-in-the-loop against this specification: an external fake supply implements the
``m2e`` side while EVerest runs charging sessions, faults, derating and communication loss.
