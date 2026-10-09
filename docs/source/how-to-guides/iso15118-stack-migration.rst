.. _howto-iso15118-stack-migration:

###############################################
Migrate from EvseV2G and IsoMux to Evse15118D20
###############################################

This is a goal-oriented how-to guide on moving an existing deployment from the
deprecated :ref:`EvseV2G <everest_modules_EvseV2G>` module (DIN SPEC 70121 and
ISO 15118-2 on the C implementation), or from the deprecated
:ref:`IsoMux <everest_modules_IsoMux>` module that multiplexes EvseV2G with
Evse15118D20, to :ref:`Evse15118D20 <everest_modules_Evse15118D20>` alone.
Evse15118D20 runs DIN SPEC 70121, ISO 15118-2 and ISO 15118-20 on the C++
ISO 15118 stack (``lib/everest/iso15118``) and takes over the responsibilities
of both modules.

EvseV2G and IsoMux are deprecated in 2026.10.0 and may be removed in 2027.04.0
at the earliest (see the :ref:`deprecation index <project-deprecation-index>`).
Both log a ``DEPRECATED MODULE`` warning at startup. Until they are removed
they keep working as before, so the migration can be planned per station.

The migration has three parts:

* **Decide what to offer**: which protocol generations the station announces
  in the SupportedAppProtocol handshake, and whether ISO 15118-20 joins the
  offer.
* **Module-level switchover**: replace the module in the EVerest configuration
  and map the config keys.
* **Surroundings**: EvseManager settings, certificates, firewall ports and the
  connections of other modules.

What stays the same
===================

* **Interfaces and wiring.** Evse15118D20 provides ``charger``
  (``ISO15118_charger``) and ``extensions`` (``iso15118_extensions``) under the
  same implementation ids as EvseV2G and IsoMux, and requires ``security``
  (``evse_security``) and optional ``iso15118_vas`` providers in the same way.
  The ``hlc`` requirement of EvseManager, the ``extensions`` requirement of the
  OCPP module and any VAS provider only need the ``module_id`` changed if the
  module id changes.
* **Config keys with the same name and meaning:** ``device`` (including
  ``auto``), ``enable_sdp_server``, ``auth_timeout_eim`` and
  ``auth_timeout_pnc`` (same defaults), ``supported_DIN70121`` and
  ``supported_ISO15118_2`` (but see their defaults below),
  ``tls_key_logging_path``.
* **SDP.** The SDP server still listens on UDP port 15118 on ``device``.
* **Certificates for ISO 15118-2.** The ISO 15118-2 TLS profile still needs a
  V2G leaf certificate on ``prime256v1`` in the EvseSecurity SECC leaf
  directory. Nothing has to be reinstalled when only DIN SPEC 70121 and
  ISO 15118-2 are offered.
* **EvseManager.** ``ac_hlc_enabled``, ``payment_enable_contract``,
  ``contract_certificate_installation_enabled``, ``hack_allow_bpt_with_iso2``
  and the other HLC related keys are read by EvseManager, not by the ISO
  module, and carry over unchanged.

Things to be aware of
=====================

Protocol offer defaults differ
------------------------------

EvseV2G offers DIN SPEC 70121 and ISO 15118-2 by default. Evse15118D20 offers
ISO 15118-20 by default and nothing else:

.. list-table::
   :header-rows: 1
   :widths: 40 30 30

   * - Key
     - EvseV2G default
     - Evse15118D20 default
   * - ``supported_DIN70121``
     - ``true``
     - ``false``
   * - ``supported_ISO15118_2``
     - ``true``
     - ``false``
   * - ``supported_ISO15118_20``
     - not available
     - ``true``

Set all three keys explicitly in the migrated configuration. A configuration
that relied on the EvseV2G defaults and sets none of them would offer only
ISO 15118-20 after the switch, and a DIN or ISO 15118-2 vehicle would find no
matching protocol.

Decide whether to offer ISO 15118-20. It is negotiated with the highest
priority, so a vehicle that supports it will prefer it over ISO 15118-2 from
the first session after the switch. ISO 15118-20 is TLS-only and needs a SECC
leaf on ``secp521r1`` or Ed448 (EvseSecurity leaf type ``V2G20``); without such
a leaf it is silently dropped from the offer. To keep the behavior of the
EvseV2G deployment identical, set ``supported_ISO15118_20: false``. When
offering it, also review ``supported_dynamic_mode``, ``supported_scheduled_mode``
and the TLS section of the :ref:`Evse15118D20 documentation
<everest_modules_handwritten_Evse15118D20>`.

