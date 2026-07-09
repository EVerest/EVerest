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
accepted and stored, but are not yet applied to the EV: the DER control-function
relay that would push a directive into the session is not implemented. The DER
transfer limits advertised at each session's charge parameter discovery derive
from the module's AC transfer limits (``ac_limits``), not from the directives.
Applying directives (including via an in-session service renegotiation) is
planned but not yet implemented.

This module does not publish DER ``capability``; for the AC_DER_IEC service an
empty capability is expected.

The DER control functions the EV negotiates during service selection are
surfaced upward in ``ChargingNeeds.der_charging_parameters.ev_supported_dercontrol``
so the backend can learn what the EV supports without the EVSE advertising a
guess. Relaying grid directives back down onto those control functions is a
planned follow-up: it depends on this EV-supported set plus a source for the
nominal-voltage and reactive-power bases needed to encode the ISO 15118-20
absolute-unit control curves, neither of which this module currently holds.

Discharge power limits advertised to the EV follow ISO 15118-20 8.3.5.2.1: they
are emitted as negative values when the ``negative_bidirectional_limits`` config
key is set, and the nominal discharge power is never advertised above the
maximum discharge power (V2G20-3229), including under a ``LimitMaxDischarge``
curtailment directive.
