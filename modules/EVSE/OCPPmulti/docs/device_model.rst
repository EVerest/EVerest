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

Every variable has exactly one owner. libocpp's device model composes the model from what the owners
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
     - Owner
     - Brings
     - Values live in
   * - Component configuration
     - ``COMPONENT_CONFIG``
     - the components and variables defined in the JSON files under ``DeviceModelConfigPath``
     - the device model database
   * - libocpp
     - ``OCPP_STACK``
     - ``AvailabilityState`` of the charging station, the EVSEs and the connectors, unless the component
       configuration defines it
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
     - their own components, vendor variables on standard components, and standard configuration variables the
       component configuration leaves out
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
       component configuration leaves out

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

libocpp registers ``AvailabilityState`` of the charging station, of every EVSE and of every connector under the owner
``OCPP_STACK``, each unless the component configuration defines it. The value comes from the same state machine
that drives ``StatusNotification``, so ``GetVariables`` and ``StatusNotification`` always agree. The standardized
``ChargingStation`` component configuration defines the charging station's ``AvailabilityState``, and component
configurations written for earlier versions may define those of the EVSEs and connectors; libocpp leaves such a
definition in place and writes its value in the same way. The OCPP 1.6 stack does not use them, so the OCPP 1.6
path skips the registration.

Components EVerest provides
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The components that describe the station's hardware and the connected EV are built from what the EVerest modules
report and registered under the owner ``EVEREST`` at every start. An EVSE that is added or removed shows up at
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

The configuration of the other EVerest modules is visible in the device model under the owner ``EVEREST_CONFIG``:

* One component per module, named after the module type, with the module id as instance and the EVSE and
  connector taken from the module's mapping: ``EvseManager`` with instance ``connector_1`` on EVSE 1.
* One variable per configuration parameter, named like the parameter; a parameter of an implementation is
  prefixed with the implementation id.
* Data type, unit, limits and mutability come from the manifest. ``WriteOnly`` parameters cannot be monitored,
  and reports leave out their values.
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
exposes it on the EVerest API, so a client needs no EVerest module of its own. Each provider is registered as an
owner of its own, ``EXT:<module_id>``.

A provider hands OCPPmulti its *registration*: the complete set of components it owns, with characteristics,
attributes and current values. OCPPmulti asks for it at start and whenever the provider signals a change; a new
registration replaces the previous one. A write from the CSMS, already validated against the registered
characteristics, is handed to the provider, which answers ``Accepted``, ``RebootRequired`` or ``Rejected``. A
value the provider changes itself is reported to the device model like a value EVerest provides. Whether a
variable is only reported or also written follows the mutability the provider registers.

A provider owns its values, whatever ``persistent`` says; OCPPmulti stores nothing of them, so the provider keeps
them across its own restarts. Monitors the CSMS places on a provider's variables are stored by OCPPmulti and apply
again when the provider registers at the next start.

A provider may register components of its own, such as ``VendorCtrlr`` / ``DisplayBrightness``; vendor variables on
a standard component, such as a vendor variable on ``EVSE``; and standard configuration variables the component
configuration leaves out, such as ``OCPPCommCtrlr`` / ``HeartbeatInterval``. To hand such a variable to a provider,
the integrator places a component configuration of the same component without the variable under
``component_config/custom``, which replaces the standardized one; the variable's monitors carry over to the
provider's registration. A provider that registers a standard variable must use the data type the
standard defines, since libocpp reads it. A provider may not register what the component configuration defines,
libocpp writes from its own state or EVerest reports, such as ``AvailabilityState``, ``SecurityCtrlr`` /
``Identity``, a network connection profile or ``EVSE`` / ``Power``; such a variable is rejected and the rest of the
registration is applied.

Rules of composition
--------------------

The device model applies the same few rules to every contributor:

* **One definition per variable.** A variable is defined by the component configuration or by one runtime owner.
  A registration of a variable that the component configuration or another runtime owner defines is rejected, and
  the rest of the registration is applied. A component may combine both: the component configuration can add
  variables to ``EVSE`` or ``Connector``, as long as it leaves out those EVerest registers.
* **Conflicts are errors.** A variable defined twice is a conflict. OCPPmulti logs them at every start as one error that
  names each variable and what to remove from the component configuration. By default the station starts anyway,
  so that a station whose component configuration is outdated after an update stays reachable for the CSMS; with
  ``FailOnDeviceModelConflicts`` the module does not start.
* **A variable can move from the component configuration to a registration.** When an update removes a variable
  from the component configuration, or the whole file of its component, and a runtime owner registers it, its
  monitors carry over: the device model keeps them by name when the variable leaves the component configuration and
  applies them when the owner registers it. The value is not kept; the owner registers its own.
* **persistent describes the value, not where it is stored.** For an owner that answers writes itself, the owner
  keeps the value; for every other owner, the device model stores a persistent attribute in its database.
