.. _everest_modules_handwritten_OCPPmulti_device_model:

How the device model is composed
================================

OCPPmulti keeps one device model, in one database, for OCPP 1.6 and OCPP 2.x alike. An OCPP 2.x CSMS reads and
writes it with ``GetVariables`` and ``SetVariables``, an OCPP 1.6 CSMS with ``GetConfiguration`` and
``ChangeConfiguration``, and EVerest modules reach it through the ``ocpp`` interface. Five contributors fill it:

- the **component configuration**, the JSON files in which the integrator defines the station;
- **libocpp**, which computes the availability states;
- the **components EVerest provides**, reported by the modules the OCPP module is connected to;
- the **EVerest module configuration views**, backed by the configuration service;
- **external providers**, components that bring their own configuration and telemetry.

Every variable has exactly one owner, its *source*. libocpp's device model composes the model from what the owners
bring, serves every read from memory and routes every write to the owner.

.. note::

   External providers and the ``ocpp_device_model_provider`` API arrive in their own pull request, after the single
   device model database. They are described here already so that the page shows the complete design.

.. mermaid::

   flowchart LR
       JSON["Component configuration (JSON)"]
       LIB[libocpp]
       EV[Components EVerest provides]
       VIEWS[Module configuration views]
       PRV[Provider binding]
       DM[Device model]
       DB[(device model database)]
       CS[(configuration service)]
       MODS[EVerest modules]
       EXT[External provider]
       CSMS[CSMS]
       API[ocpp interface]
       JSON -- seeds --> DB
       DB -- values, monitors --> DM
       LIB -- AvailabilityState --> DM
       MODS -- capabilities, power, EV information --> EV
       EV -- registers, reports values --> DM
       CS -- parameters, changes --> VIEWS
       VIEWS -- registers, answers writes --> DM
       EXT -- registration, changes --> PRV
       PRV -- registers, answers writes --> DM
       DM --> CSMS
       DM --> API

One model for OCPP 1.6 and OCPP 2.x
-----------------------------------

The device model backs configuration and telemetry of both protocol versions. OCPP 2.x addresses it directly with
``GetVariables``, ``SetVariables`` and ``GetBaseReport``. OCPP 1.6 knows only configuration keys, so OCPPmulti maps
each key onto a component and variable: a built-in mapping for the standard keys, custom mappings from
``DeviceModelConfigMappings``, and the ``OCPP16LegacyCtrlr`` component for keys without a standardized counterpart.
``GetConfiguration`` and ``ChangeConfiguration`` read and write the mapped variables, whichever contributor owns
them; a variable without a mapping is invisible to an OCPP 1.6 CSMS. OCPP 1.6 has no variable monitoring, so
telemetry such as ``EVSE`` / ``Power`` reaches a 1.6 CSMS only through ``MeterValues``. The ``ocpp`` interface
sees every variable in both modes.

Who contributes
---------------

.. list-table::
   :header-rows: 1
   :widths: 22 14 30 34

   * - Contributor
     - Source tag
     - Brings
     - Values live in
   * - Component configuration
     - none
     - the components and variables defined in the JSON files under ``DeviceModelConfigPath``
     - the device model database
   * - libocpp
     - ``OCPP_RUNTIME``
     - ``AvailabilityState`` of the charging station, the EVSEs and the connectors
     - memory, computed by libocpp
   * - Components EVerest provides
     - ``EVEREST``
     - ``EVSE``, ``Connector``, ``V2XChargingCtrlr``, ``ISO15118Ctrlr``, ``ConnectedEV``, ``ACDERCtrlr`` and
       ``DCDERCtrlr``
     - memory, reported by the module; DER ``Enabled`` also in the database
   * - EVerest module configuration views
     - ``EVEREST_CONFIG``
     - one component per EVerest module the OCPP module may read, one variable per configuration parameter
     - the configuration service
   * - External providers
     - ``EXT:<module_id>``
     - their own components, vendor variables on standard components, and standard configuration variables they
       take over
     - the provider; nothing on the EVerest side

