.. _everest_modules_handwritten_Ev15118:

Ev15118
=======

EV-side ISO 15118 EVCC built on the ``libiso15118`` ``ev::Controller``. It
provides the ``ISO15118_ev`` interface and drives a DC or AC session against a
SECC such as ``Evse15118D20`` or ``EvseV2G``.

The ``v2g_messages`` notification identifies received responses for all three
protocol generations, including ``PowerDeliveryRes`` before welding detection.
It does not report transmitted requests.

Protocols
---------

The SAP offer carries the protocols ``supported_protocols`` lists, a
comma-separated list of ``ISO15118_20``, ``ISO15118_2`` and ``DIN70121`` in
priority order: the first entry is offered with priority 1. For example
``ISO15118_2,ISO15118_20`` prefers ISO 15118-2 over ISO 15118-20. The list is
empty unless set, so a configuration offers only the protocols it names. The
module fails at startup when the list is empty, names an unknown protocol, or
names one twice.

- **ISO 15118-20** (AC, AC BPT, AC DER IEC, AC DER SAE, DC, DC BPT), EIM, plaintext or
  TLS 1.3 with a client certificate.
- **ISO 15118-2** (AC and DC), EIM or Plug & Charge (Contract). PnC needs TLS
  [V2G2-632]: with ``enable_pnc`` and TLS the EV offers ``PnC`` and ``EIM``, and
  presents the configured contract certificate in PaymentDetails; without a
  contract certificate (or with ``pnc_force_cert_install``) it runs a
  CertificateInstallation with the OEM provisioning certificate first, if the
  SECC offers installation in ServiceDetail for ServiceID 2 (ParameterSetID 1).
  A FAILED ServiceDetail response stops the session. If a successful response
  does not offer installation, it uses an available contract, or EIM if
  offered, and stops if neither is usable. An enforced Contract payment option
  remains selected without a certificate. TLS 1.2 is used for -2; a
  ``tls_version`` pinned to ``"1.3"`` runs it over TLS 1.3 anyway, and the
  module warns at startup about the deviation. An installed contract is
  kept in ``pnc_contract_store_dir`` when that is set, and later sessions
  present it instead of the configured contract.
- **DIN SPEC 70121** (DC, plaintext only), no authorization step.

The ISO 15118-2 / DIN EVCCID is the 6 byte MAC of the resolved ``device`` (the
``evcc_id`` string is ISO 15118-20 only). The EnergyTransferMode of
``start_charging`` selects the -2 / DIN transfer mode; the AC values are taken
from the ``iso2_ac_*`` options. A BPT or DER mode has no -2 / DIN equivalent and
degrades to the matching unidirectional mode, with a warning when the pre-20
generation is offered at all.

Which payment option the EV picks is config driven, because the ``EvManager``
default payment option is ``auto``: with ``enable_pnc`` and TLS the EV prefers
Contract whenever the SECC offers it, and the -2 engine falls back to EIM when
it does not. ``SelectedPaymentOption`` only overrides that per session; an
explicit ``ExternalPayment`` turns the preference off for that session, an
explicit ``Contract`` keeps it on. ``enforce_payment_option`` makes the EV
select Contract even when the SECC does not offer it (negative testing).
Without ``enable_pnc`` the EV offers EIM only.

The session runs over plain TCP or TLS (``tls`` / ``tls_version``, see
`Transport security`_). The AC DER IEC and AC DER SAE services are also
negotiated (assuming a three-phase inverter relay). The EV negotiates DER functions but never executes one; it
publishes what was negotiated and received (see `Published DER variables`_).

