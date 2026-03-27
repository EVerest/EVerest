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
  "name": "<cmd_name>",
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
  "name": "<cmd_name>",
  "type": "result",
  "data": {
    "id": "<matching_call_id>",
    "error": {
      "event": "<error_type>",
      "msg": "<error_message>"
    },
    "origin": "<responding_module_id>"
  }
}
```

### Error Types

Command responses can contain the following error events:

- `MessageParsingFailed`: JSON parsing error
- `SchemaValidationFailed`: Schema validation error  
- `HandlerException`: Exception in command handler
- `Timeout`: Command execution timeout
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
  "message": "<error_description>",
  "severity": "<error_severity>",
  "origin": {
    "module_id": "<originating_module>",
    "implementation_id": "<originating_impl>",
    "evse": <evse_number>,
    "connector": <connector_number>
  },
  "state": "<error_state>",
  "timestamp": "<iso_timestamp>",
  "uuid": "<unique_error_id>"
}
```

## 5. Configuration

The following structure applies for configuration messages. Modules publish requests to a shared request topic.
The manager handles them and routes responses back to the requesting module's private response topic.

### Topic Structure

```bash
# Request (published by module, subscribed by manager)
{everest_prefix}config/request

# Response (published by manager, subscribed by requesting module)
{everest_prefix}modules/{module_id}/response
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
independent of the underlying data type.

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

#### Response

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
    "response": { "status": "Accepted" }
  }
}
```

### Response Status Values

Top-level `status` values:

- `Ok`: Request handled successfully
- `Error`: An error occurred; details in `status_info`
- `AccessDenied`: The requesting module lacks permission to access the target configuration

Set-specific `response.status` values:

- `Accepted`: Value was set successfully
- `Rejected`: Value could not be set
- `RebootRequired`: Value was set but a reboot is required for it to take effect