TCP and TLS port
----------------

EvseV2G and IsoMux listen on TCP port 61341 (plain) and 64109 (TLS).
Evse15118D20 uses port 50000 for both plain and TLS connections. Firewall rules
on the PLC interface that allow 61341 and 64109 must allow 50000 instead; see
:doc:`Security Best Practices </how-to-guides/security-best-practices>` for the
rule set. The
SDP port 15118 (UDP) is unchanged.

TLS negotiation
---------------

``tls_security`` is replaced by ``tls_negotiation_strategy``:

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - ``tls_security`` (EvseV2G, IsoMux)
     - ``tls_negotiation_strategy`` (Evse15118D20)
     - Behavior
   * - ``allow`` (default)
     - ``ACCEPT_CLIENT_OFFER`` (default)
     - The transport security the EV requests in SDP is announced. Both modules
       fall back to a plain endpoint when the EV asks for TLS but no TLS endpoint
       is available (no SECC leaf installed).
   * - ``force``
     - ``ENFORCE_TLS``
     - TLS is announced regardless of the request. Evse15118D20 refuses to start
       the SECC when no V2G leaf certificate is installed.
   * - ``prohibit``
     - ``ENFORCE_NO_TLS``
     - A plain endpoint is announced regardless of the request. ISO 15118-20
       sessions on a plain connection are not standard-conformant and are
       logged as a warning.

Leave ``enforce_tls_1_3`` at its default ``false``: DIN SPEC 70121 and
ISO 15118-2 require TLS 1.2 with the ``prime256v1`` leaf. The TLS version
then follows the EV's offer, and the leaf is chosen per connection to match
(see "TLS and SECC leaf certificates" in the module documentation).

``tls_timeout`` (TLS handshake timeout in ms) has no counterpart; the C++
stack uses fixed internal timeouts.

Contract certificate validation (Plug and Charge)
-------------------------------------------------

``verify_contract_cert_chain`` has no counterpart. EvseV2G defaults it to
``false``, in which case the contract certificate chain is not checked locally
at all and is forwarded for validation by the CSMS. Evse15118D20 always
validates the ISO 15118-2 contract chain locally against the MO and V2G root
bundles of EvseSecurity. A chain that cannot be validated only because its
issuer is not installed (no MO or V2G root available, or the chain's root is
unknown) is forwarded to the CSMS when EvseManager's
``central_contract_validation_allowed`` is ``true``; otherwise the session is
ended with a ``FAILED`` response. Any other validation failure, such as an
expired contract leaf, is rejected locally.

For a Plug and Charge deployment this means:

* Install the MO root certificates (``get_verify_file`` of the ``MO`` CA type
  in EvseSecurity; OCPP installs them via ``InstallCertificate`` /
  ``MORootCertificate``), or
* set ``central_contract_validation_allowed: true`` in EvseManager when the
  CSMS validates contracts and no MO roots are installed on the station (the
  OCPP 2.0.1 use case C07.FR.06). The key defaults to ``false``.

A deployment that ran EvseV2G with ``verify_contract_cert_chain: true`` needs
no change beyond removing the key.

DIN SPEC 70121 EVSE ID
----------------------

For DIN SPEC 70121, EvseV2G sends the bytes of the eMI3 ``evse_id`` of
EvseManager in the hexBinary ``EVSEID`` field. Evse15118D20 sends EvseManager's
``evse_id_din`` (DIN SPEC 91286 as hexBinary, e.g. ``49A80737A45678``) and
falls back to packing ``evse_id`` only when ``evse_id_din`` is empty, which
works only for ids made of digits and ``*``. Set ``evse_id_din`` in EvseManager
for every DC EVSE that offers DIN SPEC 70121.

SAE J2847/2 bidirectional power transfer
----------------------------------------

The SAE J2847/2 bidirectional service over ISO 15118-2
(``sae_j2847_2_bpt_enabled`` and ``sae_j2847_2_bpt_mode`` in EvseManager) is
implemented in EvseV2G only. Evse15118D20 logs a warning and does not offer the
SAE service when EvseManager requests it. Stations that depend on it stay on
EvseV2G until the C++ stack provides SAE J2847/2; the deprecation period is
meant to cover this.

