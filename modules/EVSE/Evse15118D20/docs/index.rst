.. _everest_modules_handwritten_Evse15118D20:

.. ===================
.. Evse15118D20 Module
.. ===================

This module implements the SECC side of ISO 15118-20 charging, including both AC
DER services of ISO 15118-20 Amendment 1, ``AC_DER_IEC`` (Annex L) and
``AC_DER_SAE`` (Annex M), and a ``grid_support`` provider that accepts active DER
directives and raises grid alarms from EV-reported grid-event conditions.

The two DER flavors are mutually exclusive per EVSE, so at most one is ever
advertised. EvseManager enforces that when it builds the energy transfer mode
set; see "DER flavor selection" below.

DER grid support
================

This section describes the ``AC_DER_IEC`` path. ``AC_DER_SAE`` is covered in
"AC_DER_SAE grid code" below.

Active DER directives received via ``grid_support::set_active_directives`` are
stored and relayed to the EV as AC_DER_IEC control functions. Only the curve
family is mapped: ``VoltVar`` to ``VoltVarMode``, ``WattVar`` to ``WattVarMode``,
``WattPF`` to ``WattCosPhiMode``, each as an absolute-unit control curve. Non-curve
directives (frequency, volt-watt, setpoint, ride-through) have no counterpart
here and are logged and skipped.

The relay is IEC-only. On a SAE station the stored directives reach nothing:
``apply_active_der_directives`` logs a warning and leaves the SAE grid code
unchanged, so the station runs the inert default until a SAE relay lands.

Application is next-session-dynamic: a directive arriving mid-session takes
effect at the next V2G session. The DER transfer limits advertised at each
session's charge parameter discovery derive from ``ac_limits``.

This module does not publish DER ``capability``; an empty one is expected for
AC_DER_IEC.

The DER control functions the EV supports are surfaced in
``ChargingNeeds.der_charging_parameters.ev_supported_dercontrol``, so the backend
learns what the EV supports without the EVSE guessing. For AC_DER_IEC they come
from what the EV negotiates during service selection; for AC_DER_SAE from the
``SupportedModes`` bitmap in the charge parameter discovery request
([V2G20-3409]). See "Known gaps" for what a SAE session leaves out.

Discharge power limits follow ISO 15118-20 8.3.5.2.1: negative when the
``negative_bidirectional_limits`` config key is set, with nominal discharge power
set equal to maximum discharge power so nominal never exceeds maximum in
magnitude. That constraint is normative only for ``AC_DER_SAE`` ([V2G20-3228],
[V2G20-3229], Annex M); Annex L has no equivalent numbered requirement, so the IEC
path applies it defensively.

AC_DER_SAE grid code
====================

Negotiation
-----------

SAE negotiation is declare, enable, acknowledge. The EV declares its
``SupportedModes`` bitmap in the charge parameter discovery request. The SECC
masks it with ``SAE_MODE_BITMAP_MASK`` (``0x05FFFDFB``; bits 2, 9, 25 and 27 to 31
are unused by the specification and must be ignored), then sets the per-function
``Enable`` booleans inside ``DERControlCPDRes``, clearing every ``Enable`` whose
function the EV did not declare. There is no SECC-side mode bitmap; the ``Enable``
flags are the SECC's only expression channel.

The EV acknowledges in ``EnabledModes``. The charge loop compares that against the
modes the SECC enabled, not against the EV's declaration, masked with
``SAE_ENABLED_MODE_MASK`` to ignore unused bits and bits with no ``Enable`` in
``DERControlCPDRes``. A mismatch is not an error: no requirement makes the SECC
enforce the acknowledge, so it emits one latched warning naming the missing and
extra functions and continues.

The diagram shows which side owns each step, and where the mask and the
per-function gating sit relative to the response carrying them.

.. mermaid:: sae-der-negotiation.mmd

Default grid code
-----------------

``get_default_sae_der_control()`` returns a complete but functionally inactive
configuration: every ``enable`` and ``permit_service`` is false. ``DERControlCPDRes``
and all its sub-elements are mandatory, so a SAE SECC must emit a full grid code at
charge parameter discovery or not offer the service at all. An inert one keeps a
station that was never given a grid code from claiming one.

The values are structurally valid, and the two mandatory curves per trip family
carry the schema minimum of two data points, so a SAE station is conformant
without any DSO directive.

Mid-session updates
-------------------

``DERControlCLRes`` updates parameters already sent in ``DERControlCPDRes``. Only
``EnterServiceCLRes`` is mandatory inside it, and only ``permit_service`` inside
that. The other four blocks (``voltage_trip``, ``frequency_trip``,
``reactive_power_support_cl_res``, ``active_power_support_cl_res``) are emitted
only when the DER control changed since charge parameter discovery, keyed on
``DerSaeSetupConfig::der_control_update_time``. Sending a conditional element
unconditionally is itself non-conformant, so the unchanged path clears them.

Transfer limits and the voltage window
--------------------------------------

