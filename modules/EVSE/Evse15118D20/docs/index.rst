.. _everest_modules_handwritten_Evse15118D20:

.. ===================
.. Evse15118D20 Module
.. ===================

This module implements the SECC side of ISO 15118-20 charging, including the
AC_DER_IEC service and a ``grid_support`` provider that accepts active DER
directives and raises grid alarms from EV-reported grid-event conditions.
It also offers ISO 15118-2 and DIN SPEC 70121 (see ``supported_ISO15118_2``
and ``supported_DIN70121``).

TLS and SECC leaf certificates
==============================

ISO 15118-2 and ISO 15118-20 prescribe different TLS profiles that cannot be
served by one certificate:

* ISO 15118-2: TLS 1.2, ``TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256``, SECC leaf
  on ``secp256r1`` (``prime256v1``), no client certificate.
* ISO 15118-20: TLS 1.3, ``TLS_AES_256_GCM_SHA384`` /
  ``TLS_CHACHA20_POLY1305_SHA256``, SECC leaf on ``secp521r1`` or ``Ed448``,
  mutual TLS with the vehicle certificate.

The TLS version and cipher follow whatever the EV offers in its ClientHello
(unless ``enforce_tls_1_3`` pins TLS 1.3). To also present the right leaf,
install both SECC leaf certificates (with their keys) in the ``EvseSecurity``
SECC leaf directory -- or let OCPP provision them (``V2GCertificate`` and, on
OCPP 2.1, ``V2G20Certificate``; see the OCPP module documentation). The two
are the ``EvseSecurity`` leaf types ``V2G`` (ISO 15118-2 profile) and ``V2G20``
(ISO 15118-20 profile, ``secp521r1`` / ``Ed448`` key). The module fetches every
valid leaf of both types -- newest per issuing root -- and tags each chain by
its key: ``prime256v1`` leaves are presented on TLS 1.2 connections,
``secp521r1`` / ``Ed448`` leaves on TLS 1.3 connections, anything else on both.
Within a version the EV's advertised CA list (``certificate_authorities`` on
TLS 1.3, ``trusted_ca_keys`` on TLS 1.2) picks among the chains, so the two
leaves may chain to different sub-CAs or roots. When only one leaf is installed
it is presented on every connection as before; a warning is logged at startup
if a protocol is offered without a leaf on its mandated curve. The chain list
is rebuilt on every ``certificate_store_update`` event, so a renewed leaf of
either type is served from the next connection on without a restart.

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
