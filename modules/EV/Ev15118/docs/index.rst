.. _everest_modules_handwritten_Ev15118:

Ev15118
=======

EV-side ISO 15118 EVCC built on the ``libiso15118`` ``ev::Controller``. It
provides the ``ISO15118_ev`` interface (the same interface as ``PyEvJosev``), so
it is a drop-in software-in-the-loop (SIL) replacement for the Python EVCC when
driving a DC or AC session against a SECC such as ``Evse15118D20`` or
``EvseV2G``.

Protocols
---------

The SAP offer carries ISO 15118-20 first, then ISO 15118-2 and DIN SPEC 70121
when ``supported_ISO15118_2`` / ``supported_DIN70121`` are set:

- **ISO 15118-20** (AC, AC BPT, AC DER IEC, DC, DC BPT), EIM, plaintext or
  TLS 1.3 with a client certificate.
- **ISO 15118-2** (AC and DC), EIM or Plug & Charge (Contract). PnC needs TLS
  [V2G2-632]: with ``enable_pnc`` and TLS the EV offers ``PnC`` and ``EIM``, and
  presents the configured contract certificate in PaymentDetails; without a
  contract certificate (or with ``pnc_force_cert_install``) it runs a
  CertificateInstallation with the OEM provisioning certificate first. TLS 1.2 is
  used for -2, so ``enable_tls_1_3`` must be ``false``; the module warns at
  startup when it is set together with ``supported_ISO15118_2`` and TLS. An
  installed contract is logged, not persisted.
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

The session runs over plain TCP or TLS (``tls_active`` / ``enforce_tls``). The
AC DER IEC service is also negotiated (assuming a three-phase inverter relay);
received DER directives are logged only, as ``ISO15118_ev`` has no DER variable
to publish them on.

Bidirectional power transfer (BPT) is negotiated for both AC_BPT and DC_BPT in
Dynamic control mode. The advertised discharge limits come from the
``*_discharge_*`` config settings; ``set_bpt_dc_params`` overrides the
configured DC discharge power and current for the next session (parameters are
snapshotted when a session starts). AC_BPT assumes a three-phase inverter relay.
Reverse power flow itself is driven entirely by the SECC's target-power
directives; the EVSE's advertised discharge limits are logged for visibility.

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
     - EVCC identifier sent to the SECC, 1 to 255 characters. A MAC address and a
       vehicle identifier (``WMIV1234567890ABCDEX``) are both valid.
   * - ``response_timeout_ms``
     - ``0``
     - Response watchdog timeout in milliseconds. ``0`` uses the per-message
       ISO 15118 timeouts from the specification table.
   * - ``d20_control_mode``
     - ``Dynamic``
     - Preferred ISO 15118-20 charge-loop control mode, ``Scheduled`` or
       ``Dynamic``.
   * - ``supported_DIN70121``
     - ``false``
     - Offer DIN SPEC 70121 in the SAP handshake, after ISO 15118-20.
   * - ``supported_ISO15118_2``
     - ``false``
     - Offer ISO 15118-2 in the SAP handshake, after ISO 15118-20.
   * - ``tls_active``
     - ``false``
     - Request TLS in the SDP request and connect with TLS when offered.
   * - ``enforce_tls``
     - ``false``
     - Reject an SDP response without TLS. Implies ``tls_active``.
   * - ``enable_tls_1_3``
     - ``false``
     - TLS 1.3 with a client certificate. ``false`` uses TLS 1.2 without one.
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
       ``<etc>/certs/ca/v2g/V2G_ROOT_CA.pem``.
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
     - ISO 15118-2 ``AC_EVChargeParameter`` EAmount in watt-hours.
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
       falls back to EIM only.
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
       configured. Requires the SECC to offer the Certificate service.
   * - ``ac_phase_count``
     - ``3``
     - Number of AC lines the EV draws on, 1 or 3. Selects the connector and
       divides the advertised totals below into per-line values.
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
     - Advertised AC maximum discharge power in watts (AC_BPT and AC_DER_IEC),
       as a three-phase total. The default is 16 A x 230 V x 3.
   * - ``ac_min_discharge_power_w``
     - ``4140``
     - Advertised AC minimum discharge power in watts (AC_BPT and AC_DER_IEC),
       as a three-phase total. The default is 6 A x 230 V x 3.
   * - ``dc_max_discharge_power_w``
     - ``150000``
     - Advertised DC maximum discharge power in watts (BPT).
   * - ``dc_min_discharge_power_w``
     - ``0``
     - Advertised DC minimum discharge power in watts (BPT).
   * - ``dc_max_discharge_current_a``
     - ``300``
     - Advertised DC maximum discharge current in amperes (BPT).

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

Limitations
-----------

The implementation has a deliberately narrow scope:

- **DC and AC only.** ``start_charging`` accepts DC, DC BPT, AC single/three-phase,
  AC BPT, and AC DER IEC energy-transfer modes; WPT and MCS sessions are not
  supported.
- **DER directives are log-only.** AC DER IEC directives (target active power,
  DSO Q and cos phi setpoints) are logged, not published, pending an interface
  variable. The three-phase inverter relay use case is assumed.
- **DER curves are log-only.** DER curves dictated in
  ``DER_AC_ChargeParameterDiscoveryRes`` are logged, not applied; the EV negotiates
  the functions and observes what the SECC dictates.
- **Plug & Charge is ISO 15118-2 only.** ISO 15118-20 authorization is EIM only,
  and an installed contract is logged rather than persisted to ``EvseSecurity``.
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

``pause_charging`` ends the session with ``SessionStop(Pause)``; the paused
session is stored and handed to the next ``ev::Controller`` as
``EvConfig::resume``. ``stop_charging`` and ``abort_charging`` discard it.
``cp_state_changed`` is latched and replayed to every new controller, and the
first report switches the CP-dependent checks (the DC cable check) on.