Bidirectional power transfer (BPT) is negotiated for both AC_BPT and DC_BPT in
Dynamic control mode. The advertised discharge limits come from the
``*_discharge_*`` config settings; ``set_bpt_dc_params`` overrides the
configured DC discharge power and current. Like ``set_dc_params`` it states
vehicle limits: they reach a running DC_BPT or MCS_BPT session at once and stay
for every later BPT session until the next ``set_bpt_dc_params``. A field it
leaves out keeps its previous value. During a DC_BPT or MCS_BPT session the
update is validated with the session's parameters and rejected whole when
invalid. These settings are magnitudes: the EV sends
discharge limits, var injection limits and excited discharge powers negated, as
ISO 15118-20 [V2G20-1035] requires. AC_BPT assumes a three-phase inverter relay.
Reverse power flow itself is driven entirely by the SECC's target-power
directives; the EVSE's advertised discharge limits are logged for visibility.

Transport security
------------------

``tls`` picks whether TLS runs: ``off`` requests no TLS in SDP, ``prefer``
requests it and accepts a plaintext SECC, ``require`` rejects an SDP response
without TLS. ``off`` runs ISO 15118-20 and ISO 15118-2 over plain TCP; for
ISO 15118-20 this deviates from [V2G20-2677] and is meant for testing (the
``config-sil-*-d20-plaintext.yaml`` configurations use it), and Plug & Charge
over plain TCP additionally needs ``allow_pnc_without_tls``. ``tls_version``
picks the version when TLS runs:

- ``auto`` offers TLS 1.3, with the vehicle chain loaded for mutual TLS, when
  the SAP offer holds ISO 15118-20, and TLS 1.2 when it holds ISO 15118-2. An
  explicit ``advertised_app_protocols`` list counts as the offer. When the
  vehicle chain or key is not configured, missing or unreadable at connect, 1.3
  is dropped and the EV logs why, once per reason. When no version is left
  ``start_charging`` is rejected with an error naming the enabled protocols and
  why 1.3 is unavailable; a chain file found unusable only at connect stops the
  session there with the same error. After the handshake the SAP offer is
  rebuilt for the negotiated version: only ISO 15118-20 over TLS 1.3, only
  ISO 15118-2 over TLS 1.2. When no enabled protocol is left the EV closes the
  connection and stops with an error naming the version and the enabled
  protocols; it never falls back to plaintext. An explicit
  ``advertised_app_protocols`` list is never narrowed after the handshake.
- ``"1.2"`` and ``"1.3"`` pin that version and offer every enabled protocol
  over it. ISO 15118-2 over TLS 1.3 and ISO 15118-20 over TLS 1.2 deviate from
  the standards and are named in a startup warning. A pinned ``"1.3"`` offers
  TLS 1.3 even without a vehicle chain.

DIN SPEC 70121 is never offered over TLS.

