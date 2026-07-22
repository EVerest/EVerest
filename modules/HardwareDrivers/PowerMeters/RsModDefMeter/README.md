# RsModDefMeter

A generic EVerest `powermeter` driver backed by a [ModDef](https://github.com/ModDefOrg/moddef)
device profile. One binary drives **any** conforming meter: the register map, framing, scaling,
polling and (where supported) the signed-transaction procedure all come from the profile, not from
per-model code.

## What it does

- **Metering (all profiles).** Publishes the `powermeter` variable by querying the profile's
  measurand-tagged points (`base_quantity` + phase / direction / aggregation) and rescaling each to
  the unit the EVerest `Powermeter` field expects (Wh, W, var, V, A, Hz). Points the profile doesn't
  provide are left absent.
- **Signed transactions (typed-register OCMF profiles).** If the profile declares a start/stop
  command — matched by the `command_ref` role tag `ocmf:start_transaction` / `ocmf:stop_transaction`,
  or by the command id `start_transaction` / `stop_transaction` — the module maps the EVerest
  `TransactionReq` onto that command's typed params and runs it through ModDef's command construct
  (issue [moddef#2](https://github.com/ModDefOrg/moddef/issues/2)). Enum fields are resolved against
  the profile's own enum tables (OCMF value names match), and the OCMF identification flags are routed
  to their per-group registers. The stop command's OCMF result becomes a `SignedMeterValue`.
- **Capability discovery.** A profile with no transaction commands publishes plain metering and
  returns `NOT_SUPPORTED` from `start`/`stop_transaction`.

Exemplar profile: **Carlo Gavazzi EM580** (`devices/energy-meter/carlo-gavazzi-em580` in the ModDef
`devices` repo), which exposes each OCMF field as its own register.

## Scope & limitations

- Transactions require a **typed-register OCMF** profile (each OCMF field is a register), matching the
  EVerest `TransactionReq` field names. Opaque-blob meters (Bauer BSM, Iskra WM3M4C) need caller-side
  payload composition, which is not declarative — those keep their bespoke drivers, and this module
  returns `NOT_SUPPORTED` for them (see moddef#3 for the rationale).
- Access is **Modbus RTU via `serial_communication_hub`** only. Modbus TCP / an owned transport is a
  future option (moddef-tokio-modbus can back the same `Transport` trait).
- Discrete-input reads aren't offered by the hub; `COMPOSED` (multi-register mantissa/exponent)
  measurand points aren't readable through the ModDef facade yet — such fields publish as absent.

## Configuration

| key | type | default | meaning |
|---|---|---|---|
| `profile` | string | — | path to the ModDef profile (`.moddef`, `.moddef.yaml`, `.moddef.json`) |
| `device_id` | string | `""` | device to select when the profile has more than one |
| `powermeter_device_id` | int | 1 | Modbus unit id on the bus |
| `read_meter_values_interval_ms` | int | 5000 | metering publish interval |
| `communication_errors_threshold` | int | 10 | consecutive read failures before `CommunicationFault` |

## Build

Needs the EVerest workspace (framework crates, interface/type YAML). From this directory:

```sh
EVEREST_CORE_ROOT=<path to everest-core repo root> cargo build
```

`moddef-core` is pulled as a git dependency, so the build needs network access. The Bazel target needs
`moddef-core`/`pollster` added to the EVerest crate index first (see `BUILD.bazel`).

## Design notes

everestrs command handlers are synchronous; `moddef-core`'s `Device` is async and `&mut`. The module
drives it with `pollster::block_on` on the metering thread and the handler threads, serialising all
bus access behind a `Mutex<Device>`. The `Device`'s `Transport` is a thin bridge (`HubBridge`) that
forwards `read_holding` / `read_input` / `read_coils` / `write_holding` / `write_coil` to the
`serial_communication_hub` commands.
