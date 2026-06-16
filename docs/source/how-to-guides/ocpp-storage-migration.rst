.. _howto-ocpp-storage-migration:

####################################
Migrate to the Combined OCPP Module
####################################

This is a goal-oriented how-to guide on migrating an existing deployment from
the separate ``OCPP`` (1.6) or ``OCPP201`` module to the combined OCPP module
:ref:`OCPPmulti <everest_modules_OCPPmulti>`, which supports OCPP 1.6, 2.0.1,
and 2.1 from a single module instance.

The migration has up to three parts:

* **Module-level switchover**: replace the old module with OCPPmulti in
  your EVerest configuration and map the module config keys.
* **Storage migration** (OCPP 1.6 only, optional): carry an existing OCPP 1.6
  JSON configuration over into the device model database.
* **Module wiring**: check the connections (``requires``/``provides``) of the
  module in your EVerest configuration.

Module-level switchover
=======================

Change the module type to ``OCPPmulti`` in your EVerest configuration and map
the module config keys as described below for the module you are coming from.

Coming from OCPP201
-------------------

All ``OCPP201`` config keys exist unchanged in OCPPmulti:
``MessageLogPath``, ``CoreDatabasePath``, ``DeviceModelDatabasePath``,
``EverestDeviceModelDatabasePath``, ``DeviceModelDatabaseMigrationPath``,
``DeviceModelConfigPath``, ``EnableExternalWebsocketControl``,
``MessageQueueResumeDelay``, ``CompositeScheduleIntervalS``,
``RequestCompositeScheduleDurationS``, ``RequestCompositeScheduleUnit``,
``DelayOcppStart``, ``ResetStopDelay``, and ``CustomMrecErrorMapPath``.

One new module config key is relevant: ``Mode`` selects the protocol
generation. Its default ``Only2`` runs OCPP 2.x, matching the behavior of the
old ``OCPP201`` module, so no change is needed. Which OCPP 2.x version (2.0.1
or 2.1) is offered to the CSMS during the websocket handshake is still
controlled by the device model variable ``SupportedOcppVersions`` of the
``InternalCtrlr`` component (a comma-separated preference list, e.g.
``ocpp2.1,ocpp2.0.1``). Everything else in an existing ``OCPP201``
configuration carries over without changes.

Coming from OCPP (1.6)
----------------------

.. list-table:: Renamed keys
   :header-rows: 1
   :widths: 50 50

   * - OCPP module key
     - OCPPmulti key
   * - ``PublishChargingScheduleIntervalS``
     - ``CompositeScheduleIntervalS``
   * - ``PublishChargingScheduleDurationS``
     - ``RequestCompositeScheduleDurationS``

.. list-table:: Dropped keys
   :header-rows: 1
   :widths: 50 50

   * - OCPP module key
     - Replacement
   * - ``ConfigBackend``
     - None. The device model is the only configuration storage backend; there
       is no ``legacy`` JSON backend. The ``device_model_with_migration``
       variant is replaced by the boolean ``EnableLegacyConfigMigration``.
   * - ``EnableDeviceModelFallbackToLegacyJson``
     - None. There is no fallback to the JSON backend.

.. list-table:: New keys
   :header-rows: 1
   :widths: 30 70

   * - Key
     - Purpose
   * - ``EnableLegacyConfigMigration``
     - Boolean. If ``true``, a one-time migration of the legacy OCPP 1.6 JSON
       config into the device model database runs on the first startup, while
       the database has not been initialized yet. See
       `Storage migration (OCPP 1.6)`_.
   * - ``Mode``
     - Selects the protocol generation. The default is ``Only2`` (OCPP 2.x),
       so to keep a deployment on OCPP 1.6, set it to ``Only1.6``.

The protocol generation is selected via the new module config key ``Mode``.
Since its default is ``Only2``, an OCPP 1.6 deployment must set
``Mode: Only1.6`` explicitly.

Unchanged keys: ``ChargePointConfigPath``, ``UserConfigPath``,
``DatabasePath``, ``DeviceModelDatabasePath``,
``DeviceModelDatabaseMigrationPath``, ``DeviceModelConfigPath``,
``DeviceModelConfigMappings``, ``Ocpp16NetworkConfigSlot``,
``EnableExternalWebsocketControl``, ``MessageLogPath``,
``MessageQueueResumeDelay``, ``RequestCompositeScheduleUnit``,
``DelayOcppStart``, and ``ResetStopDelay``.

