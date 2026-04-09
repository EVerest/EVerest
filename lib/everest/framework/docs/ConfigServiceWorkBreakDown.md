# EVerest ConfigService — Implementation Work Items

This document breaks down the work required to implement the Approach C architecture (Integrated Manager with ConfigServiceAPI). Work items are grouped by area and roughly ordered by dependency — earlier items are prerequisites for later ones.

## 1. Decouple module lifetime from manager lifetime (Christoph / James)

This is the foundational change. The manager must survive module crashes and be able to start/stop modules independently. Everything else builds on this.

### 1.1 Manager survives module crashes
Currently, when a module crashes the manager kills all other modules and terminates itself. Change this so the manager detects a module crash, cleans up the crashed module's resources and remains running with the ConfigServiceAPI available.

### 1.2 Manager can stop all modules gracefully
Implement a controlled shutdown sequence where the manager signals all modules to shut down, waits for acknowledgment (with timeout), and forcefully terminates any modules that don't respond. After shutdown, the manager remains running.

### 1.3 Manager can start modules from stopped state
After all modules have been stopped (or after initial manager startup before any modules are spawned), the manager must be able to load a configuration slot, spawn modules, and run through the startup sequence (config distribution, init, ready signaling). This is the inverse of 1.2.

### 1.4 Manager can restart all modules
Combine 1.2 and 1.3 into a restart operation: graceful shutdown of all modules, load (potentially new) configuration, start modules. This is needed for slot switching and `RebootRequired` config changes.

### 1.5 Manager startup has two phases
Separate manager startup into: (a) initialize ConfigService, connect to MQTT, expose ConfigServiceAPI, and (b) load boot_slot and start modules. Today these happen as one sequence. The split ensures the ConfigServiceAPI is available to external clients before modules start, and that the ConfigService is initialized before modules request their configuration via the Internal API.

## 2. Expose ConfigServiceAPI from within manager (Florin)

### 2.1 Define ConfigServiceAPI AsyncAPI specification (Florin)

Write the formal AsyncAPI spec for the ConfigServiceAPI, covering all operations: GetConfiguration, WriteConfiguration, GetAllConfigurations, SetActiveConfiguration, DeleteConfiguration, CopyConfiguration, config_changed notifications. Define MQTT topic structure, message schemas, and error responses.

### 2.2 Implement ConfigService with SQLite backend (Piet)

Implement the ConfigService component within the manager that manages configuration slots in SQLite. This includes slot CRUD operations (create, read, update, delete), slot state management (Active, Clean, Dirty, Invalidated), and boot_slot / active_slot / last_known_good_config tracking.

This is partly implemented already.

### 2.3 Expose ConfigServiceAPI over MQTT (Florin)
Wire the ConfigServiceAPI so it listens on the defined topics, handles incoming requests, and publishes responses and notifications. This makes the ConfigServiceAPI available to external clients (Cloud, UI, Backend).

### 2.4 Implement validation and access control (EXTRA)
Build the validation and access control layer in the ConfigService. This includes: validating config values against the module manifest schema (type, min, max, allowed values), verifying that the requesting client is authorized to read/write the targeted parameters, and rejecting invalid or unauthorized requests before they reach any module.

### 2.5 Implement config_changed notification (Piet)
When configuration is persisted (after a successful write or slot switch), publish a `config_changed` event on the ConfigService with the relevant slot_id, changed parameters, and timestamp. ConfigServiceAPI Clients and EVerest modules can subscribe to this for reactive updates. This operations therefore needs to be integrated within the EVerest internal API
as well as in the ConfigServiceAPI.

## 3. Module config reads and writes via Internal API (James / Piet)

Modules use the EVerest Internal API for all configuration operations. This keeps modules decoupled from the ConfigServiceAPI. The manager's ConfigService handles these Internal API requests internally and serves them from the SQLite backend.

### 3.1 Define SetConfigParameter / SetConfigParameterResult on Internal API
Extend the EVerest Internal API with the commands for delivering config changes to target modules: `SetConfigParameter(key, value)` sent from manager to module, and `SetConfigParameterResult(Immediate | RebootRequired | Rejected)` sent from module back to manager. Define the MQTT topics, message format, and timeout behavior.

--> API is defined, currently "Read" is already implemented. "Write" needs integration with target modules.

### 3.2 Modules write config via Internal API
Enable modules (e.g. OCPP) to send write requests via the Internal API. The manager receives these, routes them through the ConfigService for validation, delivers changes to target modules, and persists based on the verdict. The flow is the same as for ConfigServiceAPI writes, but the requesting module uses the Internal API.

### 3.3 Implement config change delivery in manager
When the ConfigService receives a WriteConfiguration for the active slot, route the change to the target module via the Internal API, wait for the verdict, and then persist (or not) based on the response. Handle timeouts (module not responding) as a rejection.

### 3.4 Implement config change handling in module framework
Provide a framework-level handler that modules can use to receive `SetConfigParameter` calls. The framework should deserialize the request, call a module-provided callback, and return the result. Module authors implement the callback; the framework handles the protocol. This is a candidate for code generation from the module manifest.

## 4. Migration and integration

### 4.1 Import existing YAML configs into SQLite
Provide tooling or a migration path to import existing YAML configuration files into the SQLite-based slot system. This is needed for both the development mode (`./manager --config my_config.yaml` importing as active slot) and for migrating existing deployments.

### 4.2 Update documentation
Update EVerest developer documentation to reflect the new ConfigServiceAPI, the module startup sequence changes, and the runtime config change handling. Include migration guide for existing module authors.

### 4.3 Update CI/CD and testing
Adapt integration tests and SIL tests to the new architecture. Tests that rely on the old config distribution mechanism need to be updated. Add tests for the ConfigServiceAPI, slot management, and runtime config change flows.
