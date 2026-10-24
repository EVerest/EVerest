# OCPP 2.0.1 and 2.1: Device model initialization and inserting of config values

If there is no custom database used for the device model, and 'initialize_device_model' is set to true in the 
constructor of ChargePoint, the device model will be created or updated when ChargePoint is created. This document will
give more information about the files you need and what will be updated when the 'initialize_device_model' is set
to true.


## Database, component config and config file paths

Along with the 'initialize_device_model' flag, a few paths must be given to the constructor:
- The path of the device model migration files (normally `resources/v2/device_model_migration_files`).
- The path of the device model database.
- The path of the directory with the device model config. There should be two directories in it: 'standardized' and 
  'custom', both containing device model config.


## Component config

When the database is created for the first time, it will insert all components, variables, characteristics and 
attributes from the component config. 

An attribute may set `persistent` and `constant` next to `type`, `mutability` and `value`. An attribute without
`persistent` is persistent, one without `constant` is not constant. A change of either flag in the component config is
applied to the database at the next start, like a change of the mutability.


## Update config values

Each time the ChargePoint class is instantiated, the component config is read and the values will be set to the database 
accordingly. Only the initial values will be set to the values in the component config. So if for example the CSMS 
changed a value, it will not be updated to the value from the component config file.


## Values in memory

`DeviceModel` reads the whole device model, including the values of all attributes, from the database when it is
created, and serves every read from memory. A write goes to the database first and to memory only after the database
accepted it; if the database rejects the write, the request is rejected and memory keeps the old value. Attributes that
are not persistent are held in memory only: a write to such an attribute is not stored, and the attribute starts with
the value the database holds at the next start. That is the value of the component config, unless another source wrote
the attribute while it was persistent: such a value is kept, because the component config only replaces values whose
source is `default`.

As a consequence, the database must only be edited while the charging station software is stopped. Changes made to the
database while it runs are not seen until the next start.

The exception is a variable whose `VariableMetaData::owner` is set to something other than `OCPP`: it is held by the
storage of that owner, which changes its value without `DeviceModel`, so its values are read from the storage and every
write goes to the storage. A variable without an `owner`, or with the owner `OCPP`, belongs to the OCPP storage and is
held in memory. The component tables do not record the owner; migration 4 drops `VARIABLE.SOURCE`. The EVerest device
model storage of the OCPP modules reports every variable it holds with the owner `EVEREST`, and the composed storage of
those modules routes the reads and writes of such a variable to it. Monitors are the exception to this routing: the
composed storage keeps all of them in the OCPP storage.

## Update component config

To update a component, just place the correct json component config in the `component_config/custom` or 
`component_config/standardized` folder. When restarting the software, it will:
- Check if there are Components in the database that are not in the component config's. Those are kept in the
  database, together with their variables and monitors, and a warning is logged for each of them.
- Check if there are Components in the component config's that are not in the database. Those will be added.
- Check if anything has changed inside the Component (`Variable`, `Characteristics` or `Attributes`). 
  Those will be removed, changed or added to the database as well. 
  
Note: When the `evse_id` or `connector_id` of a component is changed, the component with the new key is added. The
component with the old key, including its monitors, stays in the database and is reported by the warning above.

Note: OCPP requires EVSE and Connector numbering starting from 1 counting upwards.

Note: There should be no duplicate components or variables in the component config files.

## Database schema

The device model database (schema version 4) has two kinds of tables.

The component tables `COMPONENT`, `VARIABLE`, `VARIABLE_CHARACTERISTICS` and `VARIABLE_ATTRIBUTE` hold the structure
from the component config and the values of its variables. They are linked by foreign keys, and removing a component
or variable removes its characteristics and attributes with it.

Two tables are keyed by the identity of a component and variable instead of a foreign key, so they can hold data of
variables that have no row in the component tables:

| Table | Holds |
|---|---|
| `VARIABLE_MONITORING` | every monitor. `ID` is `AUTOINCREMENT`, so monitor ids stay unique and are never reused. `OWNER` records the owner of a variable registered at runtime. It is NULL for variables of the component config. |
| `VARIABLE_ATTRIBUTE_VALUE` | attribute values of variables without a row in the component tables, written by `DeviceModelStorageSqlite::set_stored_value`. `set_variable_attribute_value` rejects a variable without a row. The primary key is the name key plus the attribute type. `OWNER` records the owner of the variable when the value was stored. |

The name key consists of the columns `COMPONENT_NAME`, `COMPONENT_INSTANCE`, `EVSE_ID`, `CONNECTOR_ID`,
`VARIABLE_NAME` and `VARIABLE_INSTANCE`. Names and instances compare case-insensitively, like OCPP identifiers. SQLite
treats NULLs as distinct in keys, so an absent instance is stored as `''` and an absent EVSE or connector id as `0`. A
component config with an empty component or variable instance, or with an EVSE or connector id of `0`, therefore stops
the initialization with an error that names the file. Other invalid component configs are skipped with a warning; a
skipped custom config is then replaced by the standardized config of the same component, if there is one.

Monitors are not linked to variable rows. Removing a variable from the component config of a component that is still
configured removes all of its monitors without an owner, custom monitors included. Monitors with an owner belong to a
variable registered at runtime and are kept. Components that are no longer configured are kept with their monitors,
see [Update component config](#update-component-config).
`DeviceModelStorageSqlite::get_device_model` attaches monitors to the variables of the component tables, and
`get_all_monitors` returns all of them.

Migration 4 moves the monitors of schema 3 into the name-keyed table. Monitors keep their ids, and the id sequence is
kept, so ids of monitors deleted before the migration are not handed out again. It also drops `VARIABLE.SOURCE`: the
component tables hold only the component config, and the `source` key of a component config variable is not read. The
down migration keeps only the monitors whose variable has a row in the component tables, drops the values in
`VARIABLE_ATTRIBUTE_VALUE` and restores `VARIABLE.SOURCE` empty.

## Required variables

There are some required Variables, which can be found in the OCPP spec.  
Some `Variables` are only required if the `Component` is `Available`, for example `Reservation` and `Smart Charging`. 
There are  some Components that are always required because that is how libocpp works: `AlignedDataCtrlr` and 
`SampledDataCtrlr`.  
When libocpp is started and initialized, all required Variables will be checked and an DeviceModelError is thrown if 
one of the required Variables is not there.

This also implies, that if you write code that needs a required `Variable`, when trying to get that variable with 
`DeviceModel::get_value(...)`, you should first check if the Component that Variable belongs to is `Available`.

## DER control: construct-on-enable

The DER functional block (`ocpp::v21::DERControl`) follows a stricter variant of the `Available`-gated pattern above.
Admission and block construction are strictly opt-in. It is **not** constructed eagerly during `ChargePoint`
initialization. A component counts as active only when `Available == true` **and** `Enabled == true`, evaluated per EVSE.
Both variables are read with `value_or(false)`, so a missing `Available` or `Enabled` variable means disabled. Static
device-model configs must therefore define `Enabled` (typically `"true"`) alongside `Available` for the DER block to build
at boot; a config that defines only `Available` leaves the block unbuilt. DC directive admission likewise requires
`Available == true` (in addition to `Enabled` and `ModesSupported`). The lazy construction path still exists for
components enabled later at runtime: `ChargePoint` registers a device-model variable listener on the `DCDERCtrlr` /
`ACDERCtrlr` `Available` and `Enabled` variables and builds the DER block the first time a component of either type
becomes active via `SetVariables`. Integrators who need DER control active from the start must ensure at least one EVSE
has an active `DCDERCtrlr` / `ACDERCtrlr` component; otherwise the DER block stays unbuilt and DER messages are answered
with `NotImplemented` until a component is enabled.
