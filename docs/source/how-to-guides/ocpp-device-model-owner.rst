.. _howto-ocpp-device-model-owner:

#######################################
Add Components to the OCPP Device Model
#######################################

.. warning::

   The ``ocpp_device_model`` interface is currently **experimental**: its commands, variables and types may change
   without further notice. It is exempt from the stability guarantees and the deprecation period of the EVerest public
   API until promoted to stable (see :ref:`project-experimental-components`).

   No module requires the interface yet. Support in the :ref:`OCPPmulti <everest_modules_OCPPmulti>` module, including
   its configuration, follows separately. This guide describes the contract that a module implements to be ready for
   it.

This guide shows how a module adds its own components and variables to the OCPP 2.x device model at runtime, through
the :ref:`ocpp_device_model <everest_interfaces_ocpp_device_model>` interface. The types it uses are described in
:ref:`ocpp_device_model types <everest_types_ocpp_device_model>`. Use it when a module knows about hardware or state
that the CSMS should be able to read, monitor and configure: a cooling unit, an auxiliary meter or the devices behind
a gateway, for example.

Without the interface, a variable has to be defined in the component config of libocpp, and its value comes from the
OCPP service. With the interface, the module that knows the value defines the variable and keeps it current, and the
CSMS's writes to it are passed on to that module.

The rules in this guide come from libocpp's runtime registry. The details are in
``lib/everest/ocpp/doc/v2/ocpp_201_device_model_initialization.md``.

Owners and registrations
========================

Every variable of the device model has exactly one definition: either the component config or one **owner**. An owner
is the module that registers the variable, named by its module id. A module can also name owners itself and act on
behalf of them, see `Act on behalf of other owners`_. Ownership is per variable, so the component config and an owner
can contribute different variables to the same component.

The owner of a variable:

- registers the variable with its characteristics, attributes, initial values and monitors,
- reports every value that changes,
- applies the writes of the CSMS, and
- keeps the values of its persistent attributes across restarts.

A registration is held in memory only and is repeated at every start of the OCPP service. The monitors of a variable,
the custom monitors of the CSMS included, are stored by component and variable name, and are applied again when the
variable is registered again. Its values are not stored by the OCPP service: the owner keeps them.

The lifecycle
=============

.. mermaid::

   sequenceDiagram
       participant Owner as Owner module
       participant OCPP as OCPP module
       participant CSMS
       OCPP->>Owner: get_registrations()
       Owner-->>OCPP: Registrations
       OCPP->>Owner: set_registration_result(result)
       Note over OCPP: the OCPP service starts once every module answered
       CSMS->>OCPP: SetVariables.req
       OCPP->>Owner: set_value(request)
       Owner-->>OCPP: Accepted
       OCPP-->>CSMS: SetVariables.conf
       Note over Owner: the device changes a value
       Owner-)OCPP: value_changed
       Note over Owner: a device is added at runtime
       Owner-)OCPP: registration
       OCPP->>Owner: set_registration_result(result)

The OCPP module calls ``get_registrations`` of every connected module once at startup, and starts the OCPP service
only after every module answered. The first start removes the monitors of the variables that nobody registered, so a
module answers with **every** owner it acts for, also those whose devices are not available yet. An owner that is
registered later, through the ``registration`` variable, is still accepted, but the monitors of its variables from
earlier boots are gone by then.

Register components
===================

A registration names the components of an owner and the variables of each component. This registration of a cooling
unit at EVSE 1 adds three variables: a temperature reading, a writable fan speed limit and a mode.

.. code-block:: json

   {
     "registrations": [
       {
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
     ]
   }

The registration names no ``owner``, so the owner is the module itself, named by its module id ``cooling_unit``. The ``FanSpeedLimit`` attribute has no
``type``, so it is the Actual attribute. Because it is persistent, the module stores the value of every write and
registers the stored value at the next start, here ``80``.

The ``Temperature`` monitor raises an event when the reported temperature rises above 70 °C, and a second one with
``cleared`` set when it falls back. The OCPP service sends both to the CSMS in a NotifyEvent.req, filtered by the
monitoring level and base the CSMS set, and queued while offline up to ``OfflineQueuingSeverity``. The module is not
told about the events.

The CSMS cannot clear a monitor of either kind. It may change a ``PreconfiguredMonitor`` with a
SetVariableMonitoring.req that names its id. The monitor keeps its id and stays preconfigured. A registration
matches stored monitors by type and severity, so a threshold the CSMS changed to 75 is kept when the module registers
the variable again, even though the registration still lists 70. A changed severity is not kept: the registration
then sets up the monitor with severity 4 again.
A ``HardWiredMonitor`` cannot be changed: a SetVariableMonitoring.req with its id is rejected. It also stays active
when the CSMS sets the monitoring base to ``HardWiredOnly``, which deactivates preconfigured monitors.

Keep these rules in mind when choosing names:

- An ``instance`` is either absent or not empty. A component or variable with an empty instance is rejected with
  ``InvalidNameKey``, because monitors and stored values key an absent instance as an empty one.
- An attribute without ``mutability`` cannot be written.
- The variables that the OCPP service writes from its own state are rejected with ``ReservedVariable``, for example
  ``AuthCacheCtrlr.Storage``, the ``AvailabilityState`` of an EVSE and every variable of a ``NetworkConfiguration``
  slot.

The OCPP module answers each registration with a call of ``set_registration_result``. In the usual case nothing is
rejected:

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

The module receives no writes to a rejected variable and does not report its values.

Act on behalf of other owners
=============================

A module that fronts several devices, a Modbus gateway for two coolant pumps for example, names one owner per device.
Each device then has its own registration, which the module can replace or remove on its own:

.. code-block:: json

   {
     "registrations": [
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
       },
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
     ]
   }