Unlike the IEC limits, the SAE limits do not derive from ``ac_limits`` alone. They
also use ``evse_max_reactive_power`` (from ``update_ac_parameters``) and the
nominal voltage from ``ac_setup_config``. The nominal voltage has no fallback:
until the AC parameters arrive it is unset and the service is withheld.

The advertised voltage window is the nominal voltage times 1.10
(``GridMaximumVoltage``) and 0.88 (``GridMinimumVoltage``). Both fractions must
stay in step with the first breakpoints of the library's default over- and
under-voltage must-trip curves (110 % and 88 %): those curves carry duration on x
and ``PercentageV`` on y, and the EV denormalizes the percentage against the base
voltage for the function, usually the nominal voltage advertised here. Changing
one side alone silently moves the EV's trip thresholds.

Service withdrawal
------------------

If ``ac_limits.nominal_frequency`` or the nominal voltage is not yet positive when
the DER limits are derived, the module logs an error naming the missing parameter
and leaves ``der_sae_limits`` unset, which makes the library strip ``AC_DER_SAE``
from the advertised services. Advertising a placeholder grid is worse than
withholding the service: the nominal voltage is the usual base for the percentage
voltage curves, so a guess moves those thresholds on a 277 V or 120 V site, and
the nominal frequency is advertised as the ``GridNominalFrequency`` the EV adopts
as the frequency of the grid it is connecting to.

The frequency-trip curves are separate. Their y unit is ``Hz``, so their
breakpoints are absolute and not scaled by ``GridNominalFrequency``. The shipped
default carries 50 Hz-family constants (51.5 and 52 Hz over-frequency must-trip,
47.5 and 47 Hz under-frequency, a 49.5 to 50.1 Hz enter-service band). Setting the
nominal frequency to 60 does not move them; a 60 Hz operator supplying a real setup
config must supply 60 Hz curve values with it.

The withdrawal is not permanent. The derivation re-runs whenever the supported
energy services, the AC limits or the AC parameters change, not only at
``ready()``, so a service withheld at boot is picked up once they arrive.

Reactive power sign convention
------------------------------

Absorption fields are advertised non-negative and injection fields non-positive,
per ISO 15118-20 AMD1 clause 8.3.5.2, which replaces Table 94 and binds both
annexes. All four reactive power limits are mandatory on the wire, so an absent or
zero EVSE capability yields explicit zeros rather than omitted elements.

Known gaps
----------

- No SAE ``der_relay`` exists, so ``grid_support`` directives cannot reach a SAE
  session. In practice nothing changes the configuration mid-session, so the four
  optional ``DERControlCLRes`` blocks are not currently emitted.
- A SAE session's ``ChargingNeeds`` omits the reactive-power limits. SAE var
  absorption and injection do not map onto the IEC charge and discharge reactive
  fields of ``types::iso15118::DERChargingParameters``, so those are left unset.
  The supported-modes bitmap, the four excitation fields and the optional session
  total discharge energy available do pass through.
- ``required_der_operating_mode`` and ``grid_connection_mode`` are dictated at the
  charge parameter discovery only. [V2G20-3358] to [V2G20-3361] require the SECC to
  re-send each one in the charge loop when it changes; that is not implemented, and
  both are left unset on every SAE charge loop response.
- The grid code itself can be re-dictated mid-session through ``DERControlCLRes``
  ([V2G20-3236]) and the library does build it: the four optional sub-structs are
  emitted whenever the setup config's ``der_control_update_time`` differs from the
  value already sent, and the steady-state payload carries only the mandatory
  ``EnterServiceCLRes.PermitService``. Nothing can trigger it in a running session
  yet. ``TbdController::update_der_sae_limits`` writes only the next-session setup
  and ``SessionConfig`` copies the grid code at construction, so
  ``der_control_update_time`` cannot change within a session. Reaching a running
  session needs a control event arm like the one ``ac_limits`` has, plus the SAE
  ``der_relay`` above to produce the change.

DER flavor selection
====================

The diagram answers "why is my station not advertising the DER service I
configured?". An AC DER service needs ``set_der_available`` asserted and an
export-capable EVSE before the ``iso15118_der_flavor`` config key picks the annex,
and SAE needs the AC parameters on top of that. The key defaults to ``NONE``, so
AC DER is opt-in even when availability is asserted.

Asserting availability does not require a wired ``grid_support`` provider. It
normally arrives from OCPP via a ``grid_support`` capability publish, but the SIL
configs assert it directly over MQTT with no provider wired, which advertises the
service while leaving the directive path inert.

.. mermaid:: sae-flavor-selection.mmd

SECC state machine
==================

The ISO 15118-20 state machine of the underlying library, with the four AC DER
states in place. Two things to take from it: the four AC DER states sit alongside
the plain AC and DC ones rather than inside them, and ``AC_DER_SAE`` charge
parameter discovery is the only charge parameter discovery state that can hand
control back to ``ServiceDetail``, which is how an EV that rejects the dictated
grid code restarts service selection.

Terminations that any state performs on a ``SessionStopReq`` or a sequence error
are omitted, since they end the session rather than move it to another state.

.. mermaid:: d20-state-machine.mmd
