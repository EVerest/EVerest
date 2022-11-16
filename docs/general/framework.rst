.. doc_framework:

EVerest framework
#################

See our Quick Start Guide for a fast overview and first steps to create new modules: :ref:`general/quick_start_guide:a real quick guide to everest`

EVerest uses the principle of loose coupling of modules that allow configuration during runtime.

Modules
-------

`Modules <https://github.com/EVerest/everest-core/tree/main/modules>`_ consist of one or more interfaces.
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

Interfaces
----------

WIP


