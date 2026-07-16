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
the legacy JSON into the device model and when serving the device-model-backed configuration at runtime. OCPP 1.6
keys without a standardized device model counterpart are kept in a dedicated ``OCPP16LegacyCtrlr`` component, whose
``NumberOfConnectors`` value is automatically patched to the actual number of connected EVSEs.

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

The **log_status** and **firmware_update_status** variables are received to report the corresponding status
notifications to the CSMS.

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

Two leaf certificates are managed by the OCPP communication enabled by this module:

* CSMS client certificate (used for mTLS with security profile 3)
* SECC server certificate (server certificate for ISO 15118)

In OCPP 2.x, 60 seconds after the first **BootNotification.req** has been accepted by the CSMS, the charging station
checks whether these certificates are missing or expired and, if so, initiates a **SignCertificate.req** towards the
CSMS. For the CSMS leaf certificate this is only done when security profile 3 is used; for the SECC leaf certificate
only when Plug&Charge is enabled via **ISO15118Ctrlr.V2GCertificateInstallationEnabled**. Expiry is re-checked every
12 hours. In OCPP 1.6, the equivalent functionality is provided via the OCPP 1.6 security whitepaper extension and
the Plug&Charge extension implemented via **DataTransfer.req** messages.

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
- **External integrations** (web interface, configuration tools): the
  ``ocpp_consumer_API``; see its own documentation for transport and message
  details.

On the two EVerest-side channels, addressing and semantics are identical,
regardless of whether OCPP 1.6 or 2.x is active. The CSMS channel
uses whatever the active protocol version prescribes (configuration keys in
1.6, component/variable in 2.x); the rest of this section covers the
EVerest-side channels.

Reading and writing (canonical form)
------------------------------------

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
(re)connect or reboot), ``Rejected`` (with ``statusInfo`` explaining why and, where
applicable, what to use instead), ``UnknownComponent`` / ``UnknownVariable``,
or ``NotSupportedAttributeType``.

Addressing rules:

- Standard OCPP 2.x variables: their standard component (``OCPPCommCtrlr``,
  ``SecurityCtrlr``, ...). Works for the same datum in both protocol modes.
- 1.6-only/vendor keys: component ``OCPP16LegacyCtrlr``, variable = key name.
- Custom-mapped keys (station-specific YAML): the mapped component/variable
  from the mapping file.
- The deprecation warning emitted for legacy requests names the canonical
  address for each key in use — the simplest migration discovery mechanism.

Changing OCPP connection details
--------------------------------

Connection settings live in network profile **slots**, addressed as component
``NetworkConfiguration`` with the slot number as component ``instance``. Both
protocol stacks read this same representation (shared connectivity manager),
so the workflow below is identical for OCPP 1.6 and 2.x.

.. list-table::
   :header-rows: 1

   * - Setting
     - Component
     - Variable
   * - CSMS endpoint URL
     - ``NetworkConfiguration`` / instance ``<slot>``
     - ``OcppCsmsUrl``
   * - Security profile (1–3)
     - ``NetworkConfiguration`` / instance ``<slot>``
     - ``SecurityProfile``
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

Workflow — prepare a profile (here: slot 2):

.. code-block:: json

   {"variables": {"items": [
     {"component_variable": {"component": {"name": "NetworkConfiguration", "instance": "2"},
                             "variable": {"name": "OcppCsmsUrl"}},
      "value": "wss://csms.example.com/ocpp"},
     {"component_variable": {"component": {"name": "NetworkConfiguration", "instance": "2"},
                             "variable": {"name": "SecurityProfile"}},
      "value": "2"},
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

Each of these returns ``RebootRequired``: the values are validated and
persisted immediately and take effect on the next (re)connect or reboot.
Editing the currently active slot in place is equally allowed (same
``RebootRequired`` semantics). ``BasicAuthPassword`` is write-only — reads do
not return it. ``ActiveNetworkProfile`` reports which slot is in use and is
ReadOnly (per OCPP 2.x); writes to it are rejected by mutability in both
modes. In v16 mode this workflow is semantically equivalent to the legacy
``CentralSystemURI`` / ``SecurityProfile`` / ``AuthorizationKey`` key writes,
which remain available (deprecated) and operate on the active slot.

Monitoring configuration changes
--------------------------------

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
-----------------------------------------------

The legacy ``OCPP`` module ignores ``component.name`` entirely and always
treats ``variable.name`` as a configuration key. OCPPmulti resolves the
component: a request whose non-empty component name is not a known address
returns ``UnknownComponent`` instead of being treated as a key, and a
``monitor_variables`` registration with such an address never fires.
Integrations that pass placeholder component names must migrate to the
canonical address (or, transitionally, the deprecated empty-component form)
when switching to this module.