Configuration
-------------

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Option
     - Default
     - Description
   * - ``device``
     - ``auto``
     - Ethernet device used for high-level communication. ``auto`` picks the
       first interface with an IPv6 link-local address, and the module fails
       at startup when there is none. A named device that does not resolve at
       startup is kept with a warning; ``start_charging`` returns false while
       it is missing.
   * - ``evcc_id``
     - ``02:00:00:00:00:01``
     - EVCC identifier sent to the SECC, 1 to 255 characters. A MAC address and a
       vehicle identifier (``WMIV1234567890ABCDEX``) are both valid.
   * - ``response_timeout_ms``
     - ``0``
     - Response watchdog timeout in milliseconds. ``0`` uses the per-message
       ISO 15118 timeouts from the specification table.
   * - ``authorization_timeout_ms``
     - ``0``
     - Bound on the ISO 15118-20 authorization loop. The EV re-sends
       ``AuthorizationReq`` while the EVSE reports ``Ongoing`` and after a declined
       attempt, so the driver can authorize again, and stops the session once this
       bound expires. ``0`` uses ``V2G_EVCC_Ongoing_Timeout`` (60000 ms).
   * - ``control_mode``
     - ``Dynamic``
     - Preferred charge-loop control mode, ``Scheduled`` or ``Dynamic``.
       ISO 15118-20 only; ISO 15118-2 and DIN SPEC 70121 always run scheduled.
   * - ``supported_protocols``
     - ``""``
     - Comma-separated SAP offer in priority order, from ``ISO15118_20``,
       ``ISO15118_2`` and ``DIN70121``; see `Protocols`_.
   * - ``tls``
     - ``off``
     - ``off``, ``prefer`` or ``require``; see `Transport security`_.
   * - ``allow_pnc_without_tls``
     - ``false``
     - Accept the deviation from the standard and present a contract over a
       session with no transport security. ``false`` charges by EIM on a
       plaintext link instead, which is the correction ``EvseV2G`` already makes
       from the other end. ``true`` puts the contract chain, the eMAID and a
       contract-key signature on the wire in clear.
   * - ``tls_version``
     - ``auto``
     - ``auto``, ``"1.2"`` or ``"1.3"``, ignored when ``tls`` is ``off``; see
       `Transport security`_.
   * - ``verify_server_certificate``
     - ``true``
     - Verify the SECC chain against the V2G root.
   * - ``enable_tls_key_logging``
     - ``false``
     - Export the TLS session keys. Testing and simulation only.
   * - ``tls_key_logging_path``
     - ``/tmp``
     - Output directory for the TLS key log file.
   * - ``v2g_root_cert_path``
     - ``""``
     - V2G root certificate. Empty resolves to
       ``<etc>/certs/ca/v2g/V2G_ROOT_CA.pem``. Every certificate in the file is
       trusted, so under ``tls_version`` ``auto`` it must hold both the
       ISO 15118-2 and the ISO 15118-20 V2G roots. At certificate installation,
       the library derives the request root ID from the first PEM certificate.
       An unreadable file, missing PEM certificate, or unusable root ID is logged
       and stops the session before sending the request. The same path supplies
       the trust roots for CPS response verification.
   * - ``device_cert_chain_path``
     - ``""``
     - Vehicle certificate chain, leaf first. Empty resolves to
       ``<etc>/certs/client/vehicle/VEHICLE_CERT_CHAIN.pem``.
   * - ``device_key_path``
     - ``""``
     - Vehicle leaf private key. Empty resolves to
       ``<etc>/certs/client/vehicle/VEHICLE_LEAF.key``.
   * - ``device_key_password_path``
     - ``""``
     - File holding the vehicle leaf key password. Empty resolves to
       ``<etc>/certs/client/vehicle/VEHICLE_LEAF_PASSWORD.txt``; a missing or
       empty file means no password.
   * - ``iso2_ac_e_amount_wh``
     - ``60000``
     - ISO 15118-2 ``AC_EVChargeParameter`` EAmount in watt-hours when
       ``start_charging`` states none (see `Departure time and requested energy`_).
   * - ``iso2_ac_ev_max_voltage_v``
     - ``400``
     - ISO 15118-2 ``AC_EVChargeParameter`` EVMaxVoltage in volts (per line).
   * - ``iso2_ac_ev_max_current_a``
     - ``32``
     - ISO 15118-2 ``AC_EVChargeParameter`` EVMaxCurrent in amperes (per line).
   * - ``iso2_ac_ev_min_current_a``
     - ``10``
     - ISO 15118-2 ``AC_EVChargeParameter`` EVMinCurrent in amperes (per line).
   * - ``enable_pnc``
     - ``false``
     - Offer ISO 15118-2 Plug & Charge (Contract) next to EIM and prefer Contract
       whenever the SECC offers it. Requires TLS; without it the EV warns and
       falls back to EIM only, unless ``allow_pnc_without_tls`` says otherwise.
   * - ``pnc_contract_cert_chain_path``
     - ``""``
     - One PEM file holding the whole contract (MO) certificate chain, leaf
       first. Empty resolves to ``<etc>/certs/client/mo/MO_LEAF.pem`` followed by
       ``<etc>/certs/ca/mo/INTERMEDIATE_MO_CA_CERTS.pem``, the layout the repo PKI
       ships. The eMAID of PaymentDetails is read from the leaf CommonName.
   * - ``pnc_contract_key_path``
     - ``""``
     - Contract leaf private key; signs the PnC AuthorizationReq. Empty resolves
       to ``<etc>/certs/client/mo/MO_LEAF.key``.
   * - ``pnc_contract_key_password_path``
     - ``""``
     - File holding the contract leaf key password. Empty resolves to
       ``<etc>/certs/client/mo/MO_LEAF_PASSWORD.txt``.
   * - ``pnc_oem_prov_cert_path``
     - ``""``
     - OEM provisioning certificate; signs CertificateInstallationReq and holds
       the static ECDH key that decrypts the delivered contract key. Empty
       resolves to ``<etc>/certs/client/oem/OEM_LEAF.pem``.
   * - ``pnc_oem_prov_key_path``
     - ``""``
     - OEM provisioning private key. Empty resolves to
       ``<etc>/certs/client/oem/OEM_LEAF.key``.
   * - ``pnc_oem_prov_key_password_path``
     - ``""``
     - File holding the OEM provisioning key password. Empty resolves to
       ``<etc>/certs/client/oem/OEM_LEAF_PASSWORD.txt``.
   * - ``pnc_force_cert_install``
     - ``false``
     - Run a CertificateInstallation even when a contract certificate is
       configured. Requires the SECC to offer the Certificate service and its
       installation parameter set in ServiceDetail.
   * - ``pnc_contract_store_dir``
     - ``""``
     - Directory that keeps an installed contract as ``CONTRACT_CHAIN.pem``
       (leaf first), ``CONTRACT_LEAF.key`` and ``EMAID.txt``, each written to a
       temporary file of mode 0600 in the directory and renamed into place.
       Every session that can offer PnC reads it before starting and presents
       the stored contract when all three files are present, the key belongs to
       the leaf and the eMAID matches the leaf; otherwise it uses the configured
       contract material, with a warning unless the directory holds none of the
       files yet. An installed contract whose eMAID does not match its leaf is
       not stored. A failed store is logged and only the installing session,
       and its resume after a pause, presents the contract. The directory is
       synced after the renames; a failed sync is logged as a warning and the
       contract stays installed. Empty disables persistence. Give each Ev15118
       instance its own directory; two instances sharing one can mix their files.
   * - ``ac_phase_count``
     - ``3``
     - Number of AC lines the EV draws on, 1 or 3. Selects the connector. On a
       single-phase connector it divides the advertised totals below down to one
       line's share. A ``start_charging`` with ``AC_single_phase_core`` and 3
       here runs that session on one line, with a third of the charge power
       totals.
   * - ``ac_max_charge_power_w``
     - ``11040``
     - Advertised AC maximum charge power in watts, as a total across
       ``ac_phase_count`` lines. The default is 16 A x 230 V x 3.
   * - ``ac_min_charge_power_w``
     - ``4140``
     - Advertised AC minimum charge power in watts, as a total across
       ``ac_phase_count`` lines. The default is 6 A x 230 V x 3.
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
   * - ``ac_max_discharge_power_w``
     - ``11040``
     - Advertised AC maximum discharge power in watts (AC_BPT, AC_DER_IEC and
       AC_DER_SAE), as a total across ``ac_phase_count`` lines. The default is
       16 A x 230 V x 3.
   * - ``ac_min_discharge_power_w``
     - ``4140``
     - Advertised AC minimum discharge power in watts (AC_BPT, AC_DER_IEC and
       AC_DER_SAE), as a total across ``ac_phase_count`` lines. The default is
       6 A x 230 V x 3.
   * - ``dc_max_discharge_power_w``
     - ``150000``
     - Advertised DC maximum discharge power in watts (BPT).
   * - ``dc_min_discharge_power_w``
     - ``0``
     - Advertised DC minimum discharge power in watts (BPT).
   * - ``dc_max_discharge_current_a``
     - ``300``
     - Advertised DC maximum discharge current in amperes (BPT).
   * - ``sae_inverter_profile_path``
     - ``""``
     - JSON file describing the EV inverter for AC_DER_SAE (see
       `SAE inverter profile`_). Empty uses the built-in profile. A file that
       fails to parse makes the module fail at startup.
   * - ``cpd_rounds``
     - ``1``
     - ChargeParameterDiscovery rounds the EV drives before it reports
       ``Finished``, 1 to 65535. AC_DER_SAE only.
   * - ``der_stop_on_invalid_control``
     - ``false``
     - Stop the session on a structurally invalid DER control block from the
       SECC instead of warning and continuing. AC_DER_SAE only.

