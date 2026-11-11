.. _howto-ocpp-device-model:

####################################################################
Add Components to the OCPP Device Model from an External Application
####################################################################

.. warning::

   The :ref:`ocpp_device_model_API <everest_modules_handwritten_ocpp_device_model_API>` is currently
   **experimental**: its topics and types may change without further notice.

   No module requires the ``ocpp_device_model`` interface that the API module provides yet. Support in the
   :ref:`OCPPmulti <everest_modules_OCPPmulti>` module, including its configuration, follows separately. This guide
   describes what an application implements to be ready for it.

This guide shows how an application outside EVerest adds its own components and variables to the OCPP 2.x device
model at runtime, so that the CSMS can read, monitor and configure them: a cooling unit, an auxiliary meter or the
devices behind a gateway, for example.

The application talks MQTT to the :ref:`ocpp_device_model_API <everest_modules_handwritten_ocpp_device_model_API>`
module. The topics and payloads are specified in ``docs/source/reference/EVerest_API/ocpp_device_model_API.yaml``.
The module passes the messages on to the OCPP module through the internal
:ref:`ocpp_device_model <everest_interfaces_ocpp_device_model>` interface, whose
:ref:`types <everest_types_ocpp_device_model>` the payloads follow.

Without the API, a variable has to be defined in the
:ref:`component config <handwritten_ocppmulti_device-model-component-configs>` of libocpp, and its value comes from
the OCPP service. With the API, the application that knows the value defines the variable and keeps it current, and
the CSMS's writes to it are passed on to that application.

The rules in this guide come from libocpp's runtime registry. The details are in
``lib/everest/ocpp/doc/v2/ocpp_201_device_model_initialization.md``.

Owners
======

Every variable of the device model has exactly one definition: either the component config or one **owner**. An owner
is a name the application chooses, one per device for example. Ownership is per variable, so the component config and
an owner can contribute different variables to the same component.

For each of its owners, the application:

- registers the variables with their characteristics, attributes, initial values and monitors,
- reports every value that changes,
- applies the writes of the CSMS, and
- keeps the values of the persistent attributes across restarts.

A registration is held in memory only and is repeated at every start of the OCPP service. The monitors of a variable,
the custom monitors of the CSMS included, are stored by component and variable name, and are applied again when the
variable is registered again. Its values are not stored by the OCPP service: the application keeps them.

.. mermaid::

   sequenceDiagram
       participant App as Application
       participant API as ocpp_device_model_API
       participant OCPP as OCPP module
       participant CSMS
       OCPP->>API: get_registrations
       API-)App: e2m/get_registrations
       App-)API: m2e/registration
       API-->>OCPP: registrations of all owners
       OCPP->>API: set_registration_result
       API-)App: e2m/set_registration_result
       Note over OCPP: the OCPP service starts
       CSMS->>OCPP: SetVariables.req
       OCPP->>API: set_value
       API->>App: e2m/set_value
       App-->>API: Accepted, on the replyTo topic
       API-->>OCPP: Accepted
       OCPP-->>CSMS: SetVariables.conf
       Note over App: the device changes a value
       App-)API: m2e/value_changed
       API-)OCPP: value_changed

Configure the API module
========================

Add the module to the EVerest configuration and list the owners the application registers:

.. code-block:: yaml

   device_model_api:
     module: ocpp_device_model_API
     config_module:
       cfg_expected_owners: "cooling_unit"
       cfg_expected_owners_timeout_s: 60

The topics of this module instance are below ``everest_api/1/ocpp_device_model/device_model_api/``, named by its
module id. The topics in this guide are relative to that prefix.

The application may start before or after EVerest, so the module collects the registrations for the OCPP module.
With ``cfg_expected_owners``, it hands them on as soon as every listed owner registered, at the latest after
``cfg_expected_owners_timeout_s``. It then logs the owners still missing. Without ``cfg_expected_owners``, the
module collects registrations for ``cfg_registration_timeout_s``, 10 s by default.

The OCPP service starts only after the module handed the registrations on, so these timeouts delay the connection to
the CSMS. Each of them is at most 240 s, below the 300 s after which EVerest aborts a command.

The first start of the OCPP service removes the monitors of the variables that nobody registered. An owner that
registers later is still accepted, but the monitors of its variables from earlier boots are gone by then. List
**every** owner in ``cfg_expected_owners``, also those whose devices are not always available.

The module also supervises the communication with its clients, see the
:ref:`module documentation <everest_modules_handwritten_ocpp_device_model_API>`.

