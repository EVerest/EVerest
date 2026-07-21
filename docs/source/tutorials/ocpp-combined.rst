.. _tutorial-ocpp-combined:

*****************************************
OCPP 1.6 and 2.x with the Combined Module
*****************************************

EVerest provides a single module, OCPPmulti, that implements OCPP 1.6J,
OCPP 2.0.1 and OCPP 2.1. It is the recommended way to run OCPP in EVerest and
replaces the separate OCPP 1.6 and OCPP 2.x modules. Which protocol generation
is used is controlled by a single module configuration parameter, ``Mode``,
so the same EVerest configuration can serve charging stations that talk to
1.6 or 2.x CSMSs.

For the full list of configuration parameters, provided and required
interfaces, please see the
:ref:`module documentation <everest_modules_OCPPmulti>`.

This tutorial includes:

- How to run EVerest SIL with OCPPmulti against a local CSMS
- How the OCPP version is chosen via the ``Mode`` parameter
- How to configure the OCPP device model via component configs
- How to handle OCPP 1.6 configuration options
- How to connect to a different CSMS
- How to enable Plug&Charge

.. _tutorial-ocpp-combined-prerequisites:

Prerequisites
=============

If you're new to EVerest, start with our
:ref:`Quick Start Guide <htg_getting_started_sw>`
to get a simulation in EVerest running for the first time.
If you have done that successfully, you can move on with this tutorial.

Depending on the protocol generation you want to test, you need a local CSMS:

- For OCPP 1.6, the Open Source CSMS
  `SteVe <https://github.com/steve-community/steve>`_ is used as an example.
  Make sure you have set up the required docker containers for Mosquitto and
  SteVe.
- For OCPP 2.x, EVerest provides a
  `simple CSMS <https://github.com/EVerest/ocpp-csms>`_ that responds
  "friendly" to all OCPP messages initiated by the charging station. Follow
  the instructions of its README to start it up locally. By default it
  listens on `localhost:9000`.

.. _tutorial-ocpp-combined-run-with-csms:

Run EVerest SIL with a CSMS
===========================

The EVerest repository provides the configuration
`config-sil-ocpp-multi.yaml <https://github.com/EVerest/EVerest/blob/main/config/config-sil-ocpp-multi.yaml>`_
that you can use to run EVerest with OCPPmulti.

Simply run

.. code-block:: bash

    ${EVEREST_WORKSPACE:?}/EVerest/build/run-scripts/run-sil-ocpp-multi.sh

to start EVerest with the combined OCPP module. You can start playing around
with the EVerest simulation to start charging sessions.

Which protocol generation is used is selected by the module's ``Mode``
configuration parameter (see :ref:`tutorial-ocpp-combined-version`). The
example configuration ships with ``Mode: Only1.6``; switch it to
``Mode: Only2`` to run OCPP 2.x:

- With OCPP 1.6, the example configuration connects to SteVe. You have to add
  the chargepoint id *cp001* in SteVe's webinterface to allow the charging
  station to connect. If you want to simulate charging sessions, you also need
  to add OCPP tags for the authorization in SteVe.
- With OCPP 2.x, the default device model configuration connects to
  `localhost:9000`, which is also the default address and port of our simple
  CSMS.

You can find the OCPP message log in different formats in the
`/tmp/everest_ocpp_logs` directory. A new logfile is created every time
EVerest is started.

.. _tutorial-ocpp-combined-version:

Choosing the OCPP version
=========================

The protocol generation (OCPP 1.6 or OCPP 2.x) is selected by the ``Mode``
configuration parameter of the OCPPmulti module:

- ``Only1.6``: run OCPP 1.6
- ``Only2``: run OCPP 2.x (default)
- ``Prefer1.6`` / ``Prefer2``: currently behave exactly like ``Only1.6`` /
  ``Only2``; they are reserved for a possible future automatic fallback to
  the other protocol generation, which is not implemented yet

Within OCPP 2.x, the concrete version (2.0.1 or 2.1) is negotiated with the
CSMS during the initial websocket handshake. The ``SupportedOcppVersions``
variable of the ``InternalCtrlr`` device model component controls which 2.x
versions the charging station offers; it is a comma-separated list of
``ocpp2.1`` and ``ocpp2.0.1`` in order of preference (by default both are
offered). Configure it like any other device model variable, by setting the
``value`` of its ``Actual`` attribute in the ``InternalCtrlr`` component
config (see :ref:`tutorial-ocpp-combined-device-model`).

.. note::

  It depends on the CSMS which of the offered 2.x versions is actually used.
  For more information about the version selection, please refer to the
  OCPPmulti :ref:`module documentation <everest_modules_OCPPmulti>` and
  the **OCPP 2.1 Part 4 - JSON over WebSockets implementation guide**.

.. _tutorial-ocpp-combined-device-model:

Device model configuration via component configs
=================================================

