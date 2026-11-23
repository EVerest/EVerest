<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright Pionix GmbH and Contributors to EVerest -->

# OCPP module support

Shared EVerest/OCPP conversions, device model integration, transaction handling and error mapping.

## Configuration mappings

`device_model/configuration_mapping.hpp` loads one-to-one bindings between OCPP variables and ConfigService
parameters, validated against `schemas/configuration_mapping.schema.json`. See
`schemas/configuration_mapping.example.yaml` for the format. Invalid, inaccessible, duplicate or conflicting
bindings fail startup; an empty mapping path disables loading.

`build_configuration_views` adds optional views of unmapped parameters. Before database initialization,
`remove_mapped_configuration_variables` validates and removes mapped JSON definitions so ConfigService owns their
values. `ConfigServiceBinding` registers variables from parameter metadata and forwards writes and change
notifications. ConfigService owns persistence; the device model caches effective values. `RebootRequired` leaves
the effective value unchanged until restart. Write-only values remain unreadable and unmonitorable.

OCPPmulti uses these helpers for OCPP 1.6 and 2.x. It removes station-wide Plug & Charge variables in favor of
EVSE-scoped bindings. See [OCPPmulti configuration](../../../modules/EVSE/OCPPmulti/docs/index.rst) for deployment
instructions. Configuration mappings do not expose telemetry. `log_device_model` dumps the composed model at
verbose (`VERB`) level, excluding write-only values.