Register components
===================

Publish one registration per owner on ``m2e/registration``:

- when the application starts, and
- each time the module publishes ``e2m/get_registrations``, which it does when the OCPP module asks for the
  registrations. Publish the registration of every owner again, also the ones published before.

A registration names the components of an owner and the variables of each component. This registration of a cooling
unit at EVSE 1 adds three variables: a temperature reading, a writable fan speed limit and a mode.

.. code-block:: json

   {
     "owner": "cooling_unit",
     "components": [
       {
         "component": { "name": "CoolingUnit", "instance": "1", "evse": { "id": 1 } },
         "variables": [
           {
             "variable": { "name": "Temperature" },
             "characteristics": { "data_type": "decimal", "unit": "Celsius", "supports_monitoring": true },
             "attributes": [{ "type": "Actual", "value": "21.5", "mutability": "ReadOnly" }],
             "monitors": [
               { "type": "UpperThreshold", "value": 70, "severity": 4, "monitor_type": "PreconfiguredMonitor" }
             ]
           },
           {
             "variable": { "name": "FanSpeedLimit" },
             "characteristics": {
               "data_type": "integer",
               "unit": "Percent",
               "min_limit": 0,
               "max_limit": 100,
               "supports_monitoring": false
             },
             "attributes": [{ "value": "80", "mutability": "ReadWrite", "persistent": true }]
           },
           {
             "variable": { "name": "Mode" },
             "characteristics": {
               "data_type": "OptionList",
               "values_list": "Eco,Normal,Boost",
               "supports_monitoring": false
             },
             "attributes": [{ "value": "Normal", "mutability": "ReadWrite" }]
           }
         ]
       }
     ]
   }

A registration without ``owner`` belongs to the owner named by the module id, ``device_model_api`` here. The
``FanSpeedLimit`` attribute has no ``type``, so it defaults to the Actual attribute. Because it is persistent, the
application stores the value of every write and registers the stored value at the next start, here ``80``.

The ``Temperature`` monitor raises an event when the reported temperature rises above 70 °C, and a second one with
``cleared`` set when it falls back. The OCPP service sends both to the CSMS in a NotifyEvent.req, filtered by the
monitoring level and base the CSMS set, and queued while offline up to ``OfflineQueuingSeverity``. The application
is not told about the events.

The CSMS cannot clear a monitor of either kind. It may change a ``PreconfiguredMonitor`` with a
SetVariableMonitoring.req that names its id. The monitor keeps its id and stays preconfigured. A registration
matches stored monitors by type and severity, so a threshold the CSMS changed to 75 is kept when the application
registers the variable again, even though the registration still lists 70. A changed severity is not kept: the
registration then sets up the monitor with severity 4 again.
A ``HardWiredMonitor`` cannot be changed: a SetVariableMonitoring.req with its id is rejected. It also stays active
when the CSMS sets the monitoring base to ``HardWiredOnly``, which deactivates preconfigured monitors.

Keep these rules in mind when choosing names:

- An ``instance`` is either absent or not empty. A component or variable with an empty instance is rejected with
  ``InvalidNameKey``, because monitors and stored values key an absent instance as an empty one.
- An attribute without ``mutability`` cannot be written.
- The variables that the OCPP service writes from its own state are rejected with ``ReservedVariable``, for example
  ``AuthCacheCtrlr.Storage``, the ``AvailabilityState`` of an EVSE and every variable of a ``NetworkConfiguration``
  slot.

Check the result
================

The OCPP module answers each registration on ``e2m/set_registration_result``. In the usual case nothing is rejected:

.. code-block:: json

   { "owner": "cooling_unit", "status": "Accepted", "rejected": [] }

When the component config defines ``CoolingUnit.Mode`` already, only that variable is rejected, and ``defined_by``
names the owner of the definition:

.. code-block:: json

   {
     "owner": "cooling_unit",
     "status": "Accepted",
     "rejected": [
       {
         "component": { "name": "CoolingUnit", "instance": "1", "evse": { "id": 1 } },
         "variable": { "name": "Mode" },
         "reason": "AlreadyDefined",
         "defined_by": "COMPONENT_CONFIG"
       }
     ]
   }

The application receives no writes to a rejected variable and does not report its values.

The module ignores and logs a registration that does not fit the types, a ``unit`` of more than 16 characters or a
component name of more than 50 for example. Such a registration gets no result.

Register several owners
=======================

