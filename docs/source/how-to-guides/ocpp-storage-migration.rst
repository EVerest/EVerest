.. _howto-ocpp-storage-migration:

###########################################
Migrate OCPP 1.6 Storage to the Device Model
###########################################

This is a goal-oriented how-to-guide on how to migrate an existing OCPP 1.6
configuration from JSON-based storage to the device model storage used by the
OCPP module.

Use this guide if your deployment already has an OCPP 1.6 JSON configuration,
possibly with a user-config overlay, and you want EVerest to initialize the
device model database from these values on first startup.

Why migrate storage?
====================

Previously, OCPP 1.6 and OCPP 2.x configurations were stored separately.
Switching between protocol versions could cause configuration to be lost without
explicit manual synchronization.

Migrating into the device model consolidates OCPP 1.6 configuration into the
same unified storage used by OCPP 2.0.1 and OCPP 2.1. This provides a single
source of truth across protocol versions and lays the foundation for protocol
switching without configuration loss.

.. note::

   The separate OCPP and OCPP201 modules are planned to be merged into a single
   combined module. That new module will use the device model as its only
   configuration storage backend. Migrating to the device model now means the
   same database can be reused directly when switching to OCPP201 or adopting the
   new combined module — without any further data migration step.

Before you start
================

Make sure you know which files are currently used by your OCPP module:

* the base OCPP 1.6 JSON configuration referenced by ``ChargePointConfigPath``
* the user overlay referenced by ``UserConfigPath``

Configure the migration
=======================

Set ``ConfigBackend`` to ``"device_model_with_migration"`` and provide the
following module configuration parameters:

* **ChargePointConfigPath**: base OCPP 1.6 JSON configuration (migration source)
* **UserConfigPath**: JSON overlay with persisted local and CSMS-initiated changes (migration source)
* **DeviceModelDatabasePath**: target SQLite file for the device model (created on first run)
* **DeviceModelDatabaseMigrationPath**: SQL migration directory for the device model schema
* **DeviceModelConfigPath**: component-config directory used as the structural baseline
* **Ocpp16NetworkConfigSlot** *(optional, default: 1)*: the ``NetworkConfiguration`` slot
  that network connection details (``CentralSystemURI``, ``SecurityProfile``,
  ``AuthorizationKey``, ``HostName``, ``ChargePointId``) are written to during migration.
  Set to ``0`` to skip migration of network connection details.

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
======================================

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

Point ``DeviceModelConfigMappings`` to this file.

The referenced component and variable must already exist in the component
configuration at ``DeviceModelConfigPath``. The mapping file tells the migration
where to write the value — it does not create the target variable.

Run the migration
=================

Start EVerest with the parameters above configured.

On first startup the OCPP module:

1. checks whether the device model database has already been initialized by
   inspecting its SQLite ``user_version``; if it is already initialized,
   migration is skipped and the module proceeds to step 6
2. reads the effective OCPP 1.6 configuration from the base JSON and the user overlay
3. loads the component-config baseline from ``DeviceModelConfigPath``
4. patches the OCPP 1.6 values onto that baseline
5. writes the result into the device model database at ``DeviceModelDatabasePath``
6. opens the device model backend and validates its integrity

On subsequent starts the database already has a non-zero ``user_version`` so
migration is skipped. The module updates the database from the component
configuration only (step 6 above).

Use the device model without migration
=======================================

If you do not need to carry over existing OCPP 1.6 JSON state, set
``ConfigBackend`` to ``"device_model"`` instead of
``"device_model_with_migration"``.

In this mode the module initializes and updates the device model from component
configuration only. ``ChargePointConfigPath`` is not required. This is the
recommended starting point for new integrations: the device model backend is
compatible with OCPP201 and with the upcoming combined OCPP module, so no
further storage migration will be needed when switching protocol versions or
adopting the new module.

Enable fallback to JSON on failure
====================================

Set ``EnableDeviceModelFallbackToLegacyJson`` to ``true`` to instruct the module
to fall back to the legacy JSON backend if device model initialization or the
integrity check fails at startup. This requires ``ChargePointConfigPath`` to
exist.

Use this option during a gradual migration rollout to prevent downtime if the
device model is not yet fully validated on a device.

Verify the result
=================

After the first migrated start:

* verify the device model database exists at ``DeviceModelDatabasePath``
* read back a representative set of OCPP 1.6 configuration keys through your
  normal OCPP integration path
* verify that keys previously changed by the CSMS and present in
  ``UserConfigPath`` are correctly reflected
* if custom mappings were configured, read back each mapped key and verify
  a writable mapped key can be updated through the device-model-backed interface

If a custom key is missing after migration, the most common causes are a missing
mapping entry, a mismatched component or variable name, or a target variable
that is not part of the component configuration baseline.

Rerun the migration
====================

The migration runs exactly once, gated by the SQLite ``user_version`` of the
database. To rerun it — for example after updating the source JSON configuration
or the component-config baseline — delete the database file at
``DeviceModelDatabasePath`` and restart EVerest. The absent database causes the
module to treat the next start as a first run and execute the migration again.

.. note::

   Deleting the database discards any values that were written by the CSMS at
   runtime after the initial migration. Only do this deliberately.

What to keep in mind
====================

* Relative paths for ``DeviceModelDatabasePath``, ``DeviceModelDatabaseMigrationPath``,
  ``DeviceModelConfigPath``, and ``DeviceModelConfigMappings`` are resolved against
  the OCPP module share path.
* Only keys with a built-in mapping or an explicit custom mapping entry are migrated.
  Unknown keys produce a warning log at startup and are otherwise ignored. The complete
  built-in mapping table is in
  `lib/everest/ocpp/config/v16_to_v2_mapping.md <https://github.com/EVerest/everest-core/blob/main/lib/everest/ocpp/config/v16_to_v2_mapping.md>`_.
* Some OCPP 1.6 values are represented differently in the device model than in
  the JSON configuration. For example, ``MeterPublicKeys`` is expanded into
  individual ``MeterPublicKey[N]`` variables, and
  ``ChargingScheduleAllowedChargingRateUnit`` values ``Current``/``Power`` are
  converted to ``A``/``W``.
* The ``OCPP16LegacyCtrlr`` component configuration is always required when using
  the device model backend. If it is absent from ``DeviceModelConfigPath``, the
  module injects a built-in default schema for it automatically.
* Once configuration was migrated or changed by the CSMS the configuration is
  protected from being overwritten by defaults in the component configuration.
  This ensures that values are not accidentally reset to defaults on subsequent
  starts after migration or when values were changed by the CSMS. If you want
  to deliberately set or reset a configuration value, do it via the :ref:`OCPP stable
  API <everest_modules_handwritten_ocpp_consumer_API>`.