OCPPmulti is configured through the OCPP device model, regardless of the
OCPP version in use. OCPP 2.x defines a device model structure and a lot of
standardized variables that are used within the functional requirements of the
protocol. Please see "Part 1 - Architecture & Topology" of the OCPP 2.0.1
specification for further information about the device model and how it is
composed. You should be familiar with the OCPP 2.x terms **Component**,
**Variable**, **VariableCharacteristics**, **VariableAttributes** and
**VariableMonitoring** to be able to follow the further explanations.

.. note::

  The device model configured here is the **OCPP device model**: components
  and variables that you as an integrator define and configure via JSON
  component config files. It is distinct from the **EVerest device model**,
  which the module builds at runtime from the connected EVerest modules
  (e.g. EVSE and Connector components). You do not configure the latter via
  component configs. Please see the
  :ref:`module documentation <everest_modules_OCPPmulti>` for a full
  explanation of the two device models and how they interact.

Device model definition and configuration structure
----------------------------------------------------

The device model is defined and configured using JSON files. These files serve
two main purposes:

* **Definition**: the device model (including its components and variables)
* **Configuration**: the value of variable attributes

There is one JSON file for each Component. Each Component contains the
definition of its Variables. Each Variable contains the definition of its
VariableCharacteristics, VariableAttributes and VariableMonitoring. The actual
value of a Variable can be configured as part of the VariableAttribute(s).

This is how a definition and configuration for the ``LocalAuthListCtrlr``
component could look like (abbreviated):

.. code-block:: json

  {
    "description": "Schema for LocalAuthListCtrlr",
    "name": "LocalAuthListCtrlr",
    "type": "object",
    "properties": {
      "LocalAuthListCtrlrEnabled": {
        "variable_name": "Enabled",
        "characteristics": {
          "supportsMonitoring": true,
          "dataType": "boolean"
        },
        "attributes": [
          {
            "type": "Actual",
            "mutability": "ReadWrite",
            "value": true
          }
        ],
        "description": "If this variable exists and reports a value of true, Local Authorization List is enabled.",
        "default": true,
        "type": "boolean"
      },
      "BytesPerMessageSendLocalList": {
        "variable_name": "BytesPerMessage",
        "characteristics": {
          "supportsMonitoring": true,
          "dataType": "integer"
        },
        "attributes": [
          {
            "type": "Actual",
            "mutability": "ReadOnly",
            "value": 4096
          }
        ],
        "description": "Maximum number of bytes in a SendLocalList message.",
        "type": "integer"
      },
      "LocalAuthListCtrlrEntries": {
        "variable_name": "Entries",
        "characteristics": {
          "supportsMonitoring": true,
          "dataType": "integer"
        },
        "attributes": [
          {
            "type": "Actual",
            "mutability": "ReadOnly"
          }
        ],
        "description": "Amount of IdTokens currently in the Local Authorization List",
        "type": "integer"
      }
    },
    "required": [
      "BytesPerMessageSendLocalList",
      "LocalAuthListCtrlrEntries"
    ]
  }

You can change the components according to your needs, but note that the
definitions for the ``variable_name`` and ``characteristics`` are usually
defined by the OCPP 2.x specifications. To configure a variable attribute
value, specify the ``value`` for the attribute type that you would like to
configure. In the example above, the actual value of the VariableAttribute of
the Variable ``Enabled`` is set to ``true``. Note that not all variables have
specified variable attributes with a ``value``, e.g.
``LocalAuthListCtrlrEntries`` does not specify a value. It is telemetry rather
than configuration, and therefore a configuration value it is not required.
It's an example of a variable that is only defined.

The config files are parsed at startup and used to initialize an SQLite
database. You should specify the path to the directory of your device model
definitions using the configuration parameter ``DeviceModelConfigPath`` of the
OCPPmulti module within EVerest. It shall point to the directory where
the component files are located in these two subdirectories:

* standardized
* custom

By default, ``DeviceModelConfigPath`` is pointing to the installation
directory of the component files. The split between the two directories only
has semantic reasons. The **standardized** directory usually does not need to
be modified since it contains standardized components and variables that the
specification refers to in its functional requirements. The **custom**
directory is meant to be used for components that are custom for your specific
charging station.

It is recommended to use the
`device model definitions of libocpp <https://github.com/EVerest/EVerest/tree/main/lib/everest/ocpp/config/common/component_config>`_
as a starting point. This is an exemplary device model configuration for two
EVSEs.

.. _tutorial-ocpp-combined-ocpp16-config:

OCPP 1.6 configuration options
==============================

Historically, the OCPP 1.6 implementation was configured using a separate JSON
configuration file containing the OCPP 1.6 configuration keys. With
OCPPmulti, the OCPP 1.6 configuration keys are also served from the
device model, so there are two ways to configure OCPP 1.6:

1. **Pure device model** (recommended for new deployments): Configure
   everything via the component configs described in
   :ref:`tutorial-ocpp-combined-device-model`. The module maps OCPP 1.6
   configuration keys to device model variables via built-in mappings
   (documented in
   `lib/everest/ocpp/config/v16_to_v2_mapping.md <https://github.com/EVerest/everest-core/blob/main/lib/everest/ocpp/config/v16_to_v2_mapping.md>`_).
   Custom mappings can be provided using the ``DeviceModelConfigMappings``
   configuration parameter.

