# EVerest Configuration Service — implementation notes

**The explanation of what the Configuration Service *does* lives in the EVerest manual**, under
*Explanation -> Configuration Service*
([`docs/source/explanation/configuration-service.rst`](../../../../docs/source/explanation/configuration-service.rst)):
terminology, configuration slots, boot sources and the `--config` / `--db` options, the startup
sequence, slot switching and module restart, deployment, and the read/write semantics including
every per-parameter outcome. The six sequence diagrams are in
[`docs/source/explanation/images/`](../../../../docs/source/explanation/images/) and are rendered
on that page.

This document deliberately does **not** repeat any of that. It covers only what the manual leaves
out on purpose: where the code lives, the threading model, the exact C++ types behind the
documented behavior, the wire-level message names, and the invariants that are easy to break when
changing this code.

## Where the code lives

All paths relative to `lib/everest/framework/`.

| Concern | Files |
| --- | --- |
| Service interface (the public in-process contract) | `include/utils/config_service_interface.hpp` |
| `ConfigServiceCore` | `include/utils/config/config_service_core.hpp`, `lib/config/config_service_core.cpp` |
| `MqttConfigServiceHandler`, `ConfigServiceClient` (EVerest Internal API) | `include/utils/mqtt_config_service.hpp`, `lib/mqtt_config_service.cpp` |
| Config value types, `Mutability`, `Access`, `validate_config_value()` | `include/utils/config/types.hpp` |
| Boot source and database bootstrap | `include/utils/config/settings.hpp`, `lib/runtime.cpp` |
| User-config YAML persistence mirror (`UserConfigStorage`) | `include/utils/config.hpp`, `include/utils/config/storage_userconfig.hpp` |
| Manager wiring | `src/manager.cpp` |
| `everest-config-tool` (note: source file is `everest-config.cpp`) | `src/everest-config.cpp` |
| Tests | `tests/test_config_service_core.cpp`, `tests/test_config_userconfig.cpp`, `tests/test_config.cpp` |

Wire-level MQTT topics and payloads of the Internal API are documented separately in
[MQTT Communication](MQTTCommunication.md). Startup distribution is described in
[MQTT Config distribution](MQTTConfigDistribution.md).

## Threading model

`ConfigServiceCore` is a single-writer actor. `m_worker_thread` drains `m_command_queue`
(a `thread_safe_queue<std::function<void()>>`) one task at a time; every mutating operation is
posted through `post_to_actor()`. Readers do not touch that thread: they take an immutable
snapshot via `get_active_module_configurations()`, which returns a
`shared_ptr<const ModuleConfigurations>`.

`post_to_actor()` detects re-entrancy by comparing `std::this_thread::get_id()` against
`m_worker_thread.get_id()` and executes inline in that case. Consequences for callers, spelled out
at `config_service_core.hpp:74-83`:

- Update handlers run **on the actor thread** as part of the mutating operation and block all
  further config-service processing while they run. Keep them short.
- Calling back into public `ConfigServiceCore` methods from inside a handler is safe (it executes
  inline).
- Never block a handler on anything that itself waits for the config service.

## C++ types behind the documented behavior

| Manual concept | Type / values |
| --- | --- |
| Per-parameter write outcome | `SetConfigParameterResultEnum` — `Applied`, `WillApplyOnRestart`, `DoesNotExist`, `RetryLater`, `AccessDenied`, `Rejected` |
| Request-level write status | `SetConfigParameterStatus` — `Ok`, `Error`, `ModulesInTransientState`. `Error` is the default-initialized value |
| Module's verdict on a runtime change (Internal API) | `SetResponseStatus` — `Accepted`, `Rejected`, `RebootRequired` (`mqtt_config_service.hpp:75`) |
| Result of forwarding a runtime change | `SetParameterResponse` — `SetCallFailed`, `ModuleReplied_Applied`, `ModuleReplied_RequiresRestart`, `ModuleReplied_Rejected` |
| Slot / module status | `ActiveSlotStatus` — `Running`, `Stopped`, `Starting`, `Stopping`, `FailedToStart`, `RestartTriggered`, with the `modules_are_down()` and `modules_in_transient_state()` predicates |
| Runtime mutability | `Mutability` — `ReadOnly`, `ReadWrite`, `WriteOnly` |
| Access control | `Access` -> `ConfigAccess` -> `ModuleConfigAccess`, including `allow_set_read_only`; enforced by `access_allowed()`, applied by `update_mutability()` |
| Notifications | `ActiveSlotUpdate`, `ConfigurationUpdate`, `ConfigParameterUpdateNotice`, `Origin` |
| Boot source | `BootMode` — `YamlWithInMemoryDb`, `DatabaseOnly`, `DatabaseWithYamlSeed`; resolved by `resolve_boot_source()`, database prepared by `init_database_bootstrap()` returning `DatabaseBootstrap` |

