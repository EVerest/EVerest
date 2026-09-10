# EVerest Framework MQTT Topic Structure

This document describes the MQTT topic structure used by the EVerest framework for communication between modules.

## Topic Prefix Structure

The EVerest framework uses a configurable MQTT prefixes for topics. This allows multiple instances of EVerest
to run at the same time using the same broker. The default prefix is `everest`.

## 1. Variables (Vars)

The following structure applies for variable topics:

### Topic Structure

```bash
{everest_prefix}modules/{module_id}/impl/{impl_id}/var/{var_name}
```

### Message Payload Structure

The payload contains the actual variable data in the `data` field.

```json
{
  "data": <variable_value>
}
```

## 2. Commands (Cmds)

The following structure applies for command topics. Modules that provide (implement) the command subscribe to the command topic, while modules that call the command publish to it. Each command call generates a unique UUID as the call ID. The origin field identifies the calling module. Command handlers process the request and publish responses to a separate response topic.

### Topic Structure

```bash
{everest_prefix}modules/{module_id}/impl/{impl_id}/cmd/{cmd_name}
```

### Message Payload Structure

```json
{
  "id": "<unique_call_id>",
  "args": {
    "arg1": "value1",
    "arg2": "value2"
  },
  "origin": "<calling_module_id>"
}
```

## 3. Command Responses

The following structure applies for command response topics. These are used to send back results or errors from command handlers to the calling module. Response topics include the calling module ID for proper routing. The call ID matches the original command request.

### Topic Structure

```bash
{everest_prefix}modules/{module_id}/impl/{impl_id}/cmd/{cmd_name}/response/{calling_module_id}
```

### Message Payload Structure

#### Successful Response

```json
{
  "type": "result",
  "data": {
    "id": "<matching_call_id>",
    "retval": <return_value>,
    "origin": "<responding_module_id>"
  }
}
```

#### Error Response

```json
{
  "type": "result",
  "data": {
    "id": "<matching_call_id>",
    "error": {
      "__everest__error_type": "<error_type>",
      "__everest__error_msg": "<error_message>"
    },
    "origin": "<responding_module_id>"
  }
}
```

### Error Types

Command responses can contain the following error events:

- `MessageParsingError`: JSON parsing error
- `SchemaValidationError`: Schema validation error
- `HandlerException`: Exception in command handler
- `CmdTimeout`: Command execution timeout
- `Shutdown`: System shutdown
- `NotReady`: Module not ready

## 4. Errors

The following structure applies for error topics. Errors are raised by modules and can be subscribed to by other modules. Each error has a unique UUID and includes information about the originating module, implementation, EVSE, and connector if applicable.

### Topic Structure

```bash
{everest_prefix}modules/{module_id}/impl/{impl_id}/error/{error_type}
```

### Message Payload Structure

```json
{
  "type": "<error_namespace>/<error_name>",
  "sub_type": "<error_sub_type>",
  "message": "<error_message>",
  "description": "<error_description>",
  "severity": "<error_severity>",
  "origin": {
    "module_id": "<originating_module>",
    "implementation_id": "<originating_impl>",
    "mapping": {
      "evse": <evse_number>,
      "connector": <connector_number>
    }
  },
  "state": "<error_state>",
  "timestamp": "<iso_timestamp>",
  "uuid": "<unique_error_id>",
  "vendor_id": "<vendor_id>"
}
```

`origin.mapping` is only present when the originating module/implementation has a Mapping configured; within
`mapping`, `connector` is itself optional and only present when a connector-level mapping was configured.

## 5. Configuration

The following structure applies for configuration messages. Modules publish requests to a shared request topic.
The manager handles them and routes responses back to the requesting module's private response topic.

### Topic Structure

```bash
# Request (published by module, subscribed by manager)
{everest_prefix}config/request

# Response (published by manager, subscribed by requesting module)
{everest_prefix}modules/{module_id}/response

# Runtime set: manager forwards SetRequest to target module
{everest_prefix}modules/{module_id}/config/set_request

# Runtime set: target module replies to manager
{everest_prefix}modules/{module_id}/config/set_response
```

### Message Payload Structure

All configuration messages use `ConfigurationRequest` / `ConfigurationResponse` as the `msg_type`. The `data`
field holds the request or response object.

#### Get Own Module Configuration

Used during module startup to retrieve the module's own configuration.

```json
{
  "msg_type": "ConfigurationRequest",
  "data": {
    "type": "Get",
    "origin": "<module_id>",
    "request": { "type": "Module" }
  }
}
```