.. _everest_modules_handwritten_Ev15118_pyevjosev:

Configurations written for PyEvJosev
-------------------------------------

Ev15118 replaces the PyEvJosev EV module. A ``config_module`` written for
PyEvJosev maps onto Ev15118 as follows. A key with no Ev15118 equivalent is
logged by the manager as an unknown config entry and ignored, so remove it.

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - PyEvJosev key
     - Ev15118
   * - ``device``
     - Same key, same meaning.
   * - ``tls_active``, ``enforce_tls``, ``enable_tls_1_3``
     - ``tls: prefer`` for ``tls_active`` and ``tls: require`` for
       ``enforce_tls``. For ``enable_tls_1_3``, whichever its value, use
       ``tls_version: auto`` (the default, recommended): ISO 15118-20 runs over
       TLS 1.3 and ISO 15118-2 over TLS 1.2. Set ``"1.2"`` only for
       ``enable_tls_1_3: false`` in a configuration without ``ISO15118_20``
       that wants Josev's exact pinning.
       This mapping deliberately differs from Josev's version pinning.
   * - ``supported_ISO15118_20_AC``, ``supported_ISO15118_20_DC``,
       ``supported_ISO15118_2``, ``supported_DIN70121``
     - ``supported_protocols``, listing ``ISO15118_20`` when either -20 key was
       ``true`` and ``ISO15118_2`` or ``DIN70121`` when its key was ``true``,
       in the order to offer them. Ev15118 has one entry for ISO 15118-20; the
       session's energy transfer mode, AC or DC, comes from ``start_charging``.
   * - ``supported_d20_energy_services``
     - None. The service is the energy transfer mode of ``start_charging``,
       which ``EvManager`` takes from the first argument of its
       ``iso_start_v2g_session`` command (``DC``, ``DC_BPT``, ``AC``,
       ``AC_BPT`` and so on), not a priority list on the EV module.
   * - ``is_cert_install_needed``
     - ``pnc_force_cert_install``, together with ``enable_pnc`` and TLS;
       certificate installation is ISO 15118-2 only.
   * - ``is_internet_service_needed``, ``request_all_service_details``,
       ``select_all_vas_services``
     - None. Ev15118 selects no value-added services.