.. _handwritten_ocppmulti_where-custom-configuration-goes:

Custom configuration and telemetry
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

An integrator adds custom configuration and telemetry to the device model in one of two ways: in the component
configuration, or through an external provider. They differ in who owns the value.

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * -
     - Component configuration
     - External provider
   * - Owner of the value
     - the OCPP module, in the device model database
     - the integrator's component
   * - Declared in
     - the JSON files
     - the provider's registration
   * - Validated by
     - the device model, against the declared characteristics
     - the device model, then the provider itself
   * - Told about a write
     - nobody; a client watches with ``monitor_variables`` of the ``ocpp_consumer_API``
     - the provider, which may reject it
   * - Telemetry
     - no; the values are configuration the CSMS sets
     - yes; the provider reports its values as they change
   * - Choose it for
     - configuration the station is given, where type, range and enum checks are enough
     - configuration the component validates, keeps or changes itself; telemetry; a standard variable the
       provider takes over

A custom variable is declared in exactly one of them.

The component configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The JSON files under ``DeviceModelConfigPath`` define the station as the integrator ships it: the standardized
controllers, the network connection slots, ``InternalCtrlr`` and any custom component. They are described in
:ref:`Device model configuration via component configs <everest_modules_handwritten_OCPPmulti>`.

At the first start the files seed the database. At every later start their structure is applied again, so a new
or changed component, variable or attribute reaches the database, while values written at runtime are kept. From
then on the database holds the values. The module needs no ``EVSE`` or ``Connector`` component configuration; they
are registered at runtime.

A few of these variables are maintained by libocpp rather than by the CSMS: ``OCPPCommCtrlr`` /
``ActiveNetworkProfile`` and ``NetworkConfigurationPriority`` and ``SecurityCtrlr`` / ``SecurityProfile``, which
follow the connection in use; ``OCPP16LegacyCtrlr`` / ``NumberOfConnectors`` and ``OCPPCommCtrlr`` /
``FieldLength`` of the ISO 15118 certificate response, filled at every start; and ``AuthCacheCtrlr`` / ``Storage``
and ``LocalAuthListCtrlr`` / ``Entries``, whose ``maxLimit`` the integrator configures to bound the authorization
cache and the local list.

libocpp
^^^^^^^

libocpp registers ``AvailabilityState`` of the charging station, every EVSE and every connector under the source
``OCPP_RUNTIME``. The value comes from the same state machine that drives ``StatusNotification``, so
``GetVariables`` and ``StatusNotification`` always agree. Component configurations shipped before this
registration existed define these variables as well; the registration takes precedence. The OCPP 1.6 stack does
not use them, so the OCPP 1.6 path skips the registration.

Components EVerest provides
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The components that describe the station's hardware and the connected EV are built from what the EVerest modules
report and registered under the source ``EVEREST`` at every start. An EVSE that is added or removed shows up at
the next start without touching the database.

.. list-table::
   :header-rows: 1
   :widths: 24 24 52

   * - Component
     - One per
     - Variables
   * - ``EVSE``
     - EVSE
     - ``Available``, ``Power``, ``SupplyPhases``, ``AllowReset``, ``ISO15118EvseId``
   * - ``Connector``
     - connector
     - ``Available``, ``Type``, ``SupplyPhases``
   * - ``V2XChargingCtrlr``
     - EVSE
     - ``Available``, ``Enabled``, ``SupportedEnergyTransferModes``, ``SupportedOperationModes``
   * - ``ISO15118Ctrlr``
     - EVSE with an ISO 15118 extension
     - ``Enabled``, ``ServiceRenegotiationSupport``, ``ProtocolSupported``
   * - ``ConnectedEV``
     - EVSE
     - ``Available``, ``VehicleId``, ``ProtocolAgreed``, ``VehicleCertificate`` (instances ``Leaf``, ``SubCA1``,
       ``SubCA2``, ``Root``), ``ProtocolSupportedByEV`` instances
   * - ``ACDERCtrlr`` / ``DCDERCtrlr``
     - EVSE with a ``grid_support`` connection
     - ``Available``, ``Enabled``, ``ModesSupported``, the DC nameplate values

