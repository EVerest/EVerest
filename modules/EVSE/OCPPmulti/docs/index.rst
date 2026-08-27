.. _everest_modules_handwritten_OCPPmulti:

.. *******************************************
.. OCPPmulti
.. *******************************************

This module implements and integrates OCPP within EVerest. It supports three protocol versions in a single module:
OCPP 1.6, OCPP 2.0.1 and OCPP 2.1. A connection to a Charging Station Management System (CSMS) can be established by
loading this module as part of the EVerest configuration. The module leverages libocpp, EVerest's OCPP library.

OCPPmulti is the recommended OCPP module for new EVerest configurations. It deprecates the separate
:ref:`OCPP <everest_modules_OCPP>` (OCPP 1.6) and :ref:`OCPP201 <everest_modules_OCPP201>` (OCPP 2.0.1 / 2.1)
modules.

.. warning::

   This module is currently **experimental**: configuration parameters and its
   integration in EVerest may change without further notice. It is exempt from
   the stability guarantees and the deprecation period of the EVerest public
   API until promoted to stable (see :ref:`project-experimental-components`).

In this document, **OCPP 2.x** refers to OCPP 2.0.1 and OCPP 2.1 collectively.

Selecting the OCPP version
==========================

The protocol generation (OCPP 1.6 or OCPP 2.x) is selected by the module configuration parameter ``Mode``:

* ``Only1.6``: run OCPP 1.6.
* ``Only2``: run OCPP 2.x (the default).
* ``Prefer1.6`` / ``Prefer2``: currently behave exactly like ``Only1.6`` / ``Only2``. They are reserved for a
  possible future automatic fallback to the other protocol generation, which is not implemented yet.

The module starts the charge point stack of the selected generation at initialization; the generation is not
switched at runtime.

Within OCPP 2.x, the concrete version (2.0.1 or 2.1) is negotiated with the CSMS during the initial websocket
handshake: the charging station offers the versions as OCPP subprotocols in the ``Sec-WebSocket-Protocol`` header,
based on the device model variable **SupportedOcppVersions** of the **InternalCtrlr** component (a comma-separated
list of ``ocpp2.1`` and ``ocpp2.0.1`` in order of preference; by default both are offered). The CSMS selects one of
the offered versions and reports it in the handshake response, following the OCPP 2.1 Part 4 (JSON over WebSockets)
implementation guide.

📌 **Note:** It is planned that a device model variable listing the supported versions eventually replaces the
``Mode`` parameter. The idea is that all protocol versions, including OCPP 1.6 in combination with any OCPP 2.x
version, can then be advertised during the handshake, but only for a configurable timeout: if no initial websocket
connection succeeds within that time, the charging station falls back to either OCPP 1.6 or OCPP 2.x. This fallback
is required because authorization and offline behavior differ between the protocol generations, so the charging
station must settle on one of them to stay operational while no connection to the CSMS can be established.

Module configuration
====================

