.. _everest_modules_handwritten_Evse15118D20:

.. ===================
.. Evse15118D20 Module
.. ===================

This module implements the SECC side of ISO 15118-20 charging, including the
AC_DER_IEC service and a ``grid_support`` provider that accepts active DER
directives and raises grid alarms from EV-reported grid-event conditions.

DER grid support
================

Active DER directives received via ``grid_support::set_active_directives`` are
stored and relayed to the EV as AC_DER_IEC control functions. The relay is
implemented for the curve family: ``VoltVar`` maps to ``VoltVarMode``,
``WattVar`` to ``WattVarMode``, and ``WattPF`` to ``WattCosPhiMode``, each encoded
as an absolute-unit ISO 15118-20 control curve. Non-curve directives (frequency,
volt-watt, setpoint, ride-through, and similar) have no AC_DER_IEC counterpart
here; they are logged and skipped.

Application is next-session-dynamic: a directive arriving mid-session takes
effect at the next V2G session rather than interrupting the running one. The DER
transfer limits advertised at each session's charge parameter discovery derive
from the module's AC transfer limits (``ac_limits``).

This module does not publish DER ``capability``; for the AC_DER_IEC service an
empty capability is expected.

The DER control functions the EV negotiates during service selection are
surfaced upward in ``ChargingNeeds.der_charging_parameters.ev_supported_dercontrol``
so the backend can learn what the EV supports without the EVSE advertising a
guess.

Discharge power limits advertised to the EV follow ISO 15118-20 8.3.5.2.1: they
are emitted as negative values when the ``negative_bidirectional_limits`` config
key is set, and the nominal discharge power is set equal to the maximum discharge
power so the advertised pair never violates V2G20-3229.
