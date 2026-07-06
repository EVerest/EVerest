.. _everest_modules_handwritten_EvIso15118D20:

EvIso15118D20
=============

EV-side ISO 15118-20 EVCC built on the ``libiso15118`` ``ev::Controller``. It
provides the ``ISO15118_ev`` interface (the same interface as ``PyEvJosev``), so
it is a drop-in software-in-the-loop (SIL) replacement for the Python EVCC when
driving a DC or AC ISO 15118-20 session against a SECC such as ``Evse15118D20``.

AC support covers the Dynamic control mode over plain TCP with external
identification means (EIM) authorization. The AC DER IEC service is also
negotiated (assuming a three-phase inverter relay); received DER directives are
logged only, as ``ISO15118_ev`` has no DER variable to publish them on.

Configuration
-------------

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Option
     - Default
     - Description
   * - ``device``
     - ``eth0``
     - Ethernet device used for high-level communication. ``auto`` lets the
       library pick a usable IPv6 interface.
   * - ``evcc_id``
     - ``02:00:00:00:00:01``
     - EVCC identifier sent to the SECC (MAC-address format).
   * - ``response_timeout_ms``
     - ``20000``
     - Response watchdog timeout in milliseconds.
   * - ``ac_max_charge_power_w``
     - ``11040``
     - Advertised AC maximum charge power in watts.
   * - ``ac_min_charge_power_w``
     - ``1380``
     - Advertised AC minimum charge power in watts.
   * - ``der_over_frequency_watt_mode``
     - ``false``
     - Declare support for the IEC OverFrequencyWattMode DER control function.
   * - ``der_under_frequency_watt_mode``
     - ``false``
     - Declare support for the IEC UnderFrequencyWattMode DER control function.
   * - ``der_volt_watt_mode``
     - ``false``
     - Declare support for the IEC VoltWattMode DER control function.
   * - ``der_volt_var_mode``
     - ``false``
     - Declare support for the IEC VoltVarMode DER control function.
   * - ``der_watt_var_mode``
     - ``false``
     - Declare support for the IEC WattVarMode DER control function.
   * - ``der_watt_cos_phi_mode``
     - ``false``
     - Declare support for the IEC WattCosPhiMode DER control function.
   * - ``der_dso_q_setpoint_provision``
     - ``true``
     - Declare support for the IEC DSOQSetpointProvision DER control function.
   * - ``der_dso_cos_phi_setpoint_provision``
     - ``true``
     - Declare support for the IEC DSOCosPhiSetpointProvision DER control function.
   * - ``der_dc_injection_restriction``
     - ``false``
     - Declare support for the IEC DCInjectionRestriction DER control function.
   * - ``der_zero_current_mode``
     - ``false``
     - Declare support for the IEC ZeroCurrentMode DER control function.
   * - ``der_over_voltage_fault_ride_through_mode``
     - ``false``
     - Declare support for the IEC OverVoltageFaultRideThroughMode DER control function.
   * - ``der_under_voltage_fault_ride_through_mode``
     - ``false``
     - Declare support for the IEC UnderVoltageFaultRideThroughMode DER control function.
   * - ``der_stop_on_unsupported_functions``
     - ``true``
     - Stop the session if no AC_DER_IEC parameter set fits the supported DER functions.

DER control function negotiation
--------------------------------

For the AC DER IEC service the EV declares which IEC DER control functions it
supports through the ``der_*`` options above; these form a bitmask carried in the
ServiceDetail exchange. Among the SECC's Dynamic parameter sets the EV selects
the first whose declared functions are a subset of the supported set (a set with
no ``DERControlFunctions`` parameter counts as compatible). When no offered set
fits, ``der_stop_on_unsupported_functions`` decides the outcome: ``true`` stops
the session cleanly, while ``false`` proceeds with the first Dynamic set and warns
about the unsupported functions. At runtime, a DSO setpoint (Q or cos phi) that
arrives in a charge-loop response without the matching function having been
negotiated is dropped before the directive is surfaced; all other fields pass
through unchanged.

Limitations
-----------

The implementation has a deliberately narrow scope:

- **DC and AC only.** ``start_charging`` accepts DC, AC single/three-phase, and
  AC DER IEC energy-transfer modes; WPT and MCS sessions are not supported.
- **DER directives are log-only.** AC DER IEC directives (target active power,
  DSO Q and cos phi setpoints) are logged, not published, pending an interface
  variable. The three-phase inverter relay use case is assumed.
- **No TLS.** The session advertises ``NO_TRANSPORT_SECURITY``; Plug & Charge and
  TLS are out of scope.
- **No pause/resume.** ``pause_charging`` is a no-op.
- **No BPT / SAE.** Bidirectional power transfer (``set_bpt_dc_params``) and
  SAE J2847/2 (``enable_sae_j2847_v2g_v2h``) are not implemented.

Threading
---------

A single worker thread, started from ``ready()``, runs one V2G session at a time.
``start_charging`` hands a request to the worker; the worker constructs the
``ev::Controller`` and runs its event loop until the session ends, then waits for
the next request. Session state is guarded by a ``monitor`` so command handlers
and the worker coordinate safely, including during module teardown.