Example module config for an OCPP 1.6 deployment that migrates its existing
JSON configuration:

.. code-block:: yaml

   ocpp:
     module: OCPPmulti
     config_module:
       Mode: Only1.6
       EnableLegacyConfigMigration: true
       ChargePointConfigPath: ocpp-config.json
       UserConfigPath: user_config.json
       DeviceModelDatabasePath: device_model_storage.db
       DeviceModelDatabaseMigrationPath: device_model_migrations
       DeviceModelConfigPath: component_config

Storage migration (OCPP 1.6)
============================

OCPPmulti stores OCPP 1.6 configuration in the device model database,
the same unified storage used for OCPP 2.0.1 and OCPP 2.1. This provides a
single source of truth across protocol versions and enables protocol switching
without configuration loss.

If your deployment already has an OCPP 1.6 JSON configuration, possibly with a
user-config overlay, set ``EnableLegacyConfigMigration`` to ``true`` to have
EVerest initialize the device model database from these values on first
startup.

.. note::

   The legacy-JSON migration is one option, not a requirement. An OCPP 1.6
   deployment can skip the JSON files entirely and configure everything
   through the component configs / the device model from the start, see the
   OCPP 1.6 configuration section of the OCPPmulti
   :ref:`module documentation <everest_modules_OCPPmulti>`.
   With ``EnableLegacyConfigMigration`` at its default ``false``, the device
   model is initialized from the component configs only and
   ``ChargePointConfigPath`` is not read.

Configure the migration
-----------------------

Set ``EnableLegacyConfigMigration`` to ``true`` and provide the following
module configuration parameters:

* **ChargePointConfigPath**: base OCPP 1.6 JSON configuration (migration
  source, must exist)
* **UserConfigPath**: JSON overlay with persisted local and CSMS-initiated
  changes (migration source; an empty one is created if missing)
* **DeviceModelDatabasePath**: target SQLite file for the device model
  (created on first run)
* **DeviceModelDatabaseMigrationPath**: SQL migration directory for the device
  model schema
* **DeviceModelConfigPath**: component-config directory used as the structural
  baseline
* **Ocpp16NetworkConfigSlot** *(optional, default: 1)*: the
  ``NetworkConfiguration`` slot that network connection details
  (``CentralSystemURI``, ``SecurityProfile``, ``AuthorizationKey``,
  ``HostName``, ``ChargePointId``) are written to during migration. Any
  existing attribute values in the target slot are overwritten. Set to ``0``
  to skip migration of network connection details entirely.

These parameters define the source configuration, the target database, and the
structural baseline that receives the migrated values.

The component configuration JSON files at ``DeviceModelConfigPath`` serve a
dual purpose:

* they define the available component/variable structure
* they may define initial/default values

During migration, the OCPP 1.6 values are patched on top of this baseline.
Values present in the OCPP 1.6 JSON take precedence over defaults in the
component configuration.

Add mappings for custom OCPP 1.6 keys
-------------------------------------

Standard OCPP 1.6 keys are migrated through the built-in mapping table. The
full list of built-in key mappings is documented in
`lib/everest/ocpp/config/v16_to_v2_mapping.md <https://github.com/EVerest/everest-core/blob/main/lib/everest/ocpp/config/v16_to_v2_mapping.md>`_
in the repository. Custom or vendor-specific keys that are not covered there
need an explicit YAML mapping file.

Example:

.. code-block:: yaml

   mappings:
     - ocpp16_key: ExampleConfigurationKey
       component:
         name: OCPP16LegacyCtrlr
       variable:
         name: ExampleConfigurationKey

Point ``DeviceModelConfigMappings`` to this file. The mappings are applied
both during the one-time migration and when serving the device-model-backed
configuration at runtime.

The referenced component and variable must already exist in the component
configuration at ``DeviceModelConfigPath``. The mapping file tells the
migration where to write the value — it does not create the target variable.

Run the migration
-----------------

Start EVerest with the parameters above configured.

On first startup the module:

1. checks whether the device model database has already been initialized; if
   so, migration is skipped and the module updates the database from the
   component configuration only
2. reads the effective OCPP 1.6 configuration from the base JSON and the user
   overlay
3. loads the component-config baseline from ``DeviceModelConfigPath``
4. patches the OCPP 1.6 values onto that baseline
5. writes the result into the device model database at
   ``DeviceModelDatabasePath``