All configuration parameters of this module, including their defaults and descriptions, are listed in the generated
reference section of this page (taken from the module's ``manifest.yaml``). The OCPP 1.6-only parameters
(``ChargePointConfigPath``, ``UserConfigPath``, ``EnableLegacyConfigMigration``, ``DeviceModelConfigMappings``,
``Ocpp16NetworkConfigSlot``) are described in context in the OCPP 1.6 configuration section below.

Renamed configuration keys
^^^^^^^^^^^^^^^^^^^^^^^^^^

Compared to the deprecated :ref:`OCPP <everest_modules_OCPP>` (1.6) module, two keys were renamed (the
:ref:`OCPP201 <everest_modules_OCPP201>` module already uses the new names):

* ``PublishChargingScheduleIntervalS`` is now ``CompositeScheduleIntervalS``
* ``PublishChargingScheduleDurationS`` is now ``RequestCompositeScheduleDurationS``

External websocket control via MQTT
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If ``EnableExternalWebsocketControl`` is set to ``true``, the connection to the CSMS can be controlled externally via
these MQTT topics (the message payload is ignored):

* ``everest_api/ocpp/cmd/connect``: connect the OCPP websocket to the CSMS
* ``everest_api/ocpp/cmd/disconnect``: disconnect the OCPP websocket from the CSMS

This is intended for debug and testing purposes.

Device model configuration via component configs
=================================================

The device model is the primary entry point for configuring the OCPP stack of this module. It is built from component
config JSON files located in the directory referenced by ``DeviceModelConfigPath`` (a relative path is resolved
against the ``OCPP201`` module's share directory, i.e. ``<prefix>/share/everest/modules/OCPP201/``, where the shipped
component configs are installed). The directory contains two subdirectories:

* ``standardized``: components and variables standardized by the OCPP specification. Usually untouched by
  integrators.
* ``custom``: components that are custom for a specific charging station. This is where integrators add or override
  components and variables, especially the EVSE and Connector components matching the physical setup.

The component configs shipped with EVerest are installed from ``lib/everest/ocpp/config/common/component_config/``.

Each JSON file describes one component and all of its variables:

* ``name``: the component name
* ``properties``: one entry per variable, each with

  * ``variable_name``: the OCPP variable name
  * ``characteristics``: ``dataType``, optional ``unit``, ``supportsMonitoring``, optional limits
    (``minLimit``/``maxLimit``, ``valuesList``)
  * ``attributes``: a list of attribute objects with ``type`` (``Actual``, ``Target``, ``MinSet``, ``MaxSet``),
    ``mutability`` (``ReadOnly``, ``WriteOnly``, ``ReadWrite``) and an optional ``value``

* ``required``: the list of variables that must be present

To configure a variable, specify the ``value`` of the attribute type you want to configure. Variables without a
configured ``value`` are only defined; their values are set at runtime. See the
:ref:`combined-module tutorial <tutorial-ocpp-combined>` for a worked component-config example.

On the first initialization, the component configs seed the SQLite device model database at
``DeviceModelDatabasePath``, using the schema migrations from ``DeviceModelDatabaseMigrationPath``. Changes made by
the CSMS at runtime are persisted in this database. To add a custom component, add another JSON config file; it is
applied and reported automatically.

OCPP 1.6 configuration: two supported paths
===========================================

Unlike the deprecated :ref:`OCPP <everest_modules_OCPP>` module, OCPP 1.6 in this module is backed by the device
model as well. Two configuration paths are supported:

**(a) Legacy OCPP 1.6 JSON config.** The classic JSON configuration (``ChargePointConfigPath``, overlaid by
``UserConfigPath``) can still be used. It enters the device model via a one-time migration that is enabled by setting
``EnableLegacyConfigMigration`` to ``true``: on the first startup, while the device model database does not yet
exist, the legacy JSON (including the user config overlay) is migrated into the device model database. Once the
database is initialized, the migration is skipped and the legacy JSON is not read anymore.
``Ocpp16NetworkConfigSlot`` selects the NetworkConfiguration slot the OCPP 1.6 connection details (CentralSystemURI,
SecurityProfile, AuthorizationKey, HostName, ChargePointId) are migrated to; existing attribute values in the target
slot are overwritten, and ``0`` skips the migration of network connection details entirely.

**(b) Device model only.** OCPP 1.6 is configurable completely through the device model: with
``EnableLegacyConfigMigration`` left at ``false`` (the default), the device model is initialized only from the
component configs (``DeviceModelConfigPath``) and no legacy JSON is required. The OCPP 1.6 configuration keys are
served from the device model at runtime.

In both paths, the built-in mappings from OCPP 1.6 configuration keys to device model component/variable
combinations are documented in
`lib/everest/ocpp/config/v16_to_v2_mapping.md <https://github.com/EVerest/everest-core/blob/main/lib/everest/ocpp/config/v16_to_v2_mapping.md>`_.
``DeviceModelConfigMappings`` can point to a YAML file with custom mappings on top; it is applied both when migrating
the legacy JSON into the device model and when serving the device-model-backed configuration at runtime. Custom
mappings must not target the connection configuration (the ``NetworkConfiguration`` slots, the ``OCPPCommCtrlr``
network profile selectors, or the ``SecurityCtrlr`` connection fallbacks ``Identity`` and ``BasicAuthPassword``):
those are managed via the standardized OCPP 1.6 keys with their reboot/reconnect semantics, and a mapping file
containing such a target is rejected at startup. OCPP 1.6
keys without a standardized device model counterpart are kept in a dedicated ``OCPP16LegacyCtrlr`` component, whose
``NumberOfConnectors`` value is automatically patched to the actual number of connected EVSEs.

.. _handwritten_ocppmulti_network-profiles-ocpp16:

Network connection profiles (OCPP 1.6)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In device-model-backed configuration, OCPP 1.6 sources its network connection profiles from the same per-slot
``NetworkConfiguration_<N>`` components as OCPP 2.x rather than from the single-URL legacy keys. The slot model,
the failover behavior, the runtime reconfiguration workflow and the few points where the two protocol versions
behave differently are described once in
:ref:`Network connection configuration <handwritten_ocppmulti_network-connection-configuration>`; what follows
here is only what is specific to OCPP 1.6.

``Ocpp16NetworkConfigSlot`` selects the slot the legacy migration writes into (default ``1``). It populates that one
slot (``CentralSystemURI`` → ``OcppCsmsUrl``, ``SecurityProfile`` → ``SecurityProfile``, ``AuthorizationKey`` →
``BasicAuthPassword``, ``HostName`` → ``HostName``, ``ChargePointId`` → ``Identity``), sets that slot's
``OcppInterface`` to ``"Any"`` unless the component config sets an explicit attribute ``value`` (a ``default``,
like slot 1's ``"Wired0"``, does not count and applies only when the slot is not migrated into), and points
``OCPPCommCtrlr/ActiveNetworkProfile`` at the slot. The target slot must be defined by the component configuration:
when no ``NetworkConfiguration_<N>`` component config exists for it, the network connection migration is skipped
with an error log, and when the slot is missing from ``NetworkConfigurationPriority`` (or its ``valuesList``), a
warning is logged and the migrated profile is not used until the priority is configured accordingly. The shipped
example configs cover the default slot ``1``, so a migrated legacy deployment connects as before with a single
profile on interface ``"Any"``; for any other slot, ship the matching component config and priority.

The migration also initializes the *confirmed* security profile (``SecurityCtrlr``/``SecurityProfile``) from the
legacy config's ``SecurityProfile``. Setups configured purely through the device model (no migration) that use
security profile ``0`` must set ``SecurityCtrlr``/``SecurityProfile`` to ``0`` in their component config, since the
shipped default is ``1`` and every profile below the confirmed one is pruned from the failover list.

A slot whose ``NetworkConfiguration_<N>.OcppVersion`` names any version other than ``"OCPP16"`` is ignored in OCPP
1.6 mode; leaving ``OcppVersion`` unset or setting it to ``"OCPP16"`` makes the slot usable. Runtime writes of
``OcppVersion`` are validated against the variable's ``valuesList``, so a custom component config must list
``OCPP16`` there for such a write to be accepted (the shipped slot configs do). The *active* slot is exempt from
this filter: when every listed slot is version-filtered (typically because the device model database was
previously used with OCPP 2.x, which writes back the negotiated version; OCPP 1.6 itself never writes
``OcppVersion``), the active slot still connects using the legacy single-profile connection settings instead of
leaving the charge point unable to connect. The same fallback applies when the active slot's profile is
incomplete; in OCPP 2.x an incomplete slot is simply skipped.

The ``interface_address`` returned by the ``system`` provider's **configure_network** *replaces* the static
``Internal``/``IFace`` configuration key when binding the websocket - including clearing it when the provider
answers ``Ready`` or ``NotSupported`` without an address. Since this module always performs the configure_network
round-trip, ``IFace`` is effectively not used on successful attempts.

The legacy JSON configuration backend (OCPP 1.6 without a device model, as used by the ``OCPP`` module) is
unaffected by this and keeps the previous single-profile behavior.

OCPP device model vs. EVerest device model
==========================================

The module composes two device model storages behind libocpp's ``DeviceModelStorageInterface``:

* The **OCPP device model** (source id ``OCPP``): the integrator-facing configuration described above. It is defined
  by the component config JSON files and backed by libocpp's SQLite storage (``DeviceModelDatabasePath``). This is
  the storage integrators configure.
* The **EVerest device model** (source id ``EVEREST``): runtime telemetry that is closely related to EVerest and
  therefore not owned and managed by libocpp. Examples: ``Available`` and ``AvailabilityState`` of the EVSE and
  Connector components, ``Power`` of the EVSE component, and ``Available``, ``VehicleId`` and ``ProtocolAgreed`` of
  the ``ConnectedEV`` component. It is persisted in ``EverestDeviceModelDatabasePath`` and populated by the module at
  runtime, e.g. from power meter values and EV information reported by the connected EVSE managers.

The ``ComposedDeviceModelStorage`` routes each variable access to its owning storage by the variable's ``source``
field, which defaults to ``OCPP``.

The composed storage backs **both** protocol paths: in OCPP 2.x it is the device model of the libocpp
``ChargePoint``, and in OCPP 1.6 it backs the device-model-based charge point configuration. Variables of both
sources are therefore visible and settable through the EVerest ``ocpp`` interface (``get_variables`` /
``set_variables``, e.g. via the ``ocpp_consumer_API`` module) regardless of the active protocol version. Note that
an OCPP 1.6 **CSMS** still cannot address EVerest device model variables directly: ``GetConfiguration`` /
``ChangeConfiguration`` are key-based, and only keys with a device model mapping (built-in or via
``DeviceModelConfigMappings``) reach the device model. Also note that variables of source ``EVEREST`` are updated
by the module at runtime directly in the ``EverestDeviceModelStorage``; such updates do not trigger
``monitor_variables`` events on the ``ocpp`` interface (in either protocol version) — only writes performed through
the OCPP stack or the ``ocpp`` interface do. If a component/variable is defined in both storages, the EVerest
device model takes precedence for that variable and a warning is logged at startup; the integrator is
responsible for avoiding such overlaps.

.. mermaid::

   classDiagram
       class DeviceModel
       class DeviceModelStorageInterface {
           <<interface>>
       }
       class ComposedDeviceModelStorage
       class DeviceModelStorageSqlite
       class EverestDeviceModelStorage
       DeviceModel --> DeviceModelStorageInterface : uses
       DeviceModelStorageInterface <|.. ComposedDeviceModelStorage
       DeviceModelStorageInterface <|.. DeviceModelStorageSqlite
       DeviceModelStorageInterface <|.. EverestDeviceModelStorage
       ComposedDeviceModelStorage o-- DeviceModelStorageSqlite : source OCPP
       ComposedDeviceModelStorage o-- EverestDeviceModelStorage : source EVEREST

A ``SetVariables.req`` from the CSMS is routed as follows:

.. mermaid::

   sequenceDiagram
       participant CSMS
       participant CP as libocpp ChargePoint
       participant Composed as ComposedDeviceModelStorage
       participant Sqlite as DeviceModelStorageSqlite
       participant Everest as EverestDeviceModelStorage

       CSMS->>CP: SetVariables.req
       CP->>Composed: set_variable_attribute_value(component, variable, value)
       Composed->>Composed: get_variable_source(component, variable)
       alt source is OCPP (default)
           Composed->>Sqlite: set_variable_attribute_value(...)
           Sqlite-->>Composed: SetVariableStatus
       else source is EVEREST
           Composed->>Everest: set_variable_attribute_value(...)
           Everest-->>Composed: SetVariableStatus
       end
       Composed-->>CP: SetVariableStatus
       CP-->>CSMS: SetVariables.conf

Integration in EVerest
======================

Libocpp fulfills a large amount of protocol requirements internally, but OCPP affects, controls, and monitors many
areas of a charging station's operation. The integration of libocpp with the other parts of EVerest is done by this
module via the interfaces it provides and requires.

📌 **Note:** The ``ocpp_1_6_charge_point`` interface (``main`` implementation) of the deprecated
:ref:`OCPP <everest_modules_OCPP>` module is NOT provided by this module. The ``ocpp_generic`` interface
covers the same functionality.

.. _handwritten_ocppmulti_control-from-outside-everest:

Control from outside EVerest
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The interfaces below are EVerest-internal: they are consumed by other modules
in the same EVerest configuration. To drive OCPPmulti from outside EVerest -
a web interface, a commissioning tool, a factory test rig, a vendor cloud
agent - add the :ref:`ocpp_consumer_API
<everest_modules_handwritten_ocpp_consumer_API>` module to the configuration.
It requires OCPPmulti's ``ocpp`` interface and republishes it on the EVerest
API MQTT surface, so the OCPP stack is controllable without writing an EVerest
module: device model access (and with it the whole configuration surface
described under `Configuration access`_), DataTransfer, and the stack state
OCPPmulti publishes. Its documentation and the generated API reference it
links to are authoritative for the exact commands, variables and payloads.

The API is a trusted local integrator channel and is not rate-limited or
authenticated by OCPPmulti; expose it accordingly. Its writes are subject to
the same validation as any other EVerest-side write, including the in-use
network configuration protection described under
:ref:`Network connection configuration
<handwritten_ocppmulti_network-connection-configuration>`.

Provides: auth_validator
^^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`auth_token_validator <everest_interfaces_auth_token_validator>`

Forwards authorization requests from EVerest to libocpp. Libocpp contains the business logic to either validate the
authorization request locally, using the authorization cache and local authorization list, or to forward the request
to the CSMS. This also covers the validation of Plug&Charge authorization requests.

Provides: auth_provider
^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`auth_token_provider <everest_interfaces_auth_token_provider>`

Publishes authorization requests from the CSMS (e.g. a **RequestStartTransaction.req** in OCPP 2.x or a
**RemoteStartTransaction.req** in OCPP 1.6) within EVerest.

Provides: data_transfer
^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`ocpp_data_transfer <everest_interfaces_ocpp_data_transfer>`

Provides a command to initiate a **DataTransfer.req** from the charging station to the CSMS.

.. _handwritten_ocppmulti_provides-ocpp_generic:

Provides: ocpp_generic
^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`ocpp <everest_interfaces_ocpp>`

Provides an API to control the OCPP service and to set and get OCPP-specific data, independently of the active
protocol version.

Provides: session_cost
^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`session_cost <everest_interfaces_session_cost>`

Publishes session costs received from the CSMS as part of the California Pricing whitepaper extension.

Requires: auth (1)
^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`auth <everest_interfaces_auth>`

Typically fulfilled by the :ref:`Auth <everest_modules_Auth>` module. Used to apply authorization-related
configuration when configured and/or changed by the CSMS:

* **set_connection_timeout** (e.g. on a **SetVariables.req(EVConnectionTimeout)**)
* **set_master_pass_group_id** (e.g. on a **SetVariables.req(MasterPassGroupId)**)

Requires: charger_information (0-1)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`charger_information <everest_interfaces_charger_information>`

If connected, the module calls **get_charger_information** at startup and overrides the corresponding charging
station identity properties loaded from the configuration file(s).

Requires: data_transfer (0-1)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`ocpp_data_transfer <everest_interfaces_ocpp_data_transfer>`

If connected, **DataTransfer.req** messages from the CSMS are forwarded to this module, which can contain custom
handling logic.

Requires: display_message (0-1)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`display_message <everest_interfaces_display_message>`

Allows the CSMS to display pricing or other information on the display of the charging station
(**set_display_message**, **get_display_messages**, **clear_display_message**). Required for the California Pricing
whitepaper.

Requires: evse_energy_sink (0-129)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`external_energy_limits <everest_interfaces_external_energy_limits>`

Typically fulfilled by :ref:`EnergyNode <everest_modules_EnergyNode>` modules. Used to apply power or ampere limits
received from the CSMS via the SmartCharging feature (**set_external_limits**), one sink per EVSE plus one for EVSE
id 0 (the whole charging station). See the energy management section below.

Requires: evse_manager (1-128)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`evse_manager <everest_interfaces_evse_manager>`

Typically fulfilled by the :ref:`EvseManager <everest_modules_EvseManager>` module. One connection represents one
EVSE. Commands used include:

* **get_evse** to get the EVSE id of each connection at startup
* **pause_charging** / **resume_charging** to control charging on CSMS request
* **stop_transaction** to stop a transaction (e.g. on a remote stop request)
* **force_unlock** to force the unlock of a connector on an **UnlockConnector.req**
* **enable_disable** to set the EVSE (in)operative, e.g. on a **ChangeAvailability.req** (called with OCPP's
  mid-range priority of 5000)
* **update_allowed_energy_transfer_modes** to apply energy transfer modes allowed by the CSMS
* **external_ready_to_start_charging** to signal that OCPP is ready
* **set_plug_and_charge_configuration** to apply the Plug&Charge configuration (e.g. on an **ISO15118Ctrlr**
  **SetVariables.req**)

Variables received include **session_event** (drives the state machine and transaction handling), **powermeter** and
**powermeter_public_key_ocmf**, **ev_info**, **hw_capabilities**, **supported_energy_transfer_modes**,
**waiting_for_external_ready** and **ready**.

Requires: extensions_15118 (0-128)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`iso15118_extensions <everest_interfaces_iso15118_extensions>`

Shares data between ISO 15118 and OCPP, e.g. **set_get_certificate_response** to deliver the CSMS response for a
Plug&Charge EV contract certificate installation. Variables received include **iso15118_certificate_request** (to
trigger the corresponding request) and **charging_needs**.

Requires: grid_support (0-128)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`grid_support <everest_interfaces_grid_support>`

This optional requirement connects the module to per-EVSE DER devices (inverter / grid-support hardware
abstraction) implementing the grid_support interface, exposing the OCPP 2.1 DER / grid-code functional block (block R).
DER is an OCPP 2.x-only feature: when the active protocol is OCPP 1.6, any wired grid_support connection is inert.
One connection represents one EVSE and is routed by its framework ``mapping``: the connection whose mapping names an
EVSE serves that EVSE. A connection without a mapping is a configuration error and is excluded from routing.

The module maintains a per-EVSE snapshot of the DER directives currently applied by the CSMS and pushes it to that
EVSE's connection through the **set_active_directives** command. When libocpp applies, schedules, clears, supersedes, or
expires a DER control, the snapshot is rebuilt and re-sent for every registered EVSE on its own connection.

At startup the module pre-provisions an ``ACDERCtrlr`` or ``DCDERCtrlr`` device-model component (chosen by the EVSE's
energy-transfer modes) for every DER-capable EVSE, so no static device-model JSON is required for the DER controllers.
Any EVSE without a wired grid_support connection has its DER controller forced to ``Available="false"`` (preserving a
CSMS-written ``"false"`` and its source), so the CSMS does not see DER as available after the wiring is removed.

The device declares its inverter capability through the ``capability`` variable. The module stores the capability, writes
its config variables (``ModesSupported`` and the DC nameplate values) through the device model, and republishes the
current active directives filtered to the declared control types. If the device model rejects a capability re-report, the
module rolls back to the last accepted capability; an EVSE whose very first capability is rejected is unregistered.

The CSMS may write the ``Enabled`` variable (``ReadWrite``). Writing ``Enabled="false"`` makes the module push an empty
directive replacement set to that EVSE, so the device clears the EV's curves; writing ``Enabled="true"`` republishes the
filtered active set for that EVSE. A CSMS-written ``Enabled`` persists across reboots and is restored at boot.

The device reports grid event faults through the ``alarm`` variable, forwarded to the CSMS as a **NotifyDERAlarm.req**.
Alarms raised before the backend has accepted a capability for any EVSE are buffered and delivered once the first
capability is accepted; if no capability is ever accepted, the buffered alarms are dropped. Capability and alarm updates
received before the charge point is initialized are queued and replayed once the charge point is ready.

In addition to enabling the DER device-model component, the module asserts DER availability to the matching EvseManager
via its **set_der_available** command, so that EvseManager can advertise the corresponding ISO 15118-20 DER energy
transfer modes. If the device model rejects the capability, DER availability is withdrawn instead.

The configuration parameter **GridSupportHeartbeatS** sets the interval (in seconds) at which the current active
directive set is re-sent for every registered EVSE. A value of ``0`` disables the heartbeat; the set is then sent only
when it changes.

Requires: reservation (0-1)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`reservation <everest_interfaces_reservation>`

Optional. If connected, reservation requests from the CSMS are handled via **reserve_now**, **cancel_reservation**
and **exists_reservation**; reservation status updates are received via the **reservation_update** variable. If not
connected, reservation requests from the CSMS are rejected.

Requires: security (1)
^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`evse_security <everest_interfaces_evse_security>`

Typically fulfilled by the :ref:`EvseSecurity <everest_modules_EvseSecurity>` module. Used to execute
security-related operations and to manage certificates and private keys: installing, deleting and listing CA
certificates, updating and verifying leaf certificates, generating certificate signing requests, providing
certificate and key paths for TLS connections, and maintaining the OCSP cache.

Requires: system (1)
^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`system <everest_interfaces_system>`

The :ref:`System <everest_modules_System>` module can be used to fulfill this requirement (note that it is not meant
for production use without modification). Used to execute and control system-wide operations triggered by the CSMS:

* **update_firmware** and **allow_firmware_installation** for firmware updates
* **upload_logs** for log upload requests
* **is_reset_allowed** and **reset** for reset requests
* **set_system_time** to apply the time communicated by the CSMS
* **get_boot_reason** for the boot notification at startup
* **configure_network** to prepare the network for a connection attempt on a network profile slot (see
  :ref:`Network connection configuration <handwritten_ocppmulti_network-connection-configuration>`); a provider
  without special network handling answers ``NotSupported``

The **log_status** and **firmware_update_status** variables are received to report the corresponding status
notifications to the CSMS, and **configure_network_status** reports the asynchronous outcome of
**configure_network** requests.

Error reporting
===============

The ``enable_global_errors`` flag of this module is set in its manifest, so the module retrieves and processes all
errors reported by other modules in the same EVerest configuration.

Error reporting follows the Minimum Required Error Codes (MRECs, https://inl.gov/chargex/mrec/). The module maps
EVerest error types to MREC techCodes using a built-in mapping table. ``CustomMrecErrorMapPath`` can point to a JSON
file whose entries overwrite the built-in defaults; note that this override is currently only applied on the OCPP 2.x
error path. OCPP 1.6 uses a separate built-in MREC table that ``CustomMrecErrorMapPath`` does not affect.

For both protocol generations, only errors of the special type **evse_manager/Inoperative** are reported as faults
(i.e. lead to a **StatusNotification.req** with status **Faulted**); this type indicates that the EVSE is not
available for energy transfer. The EVSE/connector an error is reported for is derived from the EVerest mapping of the
error's origin; without a mapping, it is reported for the whole charging station (connector 0). All other errors are
reported without changing the connector status, in a version-specific way.

OCPP 1.6
^^^^^^^^

Errors are reported via the error fields of additional **StatusNotification.req** messages. Each error is converted
as follows:

* If the EVerest error type has a (built-in) MREC mapping, ``errorCode`` is taken from that mapping, ``vendorId`` is
  set to the MREC vendor id, and ``vendorErrorCode`` carries the MREC techCode. (The OCPP 1.6 path uses a fixed
  built-in table; ``CustomMrecErrorMapPath`` is not applied here.)
* Otherwise, if it maps to a standard OCPP 1.6 error code, ``errorCode`` is set accordingly.
* Otherwise, ``errorCode`` is ``OtherError``, ``info`` carries the error origin, ``vendorId`` the error message
  (up to 255 characters, the largest field), and ``vendorErrorCode`` a code derived from the EVerest error type
  (``info`` and ``vendorErrorCode`` are limited to 50 characters).

OCPP 1.6 limitations: individual errors cannot be cleared selectively via **StatusNotification.req**, and, deviating
from MREC, simultaneous errors are reported in one message per error instead of a single semicolon-separated
message, because of the field length limits.

The ``info`` field has a second, unrelated use: reporting why a connector is suspended. See
:ref:`suspend reason reporting <handwritten_ocppmulti_suspend-reason-reporting>` below.

OCPP 2.x
^^^^^^^^

In contrast to OCPP 1.6, error information is not transmitted in **StatusNotification.req** (it is only sent with
status **Faulted** for the Inoperative case above). All other errors are reported via **NotifyEvent.req**, whose
**eventData** structure requires mapping each error to a component-variable combination:

* **ChargingStation** if the error origin has no EVSE mapping
* **EVSE** otherwise; when a connector is also mapped, its id is carried in the component's ``connectorId`` (the
  component name stays **EVSE**, a dedicated **Connector** component is not used yet)

The variable is constantly set to **Problem** for now; a more fine-grained mapping of errors to component-variable
combinations may be added in the future.

.. _handwritten_ocppmulti_suspend-reason-reporting:

Suspend reason reporting in OCPP 1.6
====================================

While a connector sits in **SuspendedEVSE**, the reason can change without the status changing: a user pause during an
energy shortage, or a fault raised on an already suspended connector. OCPP 1.6 has no field for that, so the reason set
is reported in the free-text ``info`` field of a further **StatusNotification.req**, reusing the field the error path
above writes.

The feature is off by default. Set the ``InternalCtrlr`` variable ``ReportSuspendedEVSEReasonChange`` (boolean,
ReadWrite, default ``false``) to enable it. It doubles as an OCPP 1.6 configuration key of the same name and is read on
every suspend event, so **ChangeConfiguration** takes effect without a restart.

With it enabled, a reason change on an already suspended connector emits one **StatusNotification.req** with ``status``
unchanged at **SuspendedEVSE**, ``errorCode`` **NoError** (or the most recent active error's code), and the reasons in
``info``: ``UserPause``, ``Error`` and/or ``NoEnergy``, comma joined. Repeats are suppressed, and an empty set omits
``info`` rather than sending an empty string. **TriggerMessage(StatusNotification)** reports the current reason when
no error is active.

Limitations:

* All energy-related causes collapse into the single ``NoEnergy`` value. A schedule limit, load balancing and a
  missing PV surplus are not distinguishable at the CSMS.
* ``SwitchingPhases`` shares the ``info`` field with the pause reasons, so a phase switch followed by a pause
  alternates between the two, one message per cycle.
* When a fault clears while the connector is paused, the notification carries the cleared error's ``info`` rather than
  the live pause reason; a **TriggerMessage(StatusNotification)** recovers it.
* Turning the variable off at runtime does not clear a reason already reported; the CSMS holds it until the connector
  leaves **SuspendedEVSE**.

Energy management and smart charging
====================================

Both OCPP 1.6 and OCPP 2.x define a SmartCharging feature profile that allows the CSMS to control or influence the
power consumption of the charging station. This module integrates the resulting composite schedules with EVerest's
energy management through the optional ``evse_energy_sink`` requirements (interface ``external_energy_limits``).

Each composite limit is communicated via a separate sink, including the composite schedule for EVSE id 0
(representing the whole charging station). For a charging station with two EVSEs, three modules implementing
``external_energy_limits`` need to be connected: one representing EVSE id 0 and two representing the actual EVSEs.

📌 **Note:** An EVSE mapping must be configured for each module connected via the ``evse_energy_sink`` connection, so
the module can identify which requirement to use when communicating the limits. See
:doc:`3-tier module mappings </explanation/tier-module-mappings>`.

Whenever charging profiles are added, changed or removed by the CSMS (and periodically, driven by
``CompositeScheduleIntervalS``), the module retrieves the composite schedules for all EVSEs from libocpp, publishes
them via the provided :ref:`ocpp_generic <handwritten_ocppmulti_provides-ocpp_generic>` implementation, and calls
**set_external_limits** on the respective sinks. ``RequestCompositeScheduleDurationS`` defines the duration of the
requested schedules starting now; it shall be greater than ``CompositeScheduleIntervalS``, otherwise time periods
could be missed. ``RequestCompositeScheduleUnit`` selects the unit (Amps or Watts) in which schedules are requested
and shared.

Certificate management
======================

Up to three leaf certificates are managed by the OCPP communication enabled by this module:

* CSMS client certificate (used for mTLS with security profile 3)
* SECC server certificate for ISO 15118-2 (TLS 1.2, ``prime256v1`` key; OCPP certificateType
  **V2GCertificate**)
* SECC server certificate for ISO 15118-20 (TLS 1.3, ``secp521r1`` key; OCPP 2.1 certificateType
  **V2G20Certificate**)

The two SECC certificates are distinct: ISO 15118-2 and ISO 15118-20 mandate incompatible key algorithms, so one
leaf cannot serve both protocol generations. Both are stored in the ``EvseSecurity`` SECC leaf directory and are
told apart by their key algorithm; their sub-CA certificates arrive with the signed chain in **CertificateSigned.req**
and their roots -- which may or may not be the same for the two -- are installed into the V2G root bundle via
**InstallCertificate.req** (``V2GRootCertificate``).

In OCPP 2.x, 60 seconds after the first **BootNotification.req** has been accepted by the CSMS, the charging station
checks whether these certificates are missing or expire within 30 days and, if so, initiates a
**SignCertificate.req** towards the CSMS (a missing certificate is requested the same way as an expiring one, which
is how the initial certificate is provisioned). For the CSMS leaf certificate this is only done when security
profile 3 is used; for the ISO 15118-2 SECC leaf only when Plug&Charge is enabled via
**ISO15118Ctrlr.V2GCertificateInstallationEnabled**; for the ISO 15118-20 SECC leaf additionally only when the
connection is OCPP 2.1 (a 2.0.1 CSMS does not know **V2G20Certificate**) and
**InternalCtrlr.V2G20CertificateInstallationEnabled** (default ``true``) has not been switched off for a 2.1 CSMS that
does not support **V2G20Certificate**. Expiry is re-checked every 12 hours
(``V2GCertificateExpireCheckIntervalSeconds``); the two SECC leafs are checked and renewed independently, and
because only one **SignCertificate.req** may be outstanding, when both are due the -2 leaf is requested first and the
-20 leaf on the next check. On OCPP 2.1 every **SignCertificate.req** carries a ``requestId`` and a
**CertificateSigned.req** whose ``requestId`` does not belong to the outstanding request is rejected (A02.FR.24 /
A02.FR.26); a SECC leaf request also names the V2G root it shall be issued under in ``hashRootCertificate``
(A02.FR.27) -- the root of the installed leaf, or the only installed V2G root, omitted when neither identifies one. A
**CertificateSigned.req** that omits ``certificateType`` is installed as the type of the outstanding request (the CSMS
is only recommended to echo the type). The CSMS can also trigger either request via **TriggerMessage.req**
(``SignV2GCertificate`` / ``SignV2G20Certificate``). A newly installed SECC leaf is picked up by the
``Evse15118D20`` module without a restart via the ``certificate_store_update`` event. In OCPP 1.6, the equivalent
functionality for the CSMS and ISO 15118-2 SECC certificates is provided via the OCPP 1.6 security whitepaper
extension and the Plug&Charge extension implemented via **DataTransfer.req** messages; the ISO 15118-20 SECC leaf is
not managed over OCPP 1.6.

In addition, the charging station periodically (by default every seven days) updates the OCSP responses of the sub-CA
certificates of the V2G certificate chain. The cached OCSP response can be used as part of the ISO 15118 TLS
handshake with EVs. An update is attempted at every startup and then on the periodic interval.

Configuration access
====================

OCPP configuration can be read, written and monitored through three channels:

- **CSMS**: via the OCPP protocol itself — ``GetVariables`` /
  ``SetVariables`` / ``SetVariableMonitoring`` in OCPP 2.x,
  ``GetConfiguration`` / ``ChangeConfiguration`` in OCPP 1.6.
- **EVerest modules**: require the ``ocpp`` interface and call
  ``call_get_variables`` / ``call_set_variables`` / ``call_monitor_variables``;
  subscribe ``event_data`` for monitor notifications.
- **External integrations** (web interface, configuration tools, vendor cloud
  agents): the :ref:`ocpp_consumer_API
  <everest_modules_handwritten_ocpp_consumer_API>` module, which republishes
  OCPPmulti's ``ocpp`` interface on the EVerest API MQTT surface. Every
  ``get_variables`` / ``set_variables`` / ``monitor_variables`` example in this
  section applies verbatim there; the payloads shown *are* the API payloads.
  Configuration access is only part of what it exposes; see
  :ref:`Control from outside EVerest
  <handwritten_ocppmulti_control-from-outside-everest>`, and its own
  documentation for the full surface and the transport details.

On the two EVerest-side channels, addressing and semantics are identical,
regardless of whether OCPP 1.6 or 2.x is active. The CSMS channel
uses whatever the active protocol version prescribes (configuration keys in
1.6, component/variable in 2.x); the rest of this section covers the
EVerest-side channels.

Reading and writing (canonical form)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Read (``get_variables``):

.. code-block:: json

   {"items": [
     {"component_variable": {"component": {"name": "OCPPCommCtrlr"}, "variable": {"name": "HeartbeatInterval"}}}
   ]}

Write (``set_variables``):

.. code-block:: json

   {"variables": {"items": [
     {"component_variable": {"component": {"name": "OCPPCommCtrlr"}, "variable": {"name": "HeartbeatInterval"}},
      "value": "300"}
   ]}, "source": "webinterface"}

Results echo the requested ``component_variable`` and carry a status:
``Accepted``, ``RebootRequired`` (writes only; persisted; takes effect on next
(re)connect or reboot), ``Rejected``, ``UnknownComponent`` / ``UnknownVariable``,
or ``NotSupportedAttributeType``.

The status is the whole answer: the reason codes the stack computes internally
(``PriorityNetworkConf``, ``NoSecurityDowngrade``, ``InvalidNetworkConf``, ...)
are **currently not** carried on this interface; its result has no ``statusInfo`` field,
in either protocol mode. A rejected write therefore has to be diagnosed from the
module log, which names the reason code and the offending component/variable.

Addressing rules:

- Standard OCPP 2.x variables: their standard component (``OCPPCommCtrlr``,
  ``SecurityCtrlr``, ...). Works for the same datum in both protocol modes.
- 1.6-only/vendor keys: component ``OCPP16LegacyCtrlr``, variable = key name.
- Custom-mapped keys (station-specific YAML): the mapped component/variable
  from the mapping file.
- The deprecation warning emitted for legacy requests names the canonical
  address for each key in use — the simplest migration discovery mechanism.

.. _handwritten_ocppmulti_network-connection-configuration:

Network connection configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Connection settings live in network profile **slots**: one
``NetworkConfiguration`` component per slot, addressed with the slot number as
component ``instance``, plus
``OCPPCommCtrlr``/``NetworkConfigurationPriority`` - a comma-separated, ordered
list of the slots to try. Slots are set up in the component configuration and can be changed at runtime through this API. Both
protocol versions read this same representation, so everything below applies to
OCPP 1.6 and 2.x alike; the handful of behavioral differences is collected at
the end of this section.

Setting up slots
""""""""""""""""

Which slots exist is decided by the component configuration: ship one
``NetworkConfiguration_<N>.json`` per slot and list every usable slot in
``NetworkConfigurationPriority`` - both in its value (or default) and in its
``valuesList``, which gates runtime priority writes. The shipped configuration
contains two slots as an example; provide as many as your setup needs. A
typical setup is slot 1 on a cellular modem (``OcppInterface`` ``"Wireless0"``
plus APN settings) with slot 2 as a wired fallback (``"Wired0"``).

A slot is only usable once its profile is complete: ``OcppCsmsUrl``,
``SecurityProfile``, ``OcppInterface``, ``OcppTransport`` and ``MessageTimeout``
are all mandatory, and a slot missing any of them is skipped. The example
``NetworkConfiguration_1.json`` carries defaults for everything except the URL
(``SecurityProfile`` ``1``, ``OcppInterface`` ``"Wired0"``, ``OcppTransport``
``"JSON"``, ``MessageTimeout`` ``30``), so slot 1 only needs an
``OcppCsmsUrl``; ``NetworkConfiguration_2.json`` deliberately carries no
defaults for the mandatory fields, so each of them must be set explicitly - in
the component config or at runtime.

.. list-table::
   :header-rows: 1

   * - Setting
     - Component
     - Variable
   * - CSMS endpoint URL
     - ``NetworkConfiguration`` / instance ``<slot>``
     - ``OcppCsmsUrl``
   * - Security profile (0–3)
     - ``NetworkConfiguration`` / instance ``<slot>``
     - ``SecurityProfile``
   * - Network interface
     - ``NetworkConfiguration`` / instance ``<slot>``
     - ``OcppInterface``
   * - Transport and message timeout
     - ``NetworkConfiguration`` / instance ``<slot>``
     - ``OcppTransport``, ``MessageTimeout``
   * - Basic-auth password (AuthorizationKey)
     - ``NetworkConfiguration`` / instance ``<slot>``
     - ``BasicAuthPassword`` (write-only)
   * - Charge point identity
     - ``NetworkConfiguration`` / instance ``<slot>``
     - ``Identity``
   * - OCPP version of the profile
     - ``NetworkConfiguration`` / instance ``<slot>``
     - ``OcppVersion``
   * - Slot priority order (activation selector)
     - ``OCPPCommCtrlr``
     - ``NetworkConfigurationPriority`` (ReadWrite)
   * - Currently active slot (report only)
     - ``OCPPCommCtrlr``
     - ``ActiveNetworkProfile`` (ReadOnly)

Failover between slots
""""""""""""""""""""""

Slots are tried in priority order, and the list wraps around. A slot is skipped
when the ``system`` provider's **configure_network** answers
``Failed``/``Rejected`` (or does not respond within
``InternalCtrlr``/``NetworkConfigTimeout`` seconds, default 60), when the
resulting profile is invalid, or when the websocket connection fails
``OCPPCommCtrlr``/``NetworkProfileConnectionAttempts`` times in a row. Setting
``NetworkProfileConnectionAttempts`` to ``-1`` means retry-forever and thereby
disables the websocket-failure-driven part of the failover; only do that
deliberately. There is no automatic fall-back to a higher-priority slot while a
lower-priority one is connected. The address the ``system`` provider returns
from **configure_network** is what the websocket is bound to for that attempt.

Profiles with a ``SecurityProfile`` below the *confirmed* security profile -
the ``SecurityCtrlr``/``SecurityProfile`` value, which is raised only after a
successful connect - are pruned from the failover list, so security profile
upgrades are one-way. Failed attempts on a higher-security-profile slot do not
prune anything.

Changing connection details at runtime
""""""""""""""""""""""""""""""""""""""

The whole reconfiguration below is doable from outside EVerest: the
:ref:`ocpp_consumer_API <everest_modules_handwritten_ocpp_consumer_API>`
module exposes ``get_variables`` / ``set_variables`` / ``monitor_variables``
on the EVerest API MQTT surface, and the payloads in this section are exactly
what that API takes. A web interface or commissioning tool can therefore
repoint the charging station at a different CSMS, rotate credentials or switch
to a fallback interface without an EVerest module and without CSMS
involvement. The same API is the natural way to *observe* the result; monitor
``OCPPCommCtrlr``/``ActiveNetworkProfile`` for the slot actually in use. It
controls the OCPP stack well beyond network configuration; see
:ref:`Control from outside EVerest
<handwritten_ocppmulti_control-from-outside-everest>`.

Workflow - fully populate a spare profile slot (here: slot 2, which ships
without defaults, so every field has to be written; an incomplete slot cannot
be put into the priority):

.. code-block:: json

   {"variables": {"items": [
     {"component_variable": {"component": {"name": "NetworkConfiguration", "instance": "2"},
                             "variable": {"name": "OcppCsmsUrl"}},
      "value": "wss://csms.example.com/ocpp"},
     {"component_variable": {"component": {"name": "NetworkConfiguration", "instance": "2"},
                             "variable": {"name": "SecurityProfile"}},
      "value": "2"},
     {"component_variable": {"component": {"name": "NetworkConfiguration", "instance": "2"},
                             "variable": {"name": "OcppInterface"}},
      "value": "Any"},
     {"component_variable": {"component": {"name": "NetworkConfiguration", "instance": "2"},
                             "variable": {"name": "OcppTransport"}},
      "value": "JSON"},
     {"component_variable": {"component": {"name": "NetworkConfiguration", "instance": "2"},
                             "variable": {"name": "MessageTimeout"}},
      "value": "30"},
     {"component_variable": {"component": {"name": "NetworkConfiguration", "instance": "2"},
                             "variable": {"name": "BasicAuthPassword"}},
      "value": "0123456789abcdef"}
   ]}, "source": "webinterface"}

then activate it by putting slot 2 first in the priority order:

.. code-block:: json

   {"variables": {"items": [
     {"component_variable": {"component": {"name": "OCPPCommCtrlr"}, "variable": {"name": "NetworkConfigurationPriority"}},
      "value": "2,1"}
   ]}, "source": "webinterface"}

Keeping slot 1 in the list retains it as a fallback, but with only the two
shipped slots it also means no slot is left that can be rewritten (see the
protection rules below). Write ``"2"`` instead if you want to keep slot 1 free
for the next reconfiguration, or ship a third slot to rotate through.

Committed writes persist immediately, are picked up right away and take effect
on the next (re)connect; an already-established connection is not interrupted.
The status returned for a connection-config write differs between the protocol
versions (see below), but in neither case is a reboot actually required.

The in-use configuration is protected (B09.FR.21/22), which is why the workflow
above prepares a spare slot first:

- Writes targeting the currently *active* slot, or any slot listed in
  ``NetworkConfigurationPriority``, are rejected with ``PriorityNetworkConf``.
  Keep at least one slot out of the priority list so there is always a slot you
  can rewrite.
- A ``NetworkConfigurationPriority`` write naming a slot without a complete
  profile is rejected with ``InvalidNetworkConf``, as is a change that would
  make a slot's URL scheme inconsistent with its security profile (``ws://``
  needs profile < 2, ``wss://`` needs >= 2).
- Lowering a slot's ``SecurityProfile`` below the confirmed one is rejected
  with ``NoSecurityDowngrade``.

Because the active slot is always protected, credentials for the running
connection cannot be rotated per slot. Write the ``SecurityCtrlr`` globals
instead: a committed write to ``SecurityCtrlr``/``Identity`` or
``SecurityCtrlr``/``BasicAuthPassword`` clears the corresponding per-slot
override on the active slot (B09.FR.26/27), so the new global value is what the
next connection attempt uses - even when a legacy migration had populated the
per-slot value. ``BasicAuthPassword`` is write-only; reads do not return it.

``set_variables`` on this API may write ReadOnly variables: it is a trusted
local integrator channel, and ReadOnly mutability models the CSMS-facing
contract, not this API (the protection above still applies regardless of
mutability). Some cells are nevertheless stack-owned runtime state, written on
every successful connect; writing them here desyncs the device model from the
connection logic. Do not write:

- ``OCPPCommCtrlr``/``ActiveNetworkProfile`` - reports which slot is in use;
  a manual write is corrected only on the next successful connect.
- ``SecurityCtrlr``/``SecurityProfile`` - the *confirmed* security profile,
  raised after a successful connect and used to permanently prune slots below
  it from the failover list; raising it by hand can leave no attemptable slot.
- ``NetworkConfiguration``/``OcppVersion`` - written back with the negotiated
  version on 2.x connects; a value other than ``"OCPP16"`` removes the slot
  from the usable set in OCPP 1.6 mode.

Where the protocol versions differ
""""""""""""""""""""""""""""""""""

- **Status of a committed write.** OCPP 1.6 answers connection-config writes
  with ``RebootRequired``, OCPP 2.x with ``Accepted``. The persisted value and
  the moment it takes effect are the same.
- **Priority changes.** In OCPP 1.6 a ``NetworkConfigurationPriority`` write is
  picked up immediately and is effective on the next (re)connect. In OCPP 2.x
  it is persisted but only taken into account after a reboot; plan one when
  switching slots this way.
- **Unusable slots.** OCPP 1.6 additionally ignores slots whose ``OcppVersion``
  names another version, and falls back to the legacy single-profile settings
  when the *active* slot is filtered out or incomplete; OCPP 2.x uses every
  listed slot and simply skips incomplete ones. See
  :ref:`Network connection profiles (OCPP 1.6) <handwritten_ocppmulti_network-profiles-ocpp16>`
  for the details.
- **Deprecated key-only writes.** Only in OCPP 1.6 mode, the legacy
  ``SecurityProfile`` and ``AuthorizationKey`` keys can still be written
  (empty component name); they act on the active slot and bypass the validation
  above, and the profile used for **configure_network** and for
  security-profile pruning keeps its previous contents until a canonical write,
  a security-profile switch or a restart. ``SecurityProfile`` is not a quiet
  write either: it triggers the 1.6 security-profile switch, which reconnects
  immediately and falls back to the previous profile if the new one does not
  connect within ``SwitchSecurityProfileConnectionTimeout``.
  ``CentralSystemURI`` is *not* writable this way; it is read-only in the key
  path, so the canonical ``NetworkConfiguration`` addressing is the only way to
  change a CSMS URL.

Monitoring configuration changes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``monitor_variables`` with canonical addresses; changes are published on
``event_data`` with the same ``component_variable`` used at registration:

.. code-block:: json

   {"items": [
     {"component": {"name": "OCPPCommCtrlr"}, "variable": {"name": "HeartbeatInterval"}}
   ]}

Legacy-form registrations (empty component name) receive legacy-shaped events.
Registrations are additive across calls.

Migration from OCPP 1.6 key addressing
======================================

Existing 1.6 integrations keep working in v16 mode: empty component name,
``variable.name`` = configuration key.

.. code-block:: json

   {"items": [
     {"component_variable": {"component": {"name": ""}, "variable": {"name": "HeartbeatInterval"}}}
   ]}

The first use of each key logs a deprecation warning naming the canonical
address. This form is not accepted when OCPP 2.x is active (returns
``UnknownComponent``) and will be removed per the deprecation policy, it is strongly recommended to migrate
to canonical addressing. Requests with a non-empty component name are never
reinterpreted as configuration keys.

Behavioral difference to the legacy OCPP module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The legacy ``OCPP`` module ignores ``component.name`` entirely and always
treats ``variable.name`` as a configuration key. OCPPmulti resolves the
component: a request whose non-empty component name is not a known address
returns ``UnknownComponent`` instead of being treated as a key, and a
``monitor_variables`` registration with such an address never fires.
Integrations that pass placeholder component names must migrate to the
canonical address (or, transitionally, the deprecated empty-component form)
when switching to this module.