Telemetry
---------

EvseV2G publishes V2G telemetry blocks (``transport``, ``ev_electrical``,
``payment_service``, ``charger_status`` and others, category ``V2G``) through
the EVerest telemetry provider when telemetry is enabled, controlled by
``publish_telemetry_only_on_change``. Evse15118D20 publishes no telemetry and
the key has no counterpart. The ``v2g_messages`` and ``ev_app_protocol``
publishes under ``debug_mode`` exist in both modules.

Dropped and renamed keys
------------------------

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - EvseV2G key
     - Evse15118D20
   * - ``tls_security``
     - ``tls_negotiation_strategy``, see above
   * - ``tls_key_logging``
     - renamed to ``enable_tls_key_logging``
   * - ``tls_key_logging_path``
     - unchanged
   * - ``tls_timeout``
     - none, fixed internal timeouts
   * - ``verify_contract_cert_chain``
     - none, local validation is always on; see above
   * - ``terminate_connection_on_failed_response``
     - none. The C++ stack ends the session itself after a ``FAILED`` response
       where the protocol requires it, and otherwise leaves the SessionStop to
       the EV.
   * - ``publish_telemetry_only_on_change``
     - none, no telemetry
   * - ``supported_DIN70121``, ``supported_ISO15118_2``
     - unchanged names, but default ``false``; set explicitly
   * - ``device``, ``enable_sdp_server``, ``auth_timeout_eim``, ``auth_timeout_pnc``
     - unchanged

Keys of Evse15118D20 that have no EvseV2G counterpart and matter when
ISO 15118-20 is offered: ``supported_ISO15118_20``, ``enforce_tls_1_3``,
``supported_dynamic_mode``, ``supported_scheduled_mode``,
``supported_mobility_needs_mode_provided_by_secc``,
``negative_bidirectional_limits``, ``selecting_sap_based_on_energy_service``,
``custom_protocol_namespace``. ``logging_path`` is an obsolete key and should
not be set. ``enable_ssl_logging`` adds verbose TLS logging.

Module-level switchover
=======================

Coming from EvseV2G
-------------------

1. Change ``module: EvseV2G`` to ``module: Evse15118D20``.
2. Set ``supported_DIN70121``, ``supported_ISO15118_2`` and
   ``supported_ISO15118_20`` explicitly.
3. Map ``tls_security`` to ``tls_negotiation_strategy`` and rename
   ``tls_key_logging``; remove ``tls_timeout``, ``verify_contract_cert_chain``,
   ``terminate_connection_on_failed_response`` and
   ``publish_telemetry_only_on_change``.
4. Check ``central_contract_validation_allowed`` and ``evse_id_din`` in
   EvseManager as described above.
5. Update firewall rules from ports 61341 and 64109 to port 50000.

Before:

.. code-block:: yaml

   active_modules:
     iso15118_charger:
       module: EvseV2G
       config_module:
         device: eth1
         tls_security: allow
         tls_key_logging: false
       connections:
         security:
           - module_id: evse_security
             implementation_id: main
     evse_manager:
       module: EvseManager
       config_module:
         evse_id: DE*PNX*E12345*1
         charge_mode: DC
       connections:
         hlc:
           - module_id: iso15118_charger
             implementation_id: charger

After:

.. code-block:: yaml

   active_modules:
     iso15118_charger:
       module: Evse15118D20
       config_module:
         device: eth1
         supported_DIN70121: true
         supported_ISO15118_2: true
         supported_ISO15118_20: false   # true once a V2G20 leaf is installed
         tls_negotiation_strategy: ACCEPT_CLIENT_OFFER
         enable_tls_key_logging: false
       connections:
         security:
           - module_id: evse_security
             implementation_id: main
     evse_manager:
       module: EvseManager
       config_module:
         evse_id: DE*PNX*E12345*1
         evse_id_din: 49A80737A45678
         charge_mode: DC
       connections:
         hlc:
           - module_id: iso15118_charger
             implementation_id: charger

Coming from IsoMux
------------------