The values come from the EVSE manager, its power meter, the ISO 15118 extension and the ``grid_support`` device,
and the module reports them as they change. All variables are ``ReadOnly`` except ``Enabled`` of the DER
controllers, which the CSMS may write; it is the only persistent attribute of this group, so the value the CSMS
set survives a restart. Monitors the CSMS sets on any of these variables survive a restart as well.

.. _handwritten_ocppmulti_module-configuration-views:

EVerest module configuration views
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The configuration of the other EVerest modules is visible in the device model under the source ``EVEREST_CONFIG``:

* One component per module, named after the module type, with the module id as instance and the EVSE and
  connector taken from the module's mapping: ``EvseManager`` with instance ``connector_1`` on EVSE 1.
* One variable per configuration parameter, named like the parameter; a parameter of an implementation is
  prefixed with the implementation id.
* Data type, unit, limits and mutability come from the manifest. ``WriteOnly`` parameters cannot be monitored.
* The values stay in the configuration service, which keeps them across restarts.

Which modules are visible follows the ``access`` block of the OCPP module in the EVerest configuration: reading
needs read access to the module, writing additionally needs write access. A ``ReadOnly`` parameter cannot be
changed through OCPP.

A write the CSMS makes is forwarded to the configuration service, and its answer is the answer to the CSMS:
``Accepted``, ``Rejected``, or ``RebootRequired`` for a parameter the module applies only at its next start. A
change made elsewhere, for example through the Configuration API, shows up in ``GetVariables`` and in variable
monitors right away.

.. _handwritten_ocppmulti_external-providers:

External providers
^^^^^^^^^^^^^^^^^^

An integrator can bring their own configuration and telemetry into the device model from outside the EVerest
process tree: a vendor controller with its settings, a display, a payment terminal, a cloud agent. The EVerest
interface ``ocpp_device_model_provider`` carries it, and the bridge module ``ocpp_device_model_provider_API``
exposes it on the EVerest API, so a client needs no EVerest module of its own. Each provider is registered as its
own source, ``EXT:<module_id>``.

A provider hands OCPPmulti its *registration*: the complete set of components it owns, with characteristics,
attributes and current values. OCPPmulti asks for it at start and whenever the provider signals a change; a new
registration replaces the previous one. A write from the CSMS, already validated against the registered
characteristics, is handed to the provider, which answers ``Accepted``, ``RebootRequired`` or ``Rejected``. A
value the provider changes itself is reported to the device model like a value EVerest provides. Whether a
variable is only reported or also written follows the mutability the provider registers.

A provider owns its values, whatever ``persistent`` says; OCPPmulti stores nothing of them, so the provider keeps
them across its own restarts. Monitors the CSMS places on a provider's variables are stored by OCPPmulti and apply
again when the provider registers at the next start.

A provider may register components of its own, such as ``VendorCtrlr`` / ``DisplayBrightness``; vendor variables
on a standard component, such as a vendor variable on ``EVSE``; and standard configuration variables it takes over
from the component configuration, such as ``OCPPCommCtrlr`` / ``HeartbeatInterval``, ``SecurityCtrlr`` /
``Identity`` or a network connection profile. A provider that takes over a standard variable must register it with
the data type the standard defines, since libocpp reads it. A provider may not register what libocpp computes or
EVerest reports, such as ``AvailabilityState`` or ``EVSE`` /
``Power``; such a variable is rejected and the rest of the registration is applied.

Rules of composition
--------------------

The device model applies the same few rules to every contributor:

* **One runtime owner per variable.** A variable that another runtime source registered already is rejected.
* **A registration takes precedence over the component configuration.** A variable that both the JSON and a
  runtime source define is taken from the registration, with a warning that names it. This keeps a component
  configuration working that was shipped before the variable was registered; it is not a way to define a variable
  twice. ``OCPP_RUNTIME`` takes precedence without a warning, since the shipped component configuration defines the
  variables libocpp registers on purpose.