``supported_protocols`` has no usable default; the module refuses to start
until it lists at least one protocol. A migrated configuration offers
ISO 15118-20 only when it lists ``ISO15118_20``.

DER control function negotiation
--------------------------------

For the AC DER IEC service the EV declares which IEC DER control functions it
supports through the ``der_*`` options above; these form a bitmask carried in the
ServiceDetail exchange. Among the SECC's Dynamic parameter sets the EV selects
the first whose declared functions are a subset of the supported set (a set with
no ``DERControlFunctions`` parameter counts as compatible). A mask carrying bits
above the functions the EV models names functions it cannot honor, so such a set
is not a subset even when its remaining bits are supported. When no offered set
fits, ``der_stop_on_unsupported_functions`` decides the outcome: ``true`` stops
the session cleanly, while ``false`` proceeds with the first Dynamic set and warns
about the unsupported functions. At runtime, a DSO setpoint (Q or cos phi) that
arrives in a charge-loop response without the matching function having been
negotiated is dropped before the directive is surfaced; all other fields pass
through unchanged.

For the AC DER SAE service (ISO 15118-20 AMD1 Annex M) the EV describes its
inverter in the ChargeParameterDiscovery request; the SECC enables functions in
its responses and the EV acknowledges them.

SAE inverter profile
--------------------