6. opens the device model backend and validates its integrity

On subsequent starts the database is already initialized. The legacy JSON is not read,
``EnableLegacyConfigMigration`` is ignored, and no migration is performed.

Differences to the old OCPP module's migration path
---------------------------------------------------

If you previously used the ``OCPP`` module's device-model support, note these
behavioral differences in OCPPmulti:

* There is no ``device_model``-vs-``legacy`` backend choice: the device model
  is always used. The only decision is whether the one-time JSON migration
  runs (``EnableLegacyConfigMigration``).
* There is no fallback to the JSON backend if device model initialization or
  the integrity check fails (``EnableDeviceModelFallbackToLegacyJson`` does
  not exist).
* ``NumberOfConnectors`` is kept in sync with the number of connected EVSEs
  automatically, both during migration and when initializing from component
  configs.

Verify the result
-----------------

After the first migrated start:

* verify the device model database exists at ``DeviceModelDatabasePath``
* read back a representative set of OCPP 1.6 configuration keys through your
  normal OCPP integration path
* verify that keys previously changed by the CSMS and present in
  ``UserConfigPath`` are correctly reflected
* if custom mappings were configured, read back each mapped key and verify
  a writable mapped key can be updated through the device-model-backed
  interface

If a custom key is missing after migration, the most common causes are a
missing mapping entry, a mismatched component or variable name, or a target
variable that is not part of the component configuration baseline.

Rerun the migration
-------------------

The migration runs exactly once when the device model database has not been initialized.
To rerun it — for example after updating the source JSON
configuration or the component-config baseline — delete the database file at
``DeviceModelDatabasePath`` and restart EVerest. The absent database causes
the module to treat the next start as a first run and execute the migration
again.

.. note::

   Deleting the database discards any values that were written by the CSMS at
   runtime after the initial migration. Only do this deliberately.

What to keep in mind
--------------------

* Relative paths for ``DeviceModelDatabasePath``,
  ``DeviceModelDatabaseMigrationPath``, ``DeviceModelConfigPath``, and
  ``DeviceModelConfigMappings`` are resolved against the module share path.
* Only keys with a built-in mapping or an explicit custom mapping entry are
  migrated. Unknown keys produce a warning log at startup and are otherwise
  ignored. The complete built-in mapping table is in
  `lib/everest/ocpp/config/v16_to_v2_mapping.md <https://github.com/EVerest/everest-core/blob/main/lib/everest/ocpp/config/v16_to_v2_mapping.md>`_.
* Some OCPP 1.6 values are represented differently in the device model than in
  the JSON configuration. For example, ``MeterPublicKeys`` is expanded into
  individual ``MeterPublicKey[N]`` variables, and
  ``ChargingScheduleAllowedChargingRateUnit`` values ``Current``/``Power`` are
  converted to ``A``/``W``.
* The ``OCPP16LegacyCtrlr`` component configuration is always required for
  OCPP 1.6 operation. If it is absent from ``DeviceModelConfigPath``, the
  module injects a built-in default schema for it automatically.
* Once configuration was migrated or changed by the CSMS the configuration is
  protected from being overwritten by defaults in the component configuration.
  This ensures that values are not accidentally reset to defaults on
  subsequent starts after migration or when values were changed by the CSMS.
  If you want to deliberately set or reset a configuration value, do it via
  the :ref:`OCPP stable API <everest_modules_handwritten_ocpp_consumer_API>`.

EVerest config (module wiring)
==============================

The ``requires`` surface of OCPPmulti is the union of the old modules',
so existing connections carry over:

* ``reservation`` is optional (0..1 connections, as in ``OCPP201``; the old
  ``OCPP`` module required it).
* ``charger_information`` is supported (0..1 connections, as in ``OCPP``; the
  old ``OCPP201`` module did not have it).
* All other requirements (``auth``, ``data_transfer``, ``display_message``,
  ``evse_energy_sink``, ``evse_manager``, ``extensions_15118``, ``security``,
  ``system``) are identical to both old modules.

On the ``provides`` side, OCPPmulti offers the same interfaces as
``OCPP201`` (``auth_validator``, ``auth_provider``, ``data_transfer``,
``ocpp_generic``, ``session_cost``); the 1.6-specific ``main``
(``ocpp_1_6_charge_point``) interface of the old ``OCPP`` module is not
provided.