Watch out for two near-duplicate enums with the same value names but different types:
`SetResponseStatus` (`mqtt_config_service.hpp`, used by the Internal API `SetResponse`) and
`everest::config::SetConfigStatus` (`config/types.hpp:278`, used by `SetConfigResult`). They are
not interchangeable.

`resolve_boot_source()` and `init_database_bootstrap()` are **free functions** in
`settings.hpp` / `runtime.cpp`, not members of `ConfigServiceCore`. `init_database_bootstrap()`
runs before the core is constructed. `resolve_boot_source()` is unit-tested in
`tests/test_config.cpp` (`SCENARIO("Check resolve_boot_source")`).

## `ConfigServiceCore` entry points

| Member | Purpose |
| --- | --- |
| `get_active_module_configurations()` | immutable snapshot for readers; does not touch the actor thread |
| `set_config_parameters(slot_id, updates, origin)` | the whole write path |
| `mark_active_slot(slot_id)` | records the next boot slot only; does not stop modules |
| `reinitialize_from_db(force_reload)` | reloads the marked slot; **not** part of `ConfigServiceInterface` |
| `set_modules_at_rest()` | settles a transitional status while deliberately preserving a recorded `FailedToStart` |
| `register_set_runtime_parameter_handler(cb)` | installs the runtime-parameter forwarder |
| `register_active_slot_update_handler(cb)`, `register_config_update_handler(cb)` | push-event subscriptions; see the threading rules above |

Manager wiring, in `src/manager.cpp`: the `UserConfigStorage` persistence mirror is created for
`BootMode::YamlWithInMemoryDb` (~L827), then `ConfigServiceCore`, then
`MqttConfigServiceHandler`, then `register_set_runtime_parameter_handler()` (~L859) mapping
`SetResponseStatus::Accepted` to `ModuleReplied_Applied`, `RebootRequired` to
`ModuleReplied_RequiresRestart`, anything else to `ModuleReplied_Rejected`, and `SetCallFailed`
when the round trip returns nothing. Lifecycle status is pushed in via
`register_state_transition_handler()` (~L888). The forwarder is deregistered with
`register_set_runtime_parameter_handler(nullptr)` (~L882).

## Internal API message names

`GetRequest` / `GetResponse` and `SetRequest` / `SetResponse` on the shared request topic
`{everest_prefix}config/request`; the manager forwards a runtime change to the target module on
`{everest_prefix}modules/{module_id}/config/set_request` and reads the verdict from
`{everest_prefix}modules/{module_id}/config/set_response`. All at QOS2. Payload structures are in
[MQTT Communication](MQTTCommunication.md).

Modules reach this through `ConfigServiceClient`; read requests are answered by
`MqttConfigServiceHandler` from the `ConfigServiceCore` snapshot, writes are forwarded to
`set_config_parameters()` and always target the active slot.

## Invariants to preserve

These orderings are load-bearing and cheap to break; each has test coverage.

1. **Validate before persisting.** `validate_config_value()` runs first, so a value that would
   fail to parse on the next boot never reaches the database
   (`config_service_core.cpp:512-519`, `:618-625`). Datatype only — no min/max range check at
   this layer.
2. **Mirror before database.** Without `--db` the user-config YAML mirror is written *before* the
   in-memory database, and a failed mirror write rejects the update, because the mirror is the
   only persistence that survives a restart (`config_service_core.cpp:522-539`).
3. **Persist before consulting the module.** A successful database write sets
   `WillApplyOnRestart`; only then is the runtime path entered, and only when mutability is
   `ReadWrite` and the modules are running (`:541-552`). With no forwarder registered the value is
   already persisted and `status_info` says so (`:554-560`).
4. **Transient state fails the whole request.** `ModulesInTransientState` fills *every*
   per-parameter result with `RetryLater` and persists nothing (`:467-471`).
5. **Notify only when something was written** — guarded by `if (not event.updates.empty())`
   (`:478`).
6. **Reload only at rest.** `reinitialize_from_db()` is skipped while modules are running or
   mid-transition, so running processes cannot desynchronize from the in-memory configuration.
