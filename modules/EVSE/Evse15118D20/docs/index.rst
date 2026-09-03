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

This section describes both directive relays. The SAE negotiation, the default
grid code and the mid-session path are covered in "AC_DER_SAE grid code" below.

Active DER directives received via ``grid_support::set_active_directives`` are
stored and relayed to the EV per annex.

The IEC relay maps three curve families, ``VoltVar`` to ``VoltVarMode``,
``WattVar`` to ``WattVarMode`` and ``WattPF`` to ``WattCosPhiMode``, each as an
absolute-unit control curve; a type that appears twice is last-wins. Non-curve
directives (frequency, volt-watt, setpoint, ride-through) have no counterpart here
and are logged and skipped. Application is next-session-dynamic: a directive
arriving mid-session takes effect at the next V2G session. The DER transfer limits
advertised at each session's charge parameter discovery derive from ``ac_limits``.

The SAE relay maps the directive table onto the inert default grid code, so a
directive that leaves the set falls back to the shipped default rather than
lingering: ``HVMustTrip``, ``HVMayTrip``, ``HVMomCess``, ``LVMustTrip``,
``LVMayTrip``, ``LVMomCess``, ``HFMustTrip``, ``HFMayTrip``, ``LFMustTrip``,
``VoltVar``, ``WattVar``, ``FixedVar``, ``VoltWatt``, ``FreqDroop``,
``LimitMaxDischarge``, ``FixedPFAbsorb``, ``FixedPFInject`` and ``EnterService``.
``FixedPFAbsorb`` and ``FixedPFInject`` share the single ``ConstantPowerFactor``
element, so one wins and the other is shadowed. Each Annex M element takes one
writer: the lowest priority wins, array order breaks ties, and shadowed
directives are logged by id (unlike the IEC relay, which ignores priority). A
winner that fails validation frees its element for the next candidate. Only the
single-phase fields are written. ``LimitMaxDischarge`` is an approximation: the
percentage is clamped to 0..100 and rounded, not rescaled against the EV's
maximum discharge power. The mapped grid code reaches the next session and, after
validation, the running one; see "Mid-session updates" below.

The relay owns every unit and orientation conversion, so the library below it
knows nothing about OCPP:

- ``grid_support`` trip curves arrive in OCPP orientation, threshold on ``x`` and
  seconds on ``y``. The relay swaps them into the Annex M duration-first form
  (``x`` the duration in seconds, ``y`` the threshold) and sorts by duration per
  M.2.2.1.10 and M.2.2.1.11; two points sharing a duration reject the directive.
  HV/LV thresholds stay percentages of nominal (``PercentageV``) and HF/LF stay
  Hz, so no volt conversion happens.
- ``EnterService`` voltage bands and the VoltVar ``ReferenceVoltage`` pass through
  in volts, because AMD1 Table 1 gives them in volts and so does ``grid_support``.
- The ``FixedVar`` setpoint sign is inverted into ``VarSetpoint``. The two sign
  conventions are opposed and [V2G20-3183] mandates the conversion.
- The directive ``priority`` is forwarded into every written element that carries
  one; ``EnterService`` has none. A value outside the Annex M ``uint16`` range is
  omitted with a warning rather than wrapped.
- A curve with more than ten data points is rejected, never truncated: dropping
  points would silently reshape the grid code the operator asked for.

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

``get_default_sae_der_control(nominal_voltage_v)`` returns a complete but
functionally inactive configuration: every ``enable`` and ``permit_service`` is
false. ``DERControlCPDRes`` and all its sub-elements are mandatory, so a SAE SECC
must emit a full grid code at charge parameter discovery or not offer the service
at all. An inert one keeps a station that was never given a grid code from
claiming one.

The volt-valued fields are derived from the nominal voltage passed in: the
enter-service window is 1.05 and 0.917 of nominal (AMD1 Table 1 gives both bands
in volts) and the volt-var ``ReferenceVoltage`` is the nominal itself. A
``FrequencyDroop`` carrying a zeroed over-frequency branch is present but
disabled, because Table M.33 obliges ``UnderFrequencyDroop`` whenever
``OverFrequencyDroop`` is absent, and that holds regardless of ``enable``.

The values are structurally valid, and the two mandatory curves per trip family
carry the schema minimum of two data points, so a SAE station is conformant
without any DSO directive.

Mid-session updates
-------------------

