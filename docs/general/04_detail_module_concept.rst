.. detail_module_concept:

.. _moduleconcept_main:

######################
EVerest Module Concept
######################

What parts does a module in EVerest consist of?

- Interface definition
- Types definition
- Module implementation

Let's have a quick look to those parts in the following sections.

.. important:: 

  This documentation has been written during a work in progress which would change interface and types definitions from JSON to YAML. This will be reflected in short here.

Interfaces
==========

An interface generally describes a specific object in the EVerest world. Those objects can be device types, protocol standards, authentication instances and so on.

Everything that you will want to integrate into EVerest as a module will need to have an interface definition.

A short view on an interface describing a powermeter:

.. code-block:: json

  {
    "description": "This interface defines a generic powermeter for 5 wire TN networks.",
    "cmds": {
        "get_signed_meter_value": {
            "description": "Returns a signed meter value with the given auth token",
            "arguments": {
                "auth_token": {
                    "description": "Auth token",
                    "type": "string",
                    "minLength": 1,
                    "maxLength": 20
                }
            },
            "result": {
                "description": "Signed meter value",
                "type": "string"
            }
        }
    },
    "vars": {
        "powermeter": {
            "description": "Measured dataset",
            "type": "object",
            "$ref": "/powermeter#/Powermeter"
        }
    }
  }

Let's walk in short through the keys:

The description simply tells you in short, which type of object the interface describes.

Interfaces have commands (cmds) which can be called from the outside synchronously. Those commands can take arguments which can be limited regarding its values. As an example, see the `minLength` and `maxLength` keys in the code above.

Besides that, variables (vars) can be consumed by other modules in an asynchronous way. Variables are published by the interface implementing object. The powermeter example above does that with the measured dataset. Example: If module A implements this interface and module B connects to it, module B will get the measured dataset as often as module A publishes that data. 

Both cmds and vars can be defined as simple data types (string, bool etc) or as object type - in case you want to have a more sophisticated structure than a simple type.

Those object types have to be defined. In EVerest, we do this as a Type Definition.

Types
=====

A short view on how the powermeter type could look like:

.. code-block:: json

  {
    "description": "Powermeter types",
    "types": {
        "Powermeter": {
            "description": "Measured dataset",
            "type": "object",
            "additionalProperties": false,
            "required": [
                "timestamp",
                "energy_Wh_import"
            ],
            "properties": {
                "timestamp": {
                    "description": "Timestamp of measurement",
                    "type": "number"
                },
                "meter_id": {
                    "description": "A (user defined) meter if (e.g. id printed on the case)",
                    "type": "string"
                }
            }
        }
    }
  }

This type has been used and referenced in the powermeter interface.

You can understand the interface description as the description of a general powermeter device and the powermeter type as a data object that is used by a powermeter device to exchange measurement information.

The type definition tells EVerest which properties this type has. This is the data structure of the type. The JSON key *required* defines what is needed.

With this, we have now interfaces and types set. Let's have a look at the module:

Modules
=======

Each module resides in the `modules <https://github.com/EVerest/everest-core/tree/main/modules>`_ directory as a subdirectory.

The modules consist of one or more interfaces.

Each module is defined by its `manifest.json` file:

+----------------+-----------------------------------------------+------------------------------------------------------------------+
|    JSON KEY    |    description                                | value type                                                       |
+================+===============================================+==================================================================+
| *description*  | useful description of what the module does    | string                                                           |
+----------------+-----------------------------------------------+------------------------------------------------------------------+
| *provides*     | interfaces provided by the module,            | {<interface id1>: {interface, description, [config]},            |
|                | can be required by other modules              | <interface id2>: {interface, description, [config]}, ... }       |
+----------------+-----------------------------------------------+------------------------------------------------------------------+
| *requires*     | interfaces required by the module,            | {<interface id1>: {interface}, <interface id2>: {interface},...} |
|                | needs to be provided by other module(s)       |                                                                  |
+----------------+-----------------------------------------------+------------------------------------------------------------------+
| *metadata*     | metadata                                      | {license: <string>, authors: <array>}                            |
+----------------+-----------------------------------------------+------------------------------------------------------------------+

An example module can be found `here <https://github.com/EVerest/everest-core/tree/main/modules/Example>`_.

So, the *manifest.json* contains information about which interfaces are implemented by the module including the parameters needed to configure those interfaces. Also, it defines which interface implementations are needed to be implemented by other modules which are connecting to this module.

Further files can be found in the *module* directory:
- .cpp and .hpp code files for the implementations
- CMakeList.txt file to define needed libraries for the cmake run
- Implementations of interfaces in separate code files

Connecting The Modules
======================

Now as we have learnt about the basic concept of modules, we will need to see how to glue everything together.

When setting up EVerest as shown in the Quick Start Guide, you will receive a first example of a module network. This is delivered as a first example by using the EDM tool.

You can find some examples of module connection definitions in the `everest-core` repository in directory `config`. Have a look at the YAML files to get an idea of how those module connections are defined there.

Where To Go Next
================

If you came here via the Quick Start Guide, here is your way back to action:
:ref:`Quick Start Guide to setup a module <quickstartguide_modulesetup>`.