* **A read returns the value in effect.** ``Accepted`` stores the value in the device model, whoever owns the
  variable, so a ``GetVariables`` after a ``SetVariables`` returns the value the CSMS set. After ``RebootRequired`` the
  device model keeps the previous value: the owner holds the value written for the next start and registers with it
  then. OCPP 2.x leaves open what a read returns until then; B10.FR.01 allows either answer for a
  ``NetworkConfigurationPriority`` change that applies after a reboot.
* **libocpp writes what it owns.** libocpp sets a variable it registered or one of the component configuration
  variables it maintains. A write of its own to a variable an external provider took over, such as the heartbeat
  interval from the ``BootNotification`` response, reaches the provider like a CSMS write.
* **Every write reaches the owner.** The CSMS writes across all contributors; that is what OCPP is for. The
  ``ocpp`` interface, and with it the ``ocpp_consumer_API`` module, is the local channel, the integrator's own code
  that OCPPmulti trusts: it writes ``ReadOnly`` variables of the component configuration, which the integrator
  maintains that way, and writes a registered variable like the CSMS does, through its owner
  and only when it is ``ReadWrite``. A ``ReadOnly`` registered variable is a value its owner reports, so a write to
  it is rejected: the modules report the values they provide, and an external provider changes its own values.

Startup
-------

The device model is complete before the charge point reports it for the first time. On the OCPP 2.x and the OCPP
1.6 path alike, OCPPmulti

1. collects what the connected modules report and asks every external provider for its registration;
2. opens the device model database and applies the component configuration;
3. registers the components EVerest provides, the EVerest module configuration views and the external providers;
4. registers the availability states (OCPP 2.x only);
5. reports the conflicts between the component configuration and the registrations, and stops if
   ``FailOnDeviceModelConflicts`` is set;
6. constructs the charge point, which checks the composed model for completeness, and connects to the CSMS.

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
JSON says otherwise. The ``ocpp`` interface is the local channel and also writes ``ReadOnly`` variables, since
``ReadOnly`` models the CSMS contract; see :ref:`Configuration access <handwritten_ocppmulti_configuration-access>`.

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
       DM->>DM: keep memory
       DM-->>CSMS: RebootRequired
       CSMS->>DM: GetVariables EvseManager(connector_1).session_logging
       DM-->>CSMS: true, the value the module runs with
       Note over CSMS,CA: Read through the Configuration API until the restart
       CA->>CS: get session_logging
       CS-->>CA: true, the value the module runs with
       Note over CSMS,CA: Change through the Configuration API
       CA->>CS: set session_logging = true
       CS-)V: change notification
       V->>DM: value changed
       DM->>DM: update memory, evaluate monitors

Memory changes only when the configuration service answered ``Accepted``. After ``RebootRequired`` the module keeps
running with its previous value until the next start, and so does the device model: the CSMS reads the previous value
until the views register the new one at the next start, like for every other owner. Until then the Configuration API
answers a read with the value the module runs with as well, unless the request sets ``force_read_from_db``. A change
through the Configuration API is reported to the device model when the configuration service delivers it, whether the
module applies it at once or at its next start. A change the configuration service delivers
after the module received the answer to a write is taken as newer than that write. A change delivered while the write
is in flight is taken as older, whether the service applied it before or after the write, so the write wins. A write from the ``ocpp`` interface to a ``ReadWrite`` view reaches the configuration service
like a write from the CSMS.

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
``Enabled`` is the one variable of this group the CSMS, or the ``ocpp`` interface, writes, and the device model
stores it. A write to any other variable of this group is rejected, from the ``ocpp`` interface as well, since the
module reports them.

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
behalf. A write from the ``ocpp`` interface to a ``ReadWrite`` variable of a provider reaches the provider like a
write from the CSMS.

What the database stores
------------------------

The database at ``DeviceModelDatabasePath`` keeps three things:

* the component configuration: its structure and the values of its persistent attributes;
* the monitors the CSMS set, keyed by component and variable name, for every variable whoever owns it;
* the values of persistent attributes of registered variables, which today is DER ``Enabled`` only.

Nothing is kept for the EVerest module configuration views or for external providers: the configuration service
and the providers hold their values.

Monitors belong to OCPP, not to the owner of the variable, and are stored by name so that they outlive the
registration that brought the variable. Until the owner registers, its variables are not part of the model and
their monitors are kept, invisible to the CSMS, and active again with the same id once the owner registers the
variable. A monitor whose owner registers without the variable, or whose variable no longer supports monitoring, is
removed. The DER ``Enabled`` value the CSMS set follows the same rule. The removal happens at every start in OCPP 2.x
mode, after EVerest registered its components, so removing an EVSE or a ``grid_support`` connection, or switching an
EVSE between AC and DC, removes the monitors and the ``Enabled`` value of the variables that go away. OCPP 1.6 has no
DER controllers; a start in OCPP 1.6 mode leaves the stored ``Enabled`` values in place, unused, and they apply at a
later start in OCPP 2.x mode.

Earlier versions kept the components EVerest provides in a second database, ``EverestDeviceModelDatabasePath``
(deprecated). At the first start the module takes it over: the DER ``Enabled`` values the CSMS set are imported into
the device model database and the file is renamed to ``*.migrated``. The steps are in
:ref:`howto-ocpp-storage-migration`.