* **A custom variable is declared in one place.** In the JSON files or in a provider, never in both.
* **Component configuration of absent EVSEs is hidden.** JSON variables on an EVSE or connector that nobody
  registered are hidden, with a warning, so that a component configuration shipped for a different EVSE count does
  not break the station.
* **persistent describes the value, not where it is stored.** For an owner that answers writes itself, the owner
  keeps the value; for every other owner, the device model stores a persistent attribute in its database.
* **libocpp writes what it owns.** libocpp sets a variable it registered or one of the component configuration
  variables it maintains. A write of its own to a variable an external provider took over, such as the heartbeat
  interval from the ``BootNotification`` response, reaches the provider like a CSMS write.
* **Each contributor is written through its own API.** The CSMS writes across all of them; that is what OCPP is
  for. Every EVerest-side channel writes one contributor: the ``ocpp`` interface writes the component
  configuration, the configuration service writes the module configurations, the modules report the values they
  provide, and an external provider changes its own values. A write from the ``ocpp`` interface to a registered
  variable is rejected, whatever its mutability.

Startup
-------

The device model is complete before the charge point reports it for the first time. On the OCPP 2.x and the OCPP
1.6 path alike, OCPPmulti

1. collects what the connected modules report and asks every external provider for its registration;
2. opens the device model database and applies the component configuration;
3. registers the components EVerest provides, the EVerest module configuration views and the external providers;
4. registers the availability states (OCPP 2.x only);
5. constructs the charge point, which checks the composed model for completeness, and connects to the CSMS.

External providers are up when OCPPmulti starts; the stack relies on that. A registration a provider changes at
runtime is applied live: visible to ``GetVariables`` at once, and to the CSMS with its next ``GetBaseReport``.

Reads, writes and changes
-------------------------

Reads are served from memory, on every channel: the CSMS, the ``ocpp`` interface and with it the
``ocpp_consumer_API`` module see the complete composed model. A write goes to whoever owns the variable, and every
owner has its own way of reporting a change. The sequences below show both, per contributor. Each one ends the
same way: the device model updates memory and evaluates the monitors the CSMS set, which is where a ``NotifyEvent``
originates.

Component configuration
^^^^^^^^^^^^^^^^^^^^^^^

.. mermaid::

   sequenceDiagram
       participant CSMS
       participant API as ocpp interface
       participant LIB as libocpp
       participant DM as device model
       participant DB as database
       Note over CSMS,DB: Write by the CSMS
       CSMS->>DM: SetVariables HeartbeatInterval
       DM->>DM: validate, reject a ReadOnly attribute
       DM->>DB: store, if the attribute is persistent
       DM->>DM: update memory, evaluate monitors
       DM-->>CSMS: Accepted
       Note over CSMS,DB: Write through the ocpp interface
       API->>DM: set_variables ChargePointModel, a ReadOnly variable
       DM->>DB: store, if the attribute is persistent
       DM->>DM: update memory, evaluate monitors
       DM-->>API: Accepted
       Note over CSMS,DB: Write by libocpp
       LIB->>DM: ActiveNetworkProfile changed
       DM->>DB: store, if the attribute is persistent
       DM->>DM: update memory, evaluate monitors

The database is written only for persistent attributes, which component configuration attributes are unless the
JSON says otherwise. The ``ocpp`` interface is a trusted local channel and also writes ``ReadOnly`` variables,
since ``ReadOnly`` models the CSMS contract; see :ref:`Configuration access <everest_modules_handwritten_OCPPmulti>`.