An application that fronts several devices, a Modbus gateway for two coolant pumps for example, names one owner per
device and publishes one registration for each. Each device then has its own registration, which the application can
replace or remove on its own:

.. code-block:: json

   {
     "owner": "pump-1",
     "components": [
       {
         "component": { "name": "CoolantPump", "instance": "1" },
         "variables": [
           {
             "variable": { "name": "Speed" },
             "characteristics": { "data_type": "integer", "unit": "RPM", "supports_monitoring": true },
             "attributes": [{ "value": "1450", "mutability": "ReadOnly" }]
           }
         ]
       }
     ]
   }

.. code-block:: json

   {
     "owner": "pump-2",
     "components": [
       {
         "component": { "name": "CoolantPump", "instance": "2" },
         "variables": [
           {
             "variable": { "name": "Speed" },
             "characteristics": { "data_type": "integer", "unit": "RPM", "supports_monitoring": true },
             "attributes": [{ "value": "0", "mutability": "ReadOnly" }]
           }
         ]
       }
     ]
   }

Both owners belong in ``cfg_expected_owners``, here ``"pump-1, pump-2"``.

An owner name belongs to the module instance that registered it first, until it is released. When a second instance
registers ``pump-1``, nothing of that registration reaches the device model:

.. code-block:: json

   { "owner": "pump-1", "status": "OwnerInUse", "rejected": [] }

Apply writes
============

The OCPP module checks a write against the characteristics and the mutability of the variable, then passes it on as
a request on ``e2m/set_value``. The request names its owner, so a gateway knows which device to pass it on to:

.. code-block:: json

   {
     "headers": {
       "replyTo": "everest_api/1/ocpp_device_model/device_model_api/m2e/reply/2b0e6a41-8c1f-4f57-9d0e-5f3c1a7b9e10"
     },
     "payload": {
       "owner": "cooling_unit",
       "component": { "name": "CoolingUnit", "instance": "1", "evse": { "id": 1 } },
       "variable": { "name": "FanSpeedLimit" },
       "attribute_type": "Actual",
       "value": "60",
       "source": "csms"
     }
   }

Publish the status on the ``replyTo`` topic, ``"Accepted"`` for example. It goes to the CSMS as it is:

- ``Accepted``: the owner applied exactly ``60``. The OCPP service stores the value in memory, so the application
  does **not** report it again on ``m2e/value_changed``.
- ``RebootRequired``: the owner kept exactly ``60``, but the value takes effect only at the next start. The OCPP
  service does not store it: until then, reads return the current value, ``80`` in the example. The application
  keeps the new value and registers it at the next start, and does not report it on ``m2e/value_changed`` before it
  takes effect.
- ``Rejected``: the owner did not apply the value. This includes a value it would only apply in a changed form. A fan
  that runs at most at 55 % rejects the write rather than accepting it and clamping it.

Without a reply within ``cfg_request_reply_to_s``, 10 s by default, the write is ``Rejected``. Keep that timeout
below the message timeout of the CSMS, which waits for its SetVariables.conf meanwhile, and reject a write that the
device does not confirm in time.

Requests may overlap, also for one variable, and the API defines no order between them.

Report changes
==============

Publish each value the device changes itself on ``m2e/value_changed``. The OCPP service answers reads from the
values it holds, so a change that is not reported does not reach the CSMS, unless a monitor is registered for it.

.. code-block:: json

   {
     "owner": "cooling_unit",
     "component": { "name": "CoolingUnit", "instance": "1", "evse": { "id": 1 } },
     "variable": { "name": "Temperature" },
     "value": "48.25"
   }

Without ``owner``, a report belongs to the owner named by the module id.

A report is applied as it arrives, so a write that is in progress when the device changes the value may overwrite
it.

A report of a value that does not fit the data type or the ``values_list`` of the variable is ignored, and so is a
report for an owner the module instance did not register. A report has no reply, so the rejection is only logged.
``min_limit`` and ``max_limit`` bound the writes of the CSMS, not the reports of the owner.

Replace or release an owner
===========================

A registration replaces everything its owner registered before. The registrations of the other owners stay. A
registration with an empty ``components`` list removes the variables of the owner and releases the owner name:

.. code-block:: json

   { "owner": "pump-2", "components": [] }

Once the module handed the collected registrations on, it passes each further registration on at once. That
registration is not ordered against the collected ones and may reach the OCPP module first: a release would then be
undone by the collected registration that still holds the owner. To replace or release an owner registered at
startup, wait for its ``e2m/set_registration_result`` first.