#### Get All Accessible Module Configurations

Retrieves configurations of all modules the requesting module has read access to.

```json
{
  "msg_type": "ConfigurationRequest",
  "data": {
    "type": "Get",
    "origin": "<module_id>",
    "request": { "type": "All" }
  }
}
```

#### Get a Specific Configuration Value

Retrieves a single parameter identified by module, parameter name, and optional implementation ID.

```json
{
  "msg_type": "ConfigurationRequest",
  "data": {
    "type": "Get",
    "origin": "<module_id>",
    "request": {
      "type": "Value",
      "identifier": {
        "module_id": "<target_module_id>",
        "configuration_parameter_name": "<param_name>",
        "module_implementation_id": "<impl_id>"
      }
    }
  }
}
```

`module_implementation_id` is optional and defaults to the module-level scope (`"!module"`) when omitted.

#### Get All Accessible Module Mappings

Retrieves requirement mappings of all modules the requesting module has read access to.

```json
{
  "msg_type": "ConfigurationRequest",
  "data": {
    "type": "Get",
    "origin": "<module_id>",
    "request": { "type": "AllMappings" }
  }
}
```

#### Set a Configuration Value

Updates a configuration parameter at runtime. `value` is always the string representation of the new value,
independent of the underlying data type. The manager persists the change first; for ReadWrite parameters it
additionally forwards the request to the target module afterwards so the module can apply it at runtime; see below.

```json
{
  "msg_type": "ConfigurationRequest",
  "data": {
    "type": "Set",
    "origin": "<module_id>",
    "request": {
      "identifier": {
        "module_id": "<target_module_id>",
        "configuration_parameter_name": "<param_name>",
        "module_implementation_id": "<impl_id>"
      },
      "value": "<string_value>"
    }
  }
}
```

#### Forwarded Set Request (Manager → Target Module)

For ReadWrite parameters the manager forwards the validated SetRequest to the target module on a dedicated topic.
Unlike the original client request, this payload carries the bare `identifier`/`value` pair — without the
outer `type`/`origin` wrapper used for the module-to-manager request.

Topic: `{everest_prefix}modules/{target_module_id}/config/set_request`

```json
{
  "msg_type": "ConfigurationRequest",
  "data": {
    "identifier": {
      "module_id": "<target_module_id>",
      "configuration_parameter_name": "<param_name>",
      "module_implementation_id": "<impl_id>"
    },
    "value": "<string_value>"
  }
}
```

#### Module Set Response (Target Module → Manager)

The target module replies with a standard set response.

Topic: `{everest_prefix}modules/{target_module_id}/config/set_response`

```json
{
  "msg_type": "ConfigurationResponse",
  "data": {
    "status": "Ok",
    "status_info": "",
    "type": "Set",
    "response": { "status": "Accepted", "status_info": "" }
  }
}
```

#### Response (Manager → Requesting Module)

The manager always responds on `{everest_prefix}modules/{origin}/response`. The `type` and `response` fields
mirror the request type. `type` is omitted when `status` is not `Ok`.

```json
{
  "msg_type": "ConfigurationResponse",
  "data": {
    "status": "Ok",
    "status_info": "",
    "type": "Get",
    "response": {
      "type": "Module",
      "data": { }
    }
  }
}
```

For set responses the inner `response` object contains a `status` field instead of `data`:

```json
{
  "msg_type": "ConfigurationResponse",
  "data": {
    "status": "Ok",
    "status_info": "",
    "type": "Set",
    "response": { "status": "Accepted", "status_info": "" }
  }
}
```

### Response Status Values

Top-level `status` values:

- `Ok`: Request handled successfully
- `Error`: An error occurred; details in `status_info`. Error responses carry no `type` field, and
  their `response` field is `null`. This includes requests from an unknown origin module and
  unexpected errors during request handling — the manager always replies once a request could be
  parsed.
- `AccessDenied`: The requesting module lacks permission to access the target configuration

Set-specific `response.status` values:

- `Accepted`: Value was persisted and the running target module applied it at runtime immediately
- `Rejected`: Value was NOT persisted (e.g. it failed datatype validation or writing to storage failed);
  details in `status_info`
- `RebootRequired`: Value was persisted and takes effect after the next restart. Returned whenever the
  change could not be applied at runtime: the parameter is not runtime-changeable (e.g. ReadOnly), the
  modules are not running, no runtime forwarding to the target module is available, or the running
  module did not apply the change at runtime (it requires a restart, or it rejected the runtime change —
  the persisted value still applies on the next boot)