``DERControlCLRes`` updates parameters already sent in ``DERControlCPDRes``
([V2G20-3236]). Only ``EnterServiceCLRes`` is mandatory inside it, and only
``permit_service`` inside that. The other four blocks (``voltage_trip``,
``frequency_trip``, ``reactive_power_support_cl_res``,
``active_power_support_cl_res``) are emitted only when the grid code changed
since charge parameter discovery. Sending a conditional element unconditionally
is itself non-conformant, so the unchanged path clears them.

Change detection is keyed on ``DerSaeSetupConfig::revision``, a producer-owned
counter this module bumps only when the relay input changes: the
sorted directive ids with their ``received_at`` stamps, plus the nominal voltage
and frequency the mapping converts against. The session records the revision it
sent at charge parameter discovery and after each re-send, and compares that
against the installed config. ``der_control_update_time`` is the wall clock
``UpdateTime`` on the wire and nothing else; it is never compared, because a
timestamp changes on every construction while the counter changes only with the
content. A re-apply of an unchanged directive set, or an AC-limit re-derivation,
therefore does not re-dictate the grid code.

``RequiredDEROperatingMode`` and ``GridConnectionMode`` are handled per field and
independently of the revision: each is re-sent in the charge loop response only
when it differs from the value last sent to this EV and is left unset otherwise
([V2G20-3358] to [V2G20-3361]).

The path from a directive to the wire runs once ``AC_DER_SAE`` is advertised and
its limits are derived; otherwise ``apply_active_der_directives`` logs at info and
leaves the revision untouched. ``relay_sae_grid_code`` compares the relay input,
maps the set with ``map_active_directives_to_sae_der_control``, validates the
result with ``validate_sae_der_setup`` against the SAE DER limits and AC limits
held at that moment, stores it as the next-session setup and hands it to
``TbdController::update_der_sae_limits``. The controller pushes it into the
running session as a control event, where ``install_der_sae_setup_config``
validates it once more against the session's own limits before installing it. The
next ``DERControlCLRes`` then carries the four blocks, gated by the EV's declared
``SupportedModes`` like the discovery response, and the ``EnabledModes`` mismatch
warning re-arms. An invalid grid code is rejected with a warning at either stage.
A module-stage rejection leaves the previous revision dictated. A session-stage
rejection keeps the running session on its old grid code; the new revision is
still the next-session setup. The controller also guards a caller passing no
setup config: that withdrawal is logged once per withdrawal and does not reach
the running session, which keeps its grid code until it ends. This module never
withdraws; an empty directive set produces a new inert revision instead. The
relayed setup config outlives AC-limit re-derivations: the derivation seeds it
only while none is held or the held one is still the seed (revision 0).

.. mermaid:: sae-der-relay.mmd

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

- A SAE session's ``ChargingNeeds`` omits the reactive-power limits. SAE var
  absorption and injection do not map onto the IEC charge and discharge reactive
  fields of ``types::iso15118::DERChargingParameters``, so those are left unset.
  The supported-modes bitmap, the four excitation fields and the optional session
  total discharge energy available do pass through.
- ``required_der_operating_mode`` and ``grid_connection_mode`` are re-sent per
  field in the charge loop response when they differ from the value last sent
  ([V2G20-3358] to [V2G20-3361]). Nothing in this module changes them after boot;
  the relay carries the current values through, so in practice both are dictated
  at charge parameter discovery only.
- Directive types with no Annex M target are logged once per applied set and
  skipped: ``FreqWatt``, ``WattPF``, ``ZeroCurrent``,
  ``OvervoltageFaultRideThrough``, ``UndervoltageFaultRideThrough``,
  ``MaximumLevelDCInjection``, ``DSOQSetpoint``, ``DSOCosPhiSetpoint``,
  ``PowerMonitoringMustTrip`` and ``Gradients`` exist in Annex L or in OCPP only.
- The SAE relay writes the single-phase fields only; every ``_L2`` and ``_L3``
  counterpart stays unset.
- ``LimitMaxDischarge`` is approximated: ``pct_max_discharge_power`` lands in
  ``PercentageValue`` clamped to 0..100 and rounded, not rescaled against the
  EV's maximum discharge power.
- ``grid_support`` ``ramp_rate`` (%/s) is copied into ``EnterServiceRampTime``
  (s) unconverted, by decision, and needs a follow-up. It reaches
  ``EnterServiceRampTime`` only when paired with ``delay``; alone it is dropped at
  debug, and ``delay`` without ``ramp_rate`` rejects the directive.
- The inert default's enter-service bands and volt-var reference voltage follow
  the configured nominal voltage, so a nominal change re-seeds the default until
  a grid code is relayed.

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