An IsoMux configuration runs three ISO modules: IsoMux owns the PLC interface,
the SDP server and the TLS endpoint, and proxies each session over the loopback
interface to an EvseV2G instance (DIN SPEC 70121, ISO 15118-2) or an
Evse15118D20 instance (ISO 15118-20), both with ``enable_sdp_server: false``.
Evse15118D20 negotiates all three protocol generations itself, so the
multiplexer and the EvseV2G instance are removed:

1. Delete the IsoMux and EvseV2G module instances.
2. On the remaining Evse15118D20 instance, set ``device`` to the PLC interface
   that IsoMux used, drop ``enable_sdp_server: false`` (or set it to ``true``),
   and set ``supported_DIN70121: true`` and ``supported_ISO15118_2: true`` next
   to ``supported_ISO15118_20: true``.
3. Map IsoMux's ``tls_security`` to ``tls_negotiation_strategy`` on
   Evse15118D20. ``proxy_port_iso2``, ``proxy_port_iso20`` and ``proxy_device``
   have no counterpart.
4. Point every connection that referenced the IsoMux instance (EvseManager
   ``hlc``, OCPP ``extensions``, VAS providers) at the Evse15118D20 instance.
   The ``iso2``, ``iso20``, ``ext2`` and ``ext20`` connections disappear with
   the multiplexer.
5. Evse15118D20 now terminates TLS for every protocol generation, so both SECC
   leaves (``prime256v1`` for ISO 15118-2, ``secp521r1`` or Ed448 for
   ISO 15118-20) must be installed in EvseSecurity. The module logs a warning
   at startup for every protocol offered without a leaf on its curve.
6. Update firewall rules from ports 61341 and 64109 to port 50000.

Before:

.. code-block:: yaml

   active_modules:
     iso15118_2:
       module: EvseV2G
       config_module:
         device: lo
         tls_security: allow
         enable_sdp_server: false
       connections:
         security:
           - module_id: evse_security
             implementation_id: main
     iso15118_20:
       module: Evse15118D20
       config_module:
         device: lo
         tls_negotiation_strategy: ACCEPT_CLIENT_OFFER
         enable_sdp_server: false
       connections:
         security:
           - module_id: evse_security
             implementation_id: main
     iso_mux:
       module: IsoMux
       config_module:
         device: eth1
         tls_security: allow
       connections:
         security:
           - module_id: evse_security
             implementation_id: main
         iso2:
           - module_id: iso15118_2
             implementation_id: charger
         iso20:
           - module_id: iso15118_20
             implementation_id: charger
         ext2:
           - module_id: iso15118_2
             implementation_id: extensions
         ext20:
           - module_id: iso15118_20
             implementation_id: extensions
     evse_manager:
       module: EvseManager
       connections:
         hlc:
           - module_id: iso_mux
             implementation_id: charger

After:

.. code-block:: yaml

   active_modules:
     iso15118_charger:
       module: Evse15118D20
       config_module:
         device: eth1
         supported_DIN70121: true
         supported_ISO15118_2: true
         supported_ISO15118_20: true
         tls_negotiation_strategy: ACCEPT_CLIENT_OFFER
       connections:
         security:
           - module_id: evse_security
             implementation_id: main
     evse_manager:
       module: EvseManager
       connections:
         hlc:
           - module_id: iso15118_charger
             implementation_id: charger

Verify the migration
====================

* ``manager --check --config <path>`` validates the configuration, which
  catches unknown or misspelled keys such as a leftover ``tls_security``.
* The startup log no longer contains ``DEPRECATED MODULE``. Evse15118D20 warns
  at startup when a protocol is offered without a SECC leaf on its curve, and
  logs an error when no protocol is left to offer.
* ``ss -ltnp`` on the station shows the module listening on port 50000 instead
  of 61341 and 64109.
* Run a session per protocol generation that the station offers and check
  ``selected_protocol`` on the ``evse_manager`` interface (also available on
  the ``evse_manager_consumer_API``). With ``session_logging`` enabled in
  EvseManager (passed to the module as ``debug_mode``), the ``v2g_messages``
  variable of the module shows the SupportedAppProtocol handshake. For packet-level debugging, see
  :doc:`Debug ISO 15118 </how-to-guides/debug-iso15118>`; the TLS key log
  works as before via ``enable_tls_key_logging``.