An owner name belongs to the module that registered it first, until that module publishes an empty registration for
it. When a second module registers ``pump-1``, nothing of that registration reaches the device model:

.. code-block:: json

   { "owner": "pump-1", "status": "OwnerInUse", "rejected": [] }

Every write names its owner in ``owner``, so the gateway knows which device to pass it on to. A ``value_changed``
report names its owner the same way. Without ``owner``, a report belongs to the module itself.

Apply writes
============

The OCPP module checks a write against the characteristics and the mutability of the variable, then calls
``set_value``:

.. code-block:: json

   {
     "owner": "cooling_unit",
     "component": { "name": "CoolingUnit", "instance": "1", "evse": { "id": 1 } },
     "variable": { "name": "FanSpeedLimit" },
     "attribute_type": "Actual",
     "value": "60",
     "source": "csms",
     "write_id": 17
   }

The answer goes to the CSMS as it is:

- ``Accepted``: the owner applied exactly ``60``. The OCPP service stores the value in memory, so the module does
  **not** report it again with ``value_changed``.
- ``RebootRequired``: the owner kept exactly ``60``, but the value takes effect only at the next start. The OCPP
  service does not store it: until then, reads return the current value, ``80`` in the example. The module keeps the
  new value and registers it at the next start, and does not report it with ``value_changed`` before it takes effect.
- ``Rejected``: the owner did not apply the value. This includes a value it would only apply in a changed form. A fan
  that runs at most at 55 % rejects the write rather than accepting it and clamping it.

The OCPP module waits for the answer without a deadline, while the CSMS waits for its SetVariables.conf. Answer
promptly, well within the message timeout of the CSMS, and reject a write that the device does not confirm in time.

Writes may overlap, also for one variable. The device model keeps the value of the write with the highest
``write_id``, so the owner applies overlapping writes in the same order: a write to an attribute that arrives after
the owner applied a newer one is rejected.

Report changes
==============

Each value the device changes itself is reported with ``value_changed``. The OCPP service answers reads from the
values it holds, so a change that is not reported does not reach the CSMS.

.. code-block:: json

   {
     "component": { "name": "CoolingUnit", "instance": "1", "evse": { "id": 1 } },
     "variable": { "name": "Temperature" },
     "value": "48.25"
   }

A writable variable can change at the device while a write is in progress. Take this order of events:

#. The CSMS writes ``60`` to ``FanSpeedLimit``, write 17, and the OCPP module calls ``set_value``.
#. Before the write reaches the device, an operator sets the limit to ``40`` at the device, which reports it.
#. The device applies the write, and the module answers ``Accepted``.

Without more information, the OCPP service would store ``60`` after the report and hold a value the device no longer
has. ``applied_write_id`` resolves this: it is the id of the last write to the attribute that the module had applied
when the change happened, ``0`` if it applied none since it registered.

.. code-block:: json

   {
     "component": { "name": "CoolingUnit", "instance": "1", "evse": { "id": 1 } },
     "variable": { "name": "FanSpeedLimit" },
     "value": "40",
     "applied_write_id": 16
   }

A report based on write 16 is ignored if write 17 is already stored, because write 17 replaced the reported value.
A report based on write 17 or a later one is never overwritten by write 17. Without ``applied_write_id``, the report is
applied as it arrives.

A report of a value that does not fit the data type or the ``values_list`` of the variable is ignored, and so is a
report for an owner the module did not register. Since a variable has no reply, the OCPP module only logs it.
``min_limit`` and ``max_limit`` bound the writes of the CSMS, not the reports of the owner.

Implement the interface in C++
==============================

Add the interface to the ``provides`` section of the module manifest and generate the implementation with ev-cli, as
for any other interface:

.. code-block:: yaml

   provides:
     device_model:
       interface: ocpp_device_model
       description: Components and variables of the cooling unit in the OCPP device model