The file named by ``sae_inverter_profile_path`` is one JSON object. Every key is
optional and keeps its built-in default when absent; an unknown or repeated key,
a wrong JSON type, an out-of-range number or an illegal enum string fails the
parse. The parsed profile, absent keys at their defaults, is then checked with
libiso15118's ``validate_sae_profile`` (voltage window, frequency, power factors,
identity lengths, supported modes and totals); a problem fails the module at
startup, naming the key. Powers, var, VA and siemens values are totals across
``ac_phase_count`` lines.

- Identity strings, at most 32 bytes: ``inverter_sw_version``,
  ``inverter_hw_version`` (absent by default), ``inverter_manufacturer``,
  ``inverter_model``, ``inverter_serial_number``.
- ``supported_modes``: array of SAE function names, which must include
  ``"charge"`` and ``"discharge"``. The names are those of libiso15118's
  ``sae::SAE_FUNCTION_NAMES``, one per AMD1 Table M.6 bit: ``charge``,
  ``discharge``, ``enter service``, ``constant power factor under excited``,
  ``constant power factor over excited``, ``constant var``, ``constant watt``,
  ``frequency droop``, ``over frequency may trip curve``,
  ``over frequency must trip curve``, ``over voltage may trip curve``,
  ``over voltage momentary cessation trip curve``,
  ``over voltage must trip curve``, ``under frequency may trip curve``,
  ``under frequency must trip curve``, ``under voltage may trip curve``,
  ``under voltage momentary cessation trip curve``,
  ``under voltage must trip curve``, ``limit maximum discharge power``,
  ``evse target reactive power``, ``evse target active power``, ``volt var``,
  ``volt watt`` and ``watt var``.
- Numbers, unit in the key suffix: ``max_apparent_power_{charging,discharging}_var_{absorption,injection}_va``,
  ``max_var_{absorption,injection}_{charging,discharging}_var``,
  ``reactive_susceptance_s``, ``over_excited_discharge_power_w``,
  ``under_excited_discharge_power_w``, ``nominal_voltage_v``,
  ``maximum_voltage_v``, ``minimum_voltage_v``, ``nominal_voltage_offset_v``,
  ``nominal_frequency_hz``; and the ratios ``over_excited_power_factor`` and
  ``under_excited_power_factor``, in (0, 1].
- Unsigned integers: ``useable_watt_hours``, ``minimum_charging_duration_s``,
  ``duration_maximum_charge_rate_s``, ``duration_maximum_discharge_rate_s``, and
  ``j3072_certification_date`` in seconds since the Unix epoch.
- ``j3072_certified``: boolean.
- Enum strings: ``ieee1547_normal_category`` (``CategoryA``, ``CategoryB``),
  ``ieee1547_abnormal_category`` (``CategoryI``, ``CategoryII``,
  ``CategoryIII``), ``operational_state`` (``On``, ``Off``),
  ``connection_status`` (``Connected``, ``Disconnected``).

Published DER variables
-----------------------

- ``der_negotiated_functions``: the negotiated functions, with the ``flavor``
  (``AC_DER_IEC`` or ``AC_DER_SAE``). For AC_DER_IEC this is the demand of the
  selected parameter set in ``iec_functions`` (``DerIecFunction``), published
  once from ServiceDetail; without ``der_stop_on_unsupported_functions`` the
  fallback set can demand functions the EV does not support. For AC_DER_SAE it
  is the enabled modes the EV echoes in ``sae_functions`` (``DerSaeFunction``),
  published every ChargeParameterDiscovery round and when the charge loop
  changes the set.
- ``der_control_received``: a summary of each DER control block the SECC sends,
  with the ``flavor`` and the ``source`` (``ChargeParameterDiscovery`` or
  ``ChargeLoop``). SAE blocks carry ``permit_service``, ``enabled_functions``
  (``DerSaeFunction``) and any structural ``problems``; a block that stops the
  session under ``der_stop_on_invalid_control`` is still published, with its
  problems, before the session ends. IEC Dynamic charge loop blocks carry the DSO Q and cos phi
  setpoints when present; the first of a session is published, and a later one
  only when its setpoints change. IEC Scheduled charge loop blocks are not published.