2. **Legacy JSON with a one-time migration**: For existing OCPP 1.6
   deployments, set ``EnableLegacyConfigMigration: true``. On the first
   startup (while the device model database does not yet exist), the legacy
   OCPP 1.6 JSON config referenced by ``ChargePointConfigPath`` is migrated
   into the device model database once. Network connection details
   (``CentralSystemURI``, ``SecurityProfile``, ``AuthorizationKey``, ...) are
   migrated to the NetworkConfiguration slot given by
   ``Ocpp16NetworkConfigSlot``. Once the database is initialized, the
   migration is skipped and the legacy JSON is no longer read.

For a detailed walkthrough of migrating an existing OCPP 1.6 deployment,
please see the
:ref:`storage migration guide <howto-ocpp-storage-migration>`.

.. _tutorial-ocpp-combined-network-configuration:

Network configuration
=====================

OCPP uses **NetworkConfiguration** components in the device model to define
how the charging station connects to a CSMS. This applies to all protocol
versions, including OCPP 1.6. Each connection profile is stored as a separate component
instance (called a **slot**), and a priority list determines the order in
which slots are tried during connection and failover.

.. _tutorial-ocpp-combined-different-csms:

Connect to a different CSMS
---------------------------

Each connection profile is defined in its own JSON file under the device model
configuration directory. At least two slots must be configured; you may add as
many additional slots as you need (see
:ref:`tutorial-ocpp-combined-adding-slots`). The default profiles are:

- ``component_config/standardized/NetworkConfiguration_1.json`` (slot 1)
- ``component_config/standardized/NetworkConfiguration_2.json`` (slot 2)

To connect to a different CSMS, modify the following variables in the
appropriate slot's JSON file:

- ``OcppCsmsUrl``: The WebSocket endpoint of the CSMS, **without** the
  charging-station identifier path segment (e.g. ``ws://csms.example.com:9000``
  or ``wss://csms.example.com:443``). The identifier from ``Identity`` is
  appended at connect time.
- ``SecurityProfile``: Defines the transport layer security level:

  - ``0``: No security (OCPP 1.6 compatibility, disabled by default)
  - ``1``: Basic authentication over ``ws://``
  - ``2``: TLS with server certificate over ``wss://``
  - ``3``: TLS with mutual authentication (client + server certificates) over ``wss://``

- ``MessageTimeout``: Message timeout in seconds (minimum 1)

Each slot can optionally override the charging station's identity and
authentication credentials:

- ``Identity``: Per-slot identity override. If empty, falls back to ``SecurityCtrlr.Identity``.
- ``BasicAuthPassword``: Per-slot password override. If empty, falls back to ``SecurityCtrlr.BasicAuthPassword``.

The global fallback values are configured in the ``SecurityCtrlr`` component:

- ``Identity`` in ``SecurityCtrlr``: The default identity of the charging station
- ``BasicAuthPassword`` in ``SecurityCtrlr``: The default password for HTTP Basic Authentication (SecurityProfile 1 or 2)

The **connection priority** is controlled by the ``NetworkConfigurationPriority``
variable in ``OCPPCommCtrlr``. This is a comma-separated list of slot numbers
that determines the order in which profiles are tried. For example, ``"1,2"``
means slot 1 is tried first; if it fails, slot 2 is tried, then back to slot 1
(round-robin failover).

.. note::

  For TLS (SecurityProfile 2 or 3), you must prepare the required certificates
  and private keys. Please see the documentation of the
  :ref:`EvseSecurity module <everest_modules_EvseSecurity>` for more information
  on how to set up the TLS connection for OCPP.

.. _tutorial-ocpp-combined-adding-slots:

Adding more network configuration slots
---------------------------------------

By default, two NetworkConfiguration slots are shipped. To add more:

1. **Create the JSON file.** Copy an existing slot file (e.g. ``NetworkConfiguration_1.json``)
   to a new file named ``NetworkConfiguration_N.json`` where ``N`` is your slot number.

2. **Update the instance number.** In the new file, change the ``"instance"`` field
   at the top level to match your slot number:

   .. code-block:: text

     {
       "name": "NetworkConfiguration",
       "instance": "3",
       ...
     }

3. **Configure the slot's variables.** Set ``OcppCsmsUrl``, ``SecurityProfile``,
   and other variables as needed for this connection profile.

4. **Add the slot to the priority list.** In ``OCPPCommCtrlr.json``, append the new
   slot number to the ``NetworkConfigurationPriority`` value. For example, change
   ``"1,2"`` to ``"1,2,3"``.

5. **Rebuild and restart.** The device model database will be re-initialized with the
   new slot on next startup.

.. _tutorial-ocpp-combined-enable-pnc:

Enable Plug&Charge
==================

In order to enable Plug&Charge, adjust your component files according to the
:doc:`Plug&Charge configuration documentation </how-to-guides/configure-pnc>`.

----

**Authors**: Piet Gömpel