The excerpt below implements the cooling unit of the examples. ``mod->cooling`` stands for the module's driver of the
device, and ``mod->settings`` for whatever the module keeps its persistent values in. Their methods are made up for the
example. It answers ``get_registrations`` from the state the driver read in ``init()``. It reports changes from
``ready()`` on, since a value published in ``init()`` is lost.

.. code-block:: cpp

   namespace {
   types::ocpp::Component cooling_unit() {
       types::ocpp::Component component;
       component.name = "CoolingUnit";
       component.instance = "1";
       component.evse = types::ocpp::EVSE{1, std::nullopt};
       return component;
   }

   types::ocpp_device_model::RegisteredVariable fan_speed_limit(int value) {
       types::ocpp_device_model::RegisteredVariable variable;
       variable.variable.name = "FanSpeedLimit";
       variable.characteristics.data_type = types::ocpp_device_model::DataEnum::integer;
       variable.characteristics.supports_monitoring = false;
       variable.characteristics.unit = "Percent";
       variable.characteristics.min_limit = 0;
       variable.characteristics.max_limit = 100;
       types::ocpp_device_model::VariableAttribute actual;
       actual.value = std::to_string(value);
       actual.mutability = types::ocpp_device_model::MutabilityEnum::ReadWrite;
       actual.persistent = true;
       variable.attributes.push_back(actual);
       return variable;
   }
   } // namespace

   types::ocpp_device_model::Registrations ocpp_device_modelImpl::handle_get_registrations() {
       types::ocpp_device_model::RegisteredComponent component;
       component.component = cooling_unit();
       component.variables.push_back(fan_speed_limit(mod->settings->fan_speed_limit()));
       // Temperature and Mode are added the same way

       types::ocpp_device_model::Registration registration;
       registration.components.push_back(component);
       return {{registration}};
   }

   void ocpp_device_modelImpl::handle_set_registration_result(types::ocpp_device_model::RegistrationResult& result) {
       if (result.status != types::ocpp_device_model::RegistrationStatus::Accepted) {
           EVLOG_error << "Device model registration of " << result.owner << " failed: " << result.status;
           return;
       }
       for (const auto& rejected : result.rejected) {
           EVLOG_warning << "Device model rejected " << rejected.component.name << "." << rejected.variable.name
                         << ": " << rejected.reason;
       }
   }

   types::ocpp::SetVariableStatusEnumType
   ocpp_device_modelImpl::handle_set_value(types::ocpp_device_model::WriteRequest& request) {
       using types::ocpp::SetVariableStatusEnumType;
       if (request.variable.name != "FanSpeedLimit") {
           return SetVariableStatusEnumType::Rejected;
       }
       // the OCPP module checked that the value is an integer from 0 to 100
       const auto limit = std::stoi(request.value);

       std::lock_guard<std::mutex> lock(fan_speed_limit_write_mutex);
       if (request.write_id < applied_fan_speed_limit_write) {
           return SetVariableStatusEnumType::Rejected;
       }
       if (not mod->cooling->set_fan_speed_limit(limit, std::chrono::seconds(5))) {
           return SetVariableStatusEnumType::Rejected;
       }
       applied_fan_speed_limit_write = request.write_id;
       mod->settings->store_fan_speed_limit(limit);
       return SetVariableStatusEnumType::Accepted;
   }

   void ocpp_device_modelImpl::ready() {
       mod->cooling->on_fan_speed_limit_changed([this](int limit) {
           types::ocpp_device_model::ValueChange change;
           change.component = cooling_unit();
           change.variable.name = "FanSpeedLimit";
           change.value = std::to_string(limit);
           change.applied_write_id = applied_fan_speed_limit_write.load();
           mod->settings->store_fan_speed_limit(limit);
           publish_value_changed(change);
       });
   }

``fan_speed_limit_write_mutex`` (a ``std::mutex``) and ``applied_fan_speed_limit_write`` (a ``std::atomic<int>``,
starting at ``0``) are members of ``ocpp_device_modelImpl``, declared in its marker-delimited private section. The
mutex keeps the writes to the attribute in ``write_id`` order. The change callback does not take it: a change
reported while a write is in progress carries the id of the write before, which is what lets the OCPP service tell
the two apart.

Checklist
=========

- Return every owner from ``get_registrations``, also those whose devices are not available yet.
- Answer ``get_registrations`` from data the module holds, without waiting for the OCPP module.
- Do not register variables that the component config defines, or that the OCPP service writes itself.
- Keep the values of persistent attributes, and register them with those values at the next start.
- Answer ``set_value`` promptly, and accept a write only if exactly that value was applied.
- Apply overlapping writes in ``write_id`` order, and do not report an accepted write with ``value_changed``.
- Report every other change with ``value_changed``, with ``applied_write_id`` for writable variables.
- Publish nothing in ``init()``.