- ``ac_evse_target_power``: the Dynamic charge loop targets, for AC_DER_IEC and
  AC_DER_SAE as for plain AC. AC_DER_IEC in Scheduled mode does not publish
  ``ac_evse_target_power``.

``update_present_values`` also takes ``present_frequency`` in Hz and
``der_alarm_status`` (AMD1 Table M.9), both for AC_DER_SAE, where the voltage is
the measured grid voltage. ``start_charging`` clears the voltage, active power,
frequency and DER alarm status, so values reported before it are discarded; a
voltage or frequency not reported since falls back to the profile's nominal
value.

Departure time and requested energy
-----------------------------------

``start_charging`` takes ``DepartureTime`` in seconds from the call and
``EAmount`` in kWh. Both must be finite and not negative; 0 states nothing.

An ``EAmount`` above 0 is the session's energy request, in Wh on the wire: the
ISO 15118-2 AC ``EAmount``, the ISO 15118-2 DC and DIN SPEC 70121
``EVEnergyRequest``, the ISO 15118-20 Dynamic ScheduleExchange and Dynamic
charge loop target and maximum energy, and the Scheduled ScheduleExchange
``EVTargetEnergyRequest``. The minimum energy is always 0. With ``EAmount`` 0,
optional fields are left out and mandatory ones fall back: ISO 15118-2 AC to
``iso2_ac_e_amount_wh``, the ISO 15118-20 Dynamic fields to the
``energy_capacity`` of ``set_dc_params``, whatever the energy service.
ISO 15118-2 caps ``EAmount`` and ``EVEnergyRequest`` at 200000 Wh
([V2G2-832]), the configured fallback included; DIN SPEC 70121 caps
``EVEnergyRequest`` at 32767000 Wh, the largest value its PhysicalValueType
carries. A capped value is sent at the maximum and logged as a warning once per
session. ISO 15118-20 has no cap.

A ``DepartureTime`` above 0 is sent as the seconds left until departure, counting
down, in the ISO 15118-2 AC and DC ChargeParameterDiscovery, the ISO 15118-20
ScheduleExchange of either control mode and the ISO 15118-20 Dynamic charge
loops. ISO 15118-2 sends 0 once the departure has passed. ISO 15118-20 requires
a DepartureTime above 0 ([V2G20-2103]), so a departure that is not stated or has
passed is left out where optional, and the mandatory Dynamic ScheduleExchange
DepartureTime is sent as 4294967295, no deadline.

Limitations
-----------

The implementation has a deliberately narrow scope:

- **The requested energy is static.** The energy request stays what
  ``start_charging`` stated for the whole session; energy already delivered is
  not subtracted from it (see `Departure time and requested energy`_).
- **DC, MCS and AC only.** ``start_charging`` accepts DC, DC BPT, MCS, MCS BPT,
  AC single/three-phase, AC BPT, AC DER IEC and AC DER SAE energy-transfer modes; WPT sessions
  are not supported. MCS is the megawatt DC service of ISO 15118-20: the same DC
  parameter discovery, cable check, pre-charge and charge loop under a different
  service id, so it rides the DC states and has no ISO 15118-2 or DIN equivalent.
- **DER functions are never executed.** Received DER control blocks, except IEC
  Scheduled ones, are published as summaries; acting on them is the consumer's
  job. DER curves dictated in ``DER_AC_ChargeParameterDiscoveryRes`` are logged,
  and the summary carries none of their values. The three-phase inverter relay
  use case is assumed.
- **AC DER SAE is Dynamic only.** A preferred Scheduled control mode is ignored
  for that service.
- **Plug & Charge is ISO 15118-2 only.** ISO 15118-20 authorization is EIM only.
  An installed contract is persisted only to ``pnc_contract_store_dir``, not to
  ``EvseSecurity``.
