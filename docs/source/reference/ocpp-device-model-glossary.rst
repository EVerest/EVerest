.. _reference-ocpp-device-model-glossary:

##########################
OCPP Device Model Glossary
##########################

The terms that the documentation of the OCPP device model uses: in
:ref:`How the device model is composed <everest_modules_handwritten_OCPPmulti_device_model>`, in
:ref:`howto-ocpp-device-model`, in :ref:`howto-ocpp-storage-migration` and in the documentation of the
:ref:`ocpp_device_model_API <everest_modules_handwritten_ocpp_device_model_API>` module.

Each entry says what the term means in EVerest. Where OCPP defines the term, the entry only adds what is specific to
EVerest.

Who is involved
===============

.. glossary::
   :sorted:

   CSMS
      The Charging Station Management System, the backend that the charging station connects to with OCPP. It reads
      and writes the :term:`device model` with GetVariables and SetVariables in OCPP 2.x, and with GetConfiguration
      and ChangeConfiguration in OCPP 1.6.

   OCPP module
      The EVerest module that speaks OCPP, :ref:`OCPPmulti <everest_modules_OCPPmulti>`. It holds the
      :term:`device model` and is the only module that talks to the :term:`CSMS`.

   OCPP service
      The charge point that libocpp runs inside the :term:`OCPP module`. It starts once the module has composed the
      :term:`device model`, which includes waiting for the startup registrations, and
      only then connects to the CSMS.

   integrator
      Whoever configures EVerest for a station: the EVerest configuration, the :term:`component configuration` and
      the :term:`key mapping file`. The integrator decides what the station is given; an :term:`application` decides
      what it registers.

   application
   external application
      A program outside EVerest that adds variables to the device model through the
      :ref:`ocpp_device_model_API <everest_modules_handwritten_ocpp_device_model_API>`, over MQTT. It acts for one or
      more :term:`external owners <external owner>`. The module documentation calls it a *client*.

The device model
================

.. glossary::
   :sorted:

   device model
      The tree of :term:`components <component>` and :term:`variables <variable>` that describes the charging station
      to the CSMS. OCPP 2.x defines it; OCPPmulti serves OCPP 1.6 from the same model through a
      :term:`key mapping`. It is composed at every start from the :term:`component configuration` and from what the
      :term:`owners <owner>` register, and it answers every read from memory.

   device model database
      The SQLite database at ``DeviceModelDatabasePath`` of the OCPP module, in which the device model keeps what
      must survive a restart. :ref:`What the database stores <handwritten_ocppmulti_what-the-database-stores>` lists
      what that is, and what stays with the owners instead.

   component configuration
   component config
      The JSON files under ``DeviceModelConfigPath`` in which the :term:`integrator` defines components and
      variables, see :ref:`Device model configuration via component configs
      <handwritten_ocppmulti_device-model-component-configs>`. In the device model, the variables they define belong
      to the owner ``COMPONENT_CONFIG``. It is not the EVerest module configuration, which reaches the device model
      as :term:`module configuration views <module configuration view>`.

   component
      A part of the station that the device model describes, a controller or a device, identified by its name and
      optionally by an instance, an EVSE and a connector: ``OCPPCommCtrlr``, or ``CoolingUnit`` with instance ``1``
      on EVSE 1. Several :term:`owners <owner>` and the :term:`component configuration` can contribute different
      variables to one component.

   variable
      One value of a :term:`component`, identified by its name and optionally an instance: ``HeartbeatInterval``.
      A variable has :term:`characteristics`, one or more :term:`attributes <attribute>` and optionally
      :term:`monitors <monitor>`. Every variable has exactly one definition, either in the
      :term:`component configuration` or by one :term:`owner`.

   attribute
      One of the up to four values of a :term:`variable`: ``Actual``, ``Target``, ``MinSet`` and ``MaxSet``. Without
      a type, ``Actual`` is meant. Each attribute has a value, a :term:`mutability` and a ``persistent`` flag. OCPP
      1.6 reaches only the ``Actual`` attribute.

   characteristics
      The fixed description of a :term:`variable`: its data type, unit, limits, list of allowed values and whether
      it supports monitoring. The device model validates every write against them before the write reaches the
      :term:`owner`.

   mutability
      Whether the CSMS may read and write an :term:`attribute`: ``ReadOnly``, ``WriteOnly`` or ``ReadWrite``. An
      attribute registered without one is ``ReadOnly``. A ``ReadOnly`` attribute of an owner is a value the owner
      reports.

   monitor
      A rule on one :term:`variable` that makes the OCPP service send a NotifyEvent.req to the CSMS, when the value
      crosses a threshold, changes by more than a delta, or periodically. OCPP 2.x only. The kinds of monitors, who
      declares them and how they are stored are described in :ref:`Monitors <handwritten_ocppmulti_monitors>`.