EVerest module configuration views
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. mermaid::

   sequenceDiagram
       participant CSMS
       participant DM as device model
       participant V as configuration views
       participant CS as configuration service
       participant CA as Configuration API
       Note over CSMS,CA: Write by the CSMS
       CSMS->>DM: SetVariables EvseManager(connector_1).session_logging = false
       DM->>DM: validate
       DM->>V: write
       V->>CS: set parameter
       CS-->>V: RebootRequired
       V-->>DM: RebootRequired
       DM->>DM: update memory, evaluate monitors
       DM-->>CSMS: RebootRequired
       Note over CSMS,CA: Change through the Configuration API
       CA->>CS: set session_logging = true
       CS-)V: change notification
       V->>DM: value changed
       DM->>DM: update memory, evaluate monitors

Memory changes only when the configuration service accepted the write. A write from the ``ocpp`` interface to a
view is rejected; the module configuration is written through the configuration service.

Components EVerest provides
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. mermaid::

   sequenceDiagram
       participant PM as power meter
       participant M as OCPPmulti
       participant DM as device model
       participant DB as database
       participant CSMS
       Note over PM,CSMS: Value reported by the module
       PM-)M: powermeter
       M->>DM: EVSE.Power changed
       DM->>DM: update memory, evaluate monitors
       DM-)CSMS: NotifyEvent, if a monitor triggers
       Note over PM,CSMS: Write by the CSMS
       CSMS->>DM: SetVariables DCDERCtrlr.Enabled = false
       DM->>DM: validate
       DM->>DB: store, the attribute is persistent
       DM->>DM: update memory, evaluate monitors
       DM-->>CSMS: Accepted

The module reports every value it provides, the DER capability included. A reported value must fit the data type
and the value list of the variable; limits bound what the CSMS may set, not what the module reports. DER
``Enabled`` is the one variable of this group the CSMS writes, and the device model stores it. A write from the
``ocpp`` interface to any of these variables is rejected, ``Enabled`` included.

External providers
^^^^^^^^^^^^^^^^^^

.. mermaid::

   sequenceDiagram
       participant CSMS
       participant DM as device model
       participant BR as provider bridge
       participant C as client
       Note over CSMS,C: Write by the CSMS
       CSMS->>DM: SetVariables VendorCtrlr.DisplayBrightness = 50
       DM->>DM: validate against the registered characteristics
       DM->>BR: set_value
       BR->>C: set_value
       C-->>BR: Accepted
       BR-->>DM: Accepted
       DM->>DM: update memory, evaluate monitors
       DM-->>CSMS: Accepted
       Note over CSMS,C: Value changed by the client
       C-)BR: value_changed DisplayBrightness = 65
       BR->>DM: value changed
       DM->>DM: update memory, evaluate monitors
       DM-)CSMS: NotifyEvent, if a monitor triggers

The CSMS waits on the same call as the client, so a client that does not answer in time gets ``Rejected`` on its
behalf. A write from the ``ocpp`` interface to a provider's variable is rejected like for every registered
variable.

What the database stores
------------------------

The database at ``DeviceModelDatabasePath`` keeps three things:

* the component configuration: its structure and the values of its persistent attributes;
* the monitors the CSMS set, keyed by component and variable name, for every variable whoever owns it;
* the values of persistent attributes of registered variables, which today is DER ``Enabled`` only.

Nothing is kept for the EVerest module configuration views or for external providers: the configuration service
and the providers hold their values.

Monitors belong to OCPP, not to the owner of the variable, and are stored by name so that they outlive the
registration that brought the variable. When the station's configuration changes between boots, say an EVSE is
added or a ``grid_support`` connection is removed, a monitor whose variable is no longer part of the model is kept,
invisible to the CSMS, and active again with the same id when the variable returns. A monitor whose owner registers
again without the variable, or whose variable no longer supports monitoring, is removed. The DER ``Enabled`` value
the CSMS set is kept and applied again the same way.

Earlier versions kept the components EVerest provides in a second database, ``EverestDeviceModelDatabasePath``
(deprecated). At the first start the module takes it over: the DER ``Enabled`` values the CSMS set are imported into
the device model database and the file is renamed to ``*.migrated``. The steps are in
:ref:`howto-ocpp-storage-migration`.