- **Dynamic BPT only.** BPT is negotiated in Dynamic control mode; reverse power
  flow follows SECC targets. SAE J2847/2 (``enable_sae_j2847_v2g_v2h``) is not
  implemented.

Threading
---------

A single worker thread, started from ``ready()``, runs one V2G session at a time.
``start_charging`` hands a request to the worker; the worker constructs the
``ev::Controller`` and runs its event loop until the session ends, then waits for
the next request. Session state is guarded by a ``monitor`` so command handlers
and the worker coordinate safely, including during module teardown.

``start_charging`` returns false, and starts nothing, when the worker is not
running, a session is active, the energy transfer mode is unsupported, the HLC
interface is missing, or the session configuration is invalid. An invalid
configuration includes a protocol offer left empty by the requested service or
transport, for example DIN SPEC 70121 alone for an AC mode or over TLS, and a DC
or MCS mode before ``set_dc_params`` gave a positive maximum power, current and
voltage.

``set_dc_params`` merges the fields it carries into the stored values; a field
it leaves out keeps its previous value. The merged values are validated as a
whole: an invalid update is logged and rejected, and neither stored nor pushed
to a running session. ``set_fault`` is not supported and logs a warning. A
session that ends publishes ``ev_power_ready`` false before
``v2g_session_finished``. A session that fails to start after
``start_charging`` returned true still ends with ``v2g_session_finished``.

``pause_charging`` ends the session with ``SessionStop(Pause)``; the paused
session is stored and handed to the next ``ev::Controller`` as
``ev::config::EvConfig::resume``. ``stop_charging`` and ``abort_charging`` discard it.
A stop from either side outranks a pause: when the charger terminates while a pause
is pending, or ``stop_charging`` arrives before the paused ``SessionStop`` is sent,
the session terminates and nothing is kept for a resume.
In ISO 15118-20 a pause requested before ``PowerDelivery(Start)`` terminates the
session as well [V2G20-2644].
``cp_state_changed`` is latched and replayed to every new controller, and the
first report switches the CP-dependent checks (the DC cable check) on. E or F
during a session terminates it without SessionStop, as ``abort_charging`` does
[V2G2-737], and cancels a session that was accepted but has not started yet.

For ISO 15118-2 (AC and DC), DIN SPEC 70121 (DC), and ISO 15118-20 (DC and MCS), an accepted
PowerDelivery(Stop) response publishes ``power_delivery_stopped`` before
WeldingDetection or SessionStop. The EV may then open its contactor and set CP B
while V2G shutdown continues. ISO 15118-20 AC does not publish this event.

For ISO 15118-2 AC, ``ac_evse_max_current`` publishes the EVSEMaxCurrent offered
in ChargeParameterDiscovery and reported in each ChargingStatus response that
carries it. ISO 15118-20 offers AC power, not current, and does not publish it.

DC welding detection polls until the reported voltage is strictly below 60 V.
If voltage remains unsafe for 20 accepted responses or 10 seconds, Ev15118 logs
and publishes ``welding_detection_failed`` with the response count as ``cycles``
and the last reported voltage as ``last_voltage_V``, left out when no response
was accepted, then attempts SessionStop with termination,
even when a pause was requested. DIN has no pause field on the wire; a failed
welding check prevents retaining its session for resume. ISO 15118-20 closes both
safe and failed checks with Processing Finished, then sends SessionStop after
its response. Each request waits for the configured message timeout or the
protocol's message timeout when none is configured. The welding deadline does
not extend an outstanding request's response deadline. A safe answer received
after the welding deadline still counts as safe and preserves a requested pause.
A missing response ends the session without SessionStop. In ISO 15118-2 and DIN,
it also reports welding failure. In ISO 15118-20, it reports welding failure
only before the Processing Finished request. A missing response to that request
leaves the settled result unchanged: a safe check reports no failure, and a
failed check is reported only once.