Owners
======

.. glossary::
   :sorted:

   owner
      The name under which a set of variables is added to the device model, and with it the one party that defines
      these variables, holds their values and answers the writes to them. The owners that EVerest itself uses are
      listed in :ref:`Who contributes <handwritten_ocppmulti_who-contributes>`; every other name is an
      :term:`external owner`. The rules that apply to every owner are the
      :ref:`Rules of composition <handwritten_ocppmulti_rules-of-composition>`.

   runtime owner
      Any :term:`owner` other than the :term:`component configuration`: its variables are registered at every start
      instead of being read from the database. libocpp, the components EVerest provides, the
      :term:`module configuration views <module configuration view>` and the
      :term:`external owners <external owner>` are runtime owners.

   external owner
      An :term:`owner` that an :term:`application` names and registers through the ``ocpp_device_model`` interface,
      one per device for example, see :ref:`External owners <handwritten_ocppmulti_external-owners>` and
      :ref:`howto-ocpp-device-model`.

   expected owner
      An :term:`external owner` listed in ``cfg_expected_owners`` of the ``ocpp_device_model_API`` module, whose
      registration the module waits for before it lets the :term:`OCPP service` start, see
      :ref:`Configure the API module <howto-ocpp-device-model-configure-api-module>`.

   module configuration view
      The configuration of another EVerest module shown in the device model, under the owner ``EVEREST_CONFIG``, see
      :ref:`EVerest module configuration views <handwritten_ocppmulti_module-configuration-views>`.

   registered variable
      A :term:`variable` that a :term:`runtime owner` defines in a registration, as opposed to a variable of
      the :term:`component configuration`. It exists only while its owner is registered.

   value provider
      The part of the OCPP module that passes a write to a registered variable on to its :term:`owner` and
      returns the owner's answer. For an external owner it calls ``set_value`` of the ``ocpp_device_model``
      interface. It is unrelated to a module that *provides* an interface in its manifest.

OCPP 1.6 keys
=============

.. glossary::
   :sorted:

   configuration key
      The name under which OCPP 1.6 addresses one setting in GetConfiguration and ChangeConfiguration. OCPP 1.6
      knows no components and variables, so every key that OCPPmulti serves is mapped onto a :term:`variable` by a
      :term:`key mapping`.

   built-in key
      A :term:`configuration key` that the OCPP 1.6 stack of libocpp knows: the keys of the OCPP 1.6 specification
      and its extensions, and the keys libocpp adds. Each has a built-in :term:`key mapping`.

   custom key
      A :term:`configuration key` that is not a :term:`built-in key`, a vendor-specific setting for example. It
      exists only through an entry of the :term:`key mapping file` or as a :term:`declared key`, see
      :ref:`Add mappings for custom OCPP 1.6 keys <howto-ocpp-storage-migration-custom-key-mappings>`.

   key mapping
      The assignment of one :term:`configuration key` to the ``Actual`` attribute of one variable, its
      :term:`mapping target`. Every key maps to at most one variable, and every variable back to at most one key.
      The mappings come from three places: the built-in table for the :term:`built-in keys <built-in key>`, the
      :term:`key mapping file`, and the :term:`declared keys <declared key>` of external owners.

   key mapping file
   mapping file
      The YAML file at ``Ocpp16KeyMappingsPath`` of the OCPP module, in which the :term:`integrator` maps
      :term:`custom keys <custom key>` and :term:`overrides <override>`. Its format and rules are in
      :ref:`Add mappings for custom OCPP 1.6 keys <howto-ocpp-storage-migration-custom-key-mappings>`.

   mapping target
      The :term:`variable` that a :term:`key mapping` points to. Which variables can be targets is described in
      :ref:`Add mappings for custom OCPP 1.6 keys <howto-ocpp-storage-migration-custom-key-mappings>`.

   declared key
      A :term:`configuration key` that an :term:`external owner` names for one of its variables with ``ocpp16_key``
      in its registration. It is mapped while the registration lasts: a later registration of the owner
      replaces its keys, and a release removes them. In OCPP 2.x mode it is ignored.

   OCPP16LegacyCtrlr
      The component that holds the :term:`built-in keys <built-in key>` without a standardized OCPP 2.x variable,
      ``LightIntensity`` for example.

   override
      A :term:`key mapping` that points a :term:`built-in key` stored in :term:`OCPP16LegacyCtrlr` to another
      variable than its built-in one. The rules for an override in the :term:`key mapping file` are in
      :ref:`Share an OCPP 1.6 key with your own OCPP 2.x variable <howto-ocpp-storage-migration-share-legacy-key>`,
      those for an override by a :term:`declared key` in :ref:`howto-ocpp-device-model`.
