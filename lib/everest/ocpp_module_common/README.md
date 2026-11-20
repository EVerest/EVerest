<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright Pionix GmbH and Contributors to EVerest -->

# OCPP module support

Shared EVerest/OCPP conversions, device model integration, transaction handling and error mapping.

## Configuration mappings

`device_model/configuration_mapping.hpp` loads an explicit YAML `mappings` sequence. Each entry binds an OCPP variable
through `ocpp: {component, variable}` to a ConfigService parameter through
`everest: {module_id, parameter, implementation_id?}`. Component and variable identities use OCPP names, optional
instances and optional component EVSE/connector scope. Parameters and targets are one-to-one; comparison of target
names and instances is case-insensitive, like the device model. Unknown fields and invalid or inaccessible sources
fail startup. The keys `configuration`, `source` and `target` are not accepted as aliases.

The format is defined in `schemas/configuration_mapping.schema.json`; a complete example is provided in
`schemas/configuration_mapping.example.yaml`. The loader validates every file against the supplied draft-07 JSON
schema before converting entries. Schema validation checks required fields, types, unknown fields, nonempty
identifiers, OCPP identifier lengths and positive 32-bit EVSE/connector ids. Source resolution and one-to-one
ownership checks then validate the mappings against the deployment. The caller supplies the schema path;
OCPPmulti uses `configuration_mapping.schema.json` from its module share directory, independently of the mapping
file's location or `$schema` reference. A missing or invalid schema fails loading. An empty mapping path skips both
files. CMake and Bazel supply the schema and `configuration_mapping.yaml` in the OCPPmulti share directory;
packaging must keep the schema aligned with the loader. The mapping file targets `auth` and two managers named
`evse_manager_1` and `evse_manager_2`, with EVSE-scoped Plug & Charge variables. The standard OCPP 1.6, OCPP 2.x and
combined SIL configurations enable it and grant access to those modules. Other deployments must adapt the module
ids, EVSE scope and access grants. The example remains in the repository for reference and tests; it is not installed.

All shipped OCPP configurations select the full mapping file and grant access to Auth and both EvseManagers.
Custom deployments must supply scoped bindings for every participating EVSE.
Initial values come from ConfigService, not retained OCPP values.

OCPPmulti does not propagate Auth or Plug & Charge settings through legacy commands. Its database preparation
removes unscoped `PnCEnabled`, `CentralContractValidationAllowed` and `ContractCertificateInstallationEnabled`
variables while preserving other ISO15118Ctrlr settings. Explicit unscoped mappings for those names are rejected.
Scoped values have no global fallback, aggregation or fan-out. OCPP 1.6 unscoped keys cannot address them; use
canonical component/EVSE/variable access or explicit per-EVSE custom key mappings.

`build_configuration_views` combines these mappings with optional automatic views of **unmapped** parameters.
`remove_mapped_configuration_variables` validates the JSON target datatype and Actual attribute, then removes the
target definition before `InitDeviceModelDb` runs. The database initializer removes the old value while retaining
name-keyed monitors. Missing targets need no JSON definition: `ConfigServiceBinding` registers them from parameter
metadata. Other runtime owners and libocpp-reserved state cannot be replaced by mappings.

`ConfigServiceBinding` is the runtime value provider. OCPP writes go through ConfigService, and accepted configuration
changes update the OCPP view. ConfigService owns persistence; the device model only caches effective values.
Writes return the ConfigService result. `RebootRequired` leaves the effective cached value alone.
Accepted external changes enter through `notify_value_changed`, which notifies listeners and monitors without
calling the provider or persisting another copy. Reports read this composed in-memory model. `log_device_model`
logs the composed model at verbose (`VERB`) level after registration for both OCPP protocol paths, without a
module configuration parameter. The dump starts with the component-variable entry count, followed by each
entry as JSON. It uses FullInventory reports, which scrub write-only values.

Both OCPPmulti protocol paths use these helpers. The operator-facing configuration and YAML example are documented
in `modules/EVSE/OCPPmulti/docs/index.rst` from the repository root. Telemetry producers remain separate runtime
owners: configuration exposure does not imply telemetry discovery or per-sample database writes.