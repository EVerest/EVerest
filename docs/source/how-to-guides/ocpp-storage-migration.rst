.. _howto-ocpp-storage-migration:

###########################################
Migrate OCPP 1.6 Storage to the Device Model
###########################################

This is a goal-oriented how-to-guide on how to migrate an existing OCPP 1.6
configuration from JSON-based storage to the device model storage used by the
OCPP module.

Use this guide if your deployment already has an OCPP 1.6 JSON configuration,
possibly with a user-config overlay, and you want EVerest to initialize and use
the device model database from these values.

Why migrate storage?
====================

Previously, OCPP 1.6 and OCPP 2.x configurations were stored separately.
When switching between protocol versions, configuration could be lost without
explicit manual synchronization.

Storage migration consolidates OCPP 1.6 configuration into the unified device
model storage, eliminating the need for manual synchronization. This allows a single
storage for all protocol versions: OCPP1.6, OCPP2.0.1 and OCPP2.1. This unified
storage also provides the foundation for supporting runtime protocol switching
in the future without configuration loss.

Before you start
================

Make sure you know which files are currently used by your OCPP module:

* the base OCPP 1.6 JSON configuration referenced by ``ChargePointConfigPath``
* the user overlay referenced by ``UserConfigPath``

Configure the migration inputs
==============================

The OCPP module needs the following configuration parameters for the migration:

* ``ChargePointConfigPath``: base OCPP 1.6 JSON configuration
* ``UserConfigPath``: persisted JSON overlay with local and CSMS-initiated changes
* ``DeviceModelDatabasePath``: target SQLite file for the device model
* ``DeviceModelDatabaseMigrationPath``: SQL migration directory for the device model schema
* ``DeviceModelConfigPath``: component-config directory used as the migration baseline
* ``EnableOCPP16ConfigMigration``: enables/disables migration of effective OCPP 1.6 JSON values

These parameters define the source configuration, the target database, and the
baseline device model that will receive the migrated values.

The component configuration JSON files behind ``DeviceModelConfigPath`` have two
important functions:

* they define the available component/variable structure
* they may also define initial/default values

When migrating, the structure is used as patch target and migrated OCPP 1.6
values override the respective entries.

If you have non-standard OCPP 1.6 keys that must remain available after the
migration, also configure ``DeviceModelConfigMappingsPath``.

Add mappings for custom OCPP 1.6 keys
=====================================

Standard OCPP 1.6 keys are migrated through the built-in mapping table. Custom
or vendor-specific keys need an explicit YAML mapping file.

Example:

.. code-block:: yaml

   mappings:
     - ocpp16_key: ExampleConfigurationKey
       component:
         name: OCPP16LegacyCtrlr
       variable:
         name: ExampleConfigurationKey

Point ``DeviceModelConfigMappingsPath`` to this file.

Also make sure that the referenced component and variable are present in the
device model component config used by ``DeviceModelConfigPath``. The mapping
file only tells the migration where to write the value. It does not create the
target variable on its own.

Run the migration
=================

Start EVerest with the migration parameters configured.

On startup, the OCPP module:

1. reads the effective OCPP 1.6 configuration from the base JSON plus user overlay
2. loads the component config baseline for the device model
3. applies the migrated OCPP 1.6 values to that baseline
4. initializes the device model database with the resulting configuration

Normally this happens only once for a given device model database. Subsequent
starts reuse the existing migrated database.

Skip migration if desired
=========================

Migration is optional. If you do not need to preserve existing OCPP 1.6 JSON
state, you can skip migration and configure your device model directly through
component configuration JSON files and device model storage.

Set ``EnableOCPP16ConfigMigration`` to ``false`` to activate this mode.
In this mode, startup initializes/updates the device model from component
configuration only and does not apply values from OCPP 1.6 JSON configuration
files.

This is a valid approach when moving to an OCPP 2.x-oriented configuration
workflow from the start.

Verify the result
=================

After the first start, verify the migration.

- Verify device model database at ``DeviceModelDatabasePath`` now exists
- Verify configuration content:
    - read back a representative set of OCPP 1.6 configuration keys through your normal OCPP integration path
    - verify keys that were changed previously through the CSMS and existed in the ``UserConfigPath`` are correctly reflected
- Verify custom mappings if configured:
    - read back each mapped custom key
    - change a writable mapped key and verify that the update is reflected through the device-model-backed OCPP interface

If a custom key is missing after migration, the most common causes are a missing
mapping entry, a wrong component or variable name, or a target variable that is
not part of the component config baseline.

Rerun the migration deliberately
================================

If you change the source JSON configuration or the component-config baseline and
want to rebuild the device model database from scratch, enable
``ForceDeviceModelDatabaseOverride`` for one restart.

Use this option carefully. It is intended for development, validation, and
controlled upgrade steps because it overwrites the existing device model
database with a fresh migration result.

If ``EnableOCPP16ConfigMigration`` is set to ``false``, this option still
recreates the database, but from component configuration only.

After a successful rerun, disable the option again for normal operation.

What to keep in mind
====================

* Relative paths for migration resources are resolved against the OCPP module share path.
* Only keys that have a built-in mapping or an explicit custom mapping are migrated into the device model.
* Some OCPP 1.6 values are represented differently internally than in the JSON configuration (e.g. MeterPublicKeys
  is represented as MeterPublicKey{N}

If you need more background on Plug&Charge-related OCPP configuration, see
:ref:`howto-configure-pnc`.
