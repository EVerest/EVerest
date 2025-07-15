.. release_notes:

.. _release_notes_main:

#####################
EVerest Release Notes
#####################

See also our detailed release information based on all PRs and source code
commits on GitHub:
`EVerest releases <https://github.com/EVerest/everest-core/releases>`_

EVerest releases are scheduled monthly. A release for a dedicated month can be
expected in the beginning of the following month. Each third release will be a
consolidated release that includes a feature freeze phase before the release
date.

.. hint::

    Currently, the consolidated releases will be the ones in January, April,
    July and October.
    Although we want to keep the consolidated release cadence this way, this
    could also change in the future.

Here are the some high-level changes for the last releases (starting from the
most recent one backwards):

2025.5.0
========

General changes
---------------

* ISO 15118-20: Added pause/resume support. Dynamic Mode is now fully
  supported, Scheduled Mode is partly supported. For more information,
  see `PR 1095 <https://github.com/EVerest/everest-core/pull/1095>`_.
* Added rejection reasons for payment process; see
  `PR 1129 <https://github.com/EVerest/everest-core/pull/1129>`_.
* Beautified process for SLAC failures. Some car models will restart the
  SLAC communication during the process. Introducing a config flag to allow
  non-standard compliant behaviour and get the SLAC process restarted
  without re-plugging the car; see
  `PR 1181 <https://github.com/EVerest/everest-core/pull/1181>`_.

OCPP
----

* Related to OCPP certification: Updating id_token_info after transaction
  event by OCPP; see
  `PR 1151 <https://github.com/EVerest/everest-core/pull/1151>`_.
* Optimized firmware update process; see
  `PR 1171 <https://github.com/EVerest/everest-core/pull/1171>`_.
* Optimizations regarding Device Model in OCPP 2.0.1; see
  `PR 1188 <https://github.com/EVerest/everest-core/pull/1188>`_ and
  `PR 1192 <https://github.com/EVerest/everest-core/pull/1192>`_.
* Added support for OCMF Tariff Texts; see
  `PR 1186 <https://github.com/EVerest/everest-core/pull/1186>`_.
* Updated EvseManager's inoperative error to include details of original cause;
  see `PR 1175 <https://github.com/EVerest/everest-core/pull/1175>`_.

Minor changes and bug fixes
---------------------------

* Make sure that charger state machine can only be started once; see
  `PR 1157 <https://github.com/EVerest/everest-core/pull/1157>`_.
* Optmized reset handling by adding a delay; see
  `PR 1165 <https://github.com/EVerest/everest-core/pull/1165>`_ and
  `PR 1179 <https://github.com/EVerest/everest-core/pull/1179>`_.

For further fixes and enhancements, see
`Release 2025.5.0 overview on GitHub <https://github.com/EVerest/everest-core/releases/tag/2025.5.0>`_.

2025.4.1 - LATEST CONSOLIDATED RELEASE
======================================

* Updated OCPP module documentation for California Pricing; see
  `PR 1169 <https://github.com/EVerest/everest-core/pull/1169>`_.
* Fix Cargo.lock version accidentally set to 4 when 3 is correct

2025.4.0
========

General changes
---------------

* Plug&Charge configuration options added and made runtime-configurable via
  OCPP; see `PR 1128 <https://github.com/EVerest/everest-core/pull/1128>`_.
* New module: Isabellenhuette IEM-DCR powermeter; see
  `PR 1059 <https://github.com/EVerest/everest-core/pull/1059>`_.
* Power limit handling adapted in case of charger reduces the power.
  Sometimes, EVs ignore the new maximum limits. Checking that and set the
  limits now; see
  `PR 1167 <https://github.com/EVerest/everest-core/pull/1167>`_.
* Removed all remaining JS modules; see
  `PR 1102 <https://github.com/EVerest/everest-core/pull/1102>`_.

OCPP
----

* Added support for Power.Offered meter value; see
  `PR 1132 <https://github.com/EVerest/everest-core/pull/1132>`_.
* Updated information via TransactionEvent or StartTransaction can be received
  for specific connectors; see
  `PR 1151 <https://github.com/EVerest/everest-core/pull/1151>`_.

Security and Authorization
--------------------------

* Check TLS server state before sending information about TLS support to
  prevent false TLS support messages; see
  `PR 1135 <https://github.com/EVerest/everest-core/pull/1135>`_.
* mbed TLS removed from EvseV2G; OpenSSL is now used exclusively; see
  `PR 1008 <https://github.com/EVerest/everest-core/pull/1008>`_.v
* The get_installed_certificates method did not return all installed leaf
  certificates for V2GCertificateChain; see
  `PR 104 <https://github.com/EVerest/libevse-security/pull/104>`_.

Bug fixes and minor changes
---------------------------

* Enabling and disabling the payment terminal is now possible during runtime;
  see `PR 1056 <https://github.com/EVerest/everest-core/pull/1056>`_.
* Cost information moved from display message interface to a dedicated message
  type; see `PR 1079 <https://github.com/EVerest/everest-core/pull/1079>`_.
* Fix for preventing a potential deadlock in Auth module; see
  `PR 1131 <https://github.com/EVerest/everest-core/pull/1131>`_.
* Set HLC early in DC charging mode; see
  `PR 1154 <https://github.com/EVerest/everest-core/pull/1154>`_.
* Optimized PWM and SLAC communication in HLC mode by adding sleep time; see
  `PR 1125 <https://github.com/EVerest/everest-core/pull/1125>`_.

For further fixes and enhancements, see
`Release 2025.4.0 overview on GitHub <https://github.com/EVerest/everest-core/releases/tag/2025.4.0>`_.

2025.3.0
========

General changes
---------------

* Enhancements regarding EV charging communication: Querying and publishing of
  EV information by EvseV2G and Evse15118D20 modules and also let the OCPP201
  module subscribe to that information; see
  `PR 1062 <https://github.com/EVerest/everest-core/pull/1062>`_.
* Allowing withdrawing of authorization; see
  `PR 992 <https://github.com/EVerest/everest-core/pull/992>`_.
* Adding a new C++ implementation of a Yeti simulator; see
  `PR 862 <https://github.com/EVerest/everest-core/pull/862>`_.

ISO 15118-20-related
--------------------

* * TLS 1.3 support optimized in Evse15118D20 and PyEvJosev; see
  `PR 1077 <https://github.com/EVerest/everest-core/pull/1077>`_.
* Forward pre-charge events to make power supply interface aware of entering
  that charging phase; see
  `PR 1091 <https://github.com/EVerest/everest-core/pull/1091>`_.

OCPP
----

* OCPP 2.1 BiDi Smart Charging use cases: Setting setpoints and tracking the
  charging limits. Added support for the energy types and includes changes
  in EnergyManager, EnergyNode and EvseManager; see
  `PR 1033 <https://github.com/EVerest/everest-core/pull/1033>`_.
* Enhanced OCPP 2.1 support by adjusting data types and enums in EVerest; see
  `PR 1083 <https://github.com/EVerest/everest-core/pull/1083>`_.

Bug fixes and minor changes
---------------------------

* Fixed selection handling of connectors in Auth module; see
  `PR 1099 <https://github.com/EVerest/everest-core/pull/1099>`_.
* State transitions optimized for cancelled reservations; see
  `PR 1108 <https://github.com/EVerest/everest-core/pull/1108>`_.
* Differentiation between EV vs EVSE finishing state; see
  `PR 954 <https://github.com/EVerest/everest-core/pull/954>`_.
* State change behavious fixed for EvseManager's enable_disable() request; see
  `PR 1092 <https://github.com/EVerest/everest-core/pull/1092>`_.

For further fixes and enhancements, see
`Release 2025.3.0 overview on GitHub <https://github.com/EVerest/everest-core/releases/tag/2025.3.0>`_.

2025.2.0
========

Important development change
----------------------------

* Changed naming of OCPP v201 to v2; see
  `PR 1058 <https://github.com/EVerest/everest-core/pull/1058>`_.

General Changes
---------------

* Direct communication between ISO 15118 and OCPP modules is now possible. No
  need to use EvseManager for communication anymore; see
  `PR 1022 <https://github.com/EVerest/everest-core/pull/1022>`_.
* New over-voltage monitor interface. This is for IEC61851-23:2023 6.3.1.106.2
  compatible hardware to do an emergency switch-off when an over-voltage occurs
  on the DC output; see
  `PR 1055 <https://github.com/EVerest/everest-core/pull/1055>`_.
* EVerest types iso15118_charger and iso15118_ev have been merged into the
  iso15118 type; see
  `PR 1050 <https://github.com/EVerest/everest-core/pull/1050>`_.
* Added TPM2 support for EvseV2G TLS server private key; see
  `PR 1021 <https://github.com/EVerest/everest-core/pull/1021>`_.
* New command to set current and phase limit for EVSE; see
  `PR 1016 <https://github.com/EVerest/everest-core/pull/1016>`_.
* Refactoring Eichrecht implementation of LEM; see
  `PR 1035 <https://github.com/EVerest/everest-core/pull/1035>`_.
* Removed Javascript modules from OCPP integration test configs; see
  `PR 1082 <https://github.com/EVerest/everest-core/pull/1082>`_.


Bug fixes
---------

* DcPowerSupplySimulator caused a crash due to variables were set to NULL
  instead of a double value; see
  `PR 1054 <https://github.com/EVerest/everest-core/pull/1054>`_.
* Fixing and improving the handling of SLAC messages, link status detection,
  and state transitions within the EVSE, SLAC and FSM; see
  `PR 928 <https://github.com/EVerest/everest-core/pull/928>`_.
* Fixed the unlock handling in EvseManager; see
  `PR 1078 <https://github.com/EVerest/everest-core/pull/1078>`_.

Further fixes and enhancements, see
`Release 2025.2.0 overview on GitHub <https://github.com/EVerest/everest-core/releases/tag/2025.2.0>`_.


2025.1.0
========

General changes
---------------

* Accept bank cards for dedicated connectors.
* Optimized reservation handling in EvseManager, e.g. allowing a direct state
  change from "Reserved" to "Unavailable" and fixed a bug. See
  `PR 1067 <https://github.com/EVerest/everest-core/pull/1067>`_.
* New documentation and tests, e.g. for EnergyManager and EvseV2G.

Authorization and security
--------------------------

* Added security implementation to Evse15118D20 module, see
  `PR 1030 <https://github.com/EVerest/everest-core/pull/1030>`_.
* Fix error handling for authorization timeouts, see
  `PR 1032 <https://github.com/EVerest/everest-core/pull/1032>`_.
* Plug-in timeout handling: Replug necessary including authorization request,
  see `PR 976 <https://github.com/EVerest/everest-core/pull/976>`_.

Hardware
--------

* Added NXP PN7160 NFC chip support as token provider module.

Further changes
---------------

* Fix for reservation of connectors, see
  `PR 972 <https://github.com/EVerest/everest-core/pull/972>`_.
* Further bug-fixes, e.g. compilation issues on ARM, configuration parsing error
  for Python modules, solve Rust binding issues etc.
* Remove everest-modbus as dependency.

Further fixes and enhancements, see
`Release 2025.1.0 overview on GitHub <https://github.com/EVerest/everest-core/releases/tag/2024.11.0>`_.

2024.12.0
=========

Decision to have the quarterly consolidated release cadence moved one month further.
Reason for this is the December holidays. So, see January 2025 release.

2024.11.0
=========

General changes
---------------

* Adding ISO 15118-20 Dynamic Mode support. See
  `PR 912 <https://github.com/EVerest/everest-core/pull/912>`_.
* Added composed device model storage and EVerest device model storage, see
  `PR 846 <https://github.com/EVerest/everest-core/pull/846>`_.

OCPP
----

* Reservation changes for OCPP 2.0.1, see
  `PR 943 <https://github.com/EVerest/everest-core/pull/943>`_.
* Support for multiple temperature readings in OCPP 1.6, see
  `PR 986 <https://github.com/EVerest/everest-core/pull/986>`_.

Further fixes and enhancements, see
`Release 2024.11.0 overview on GitHub <https://github.com/EVerest/everest-core/releases/tag/2024.11.0>`_.

2024.10.0
=========

General changes
---------------

* Beta release of ISO 15118-20, see
  `PR 638 <https://github.com/EVerest/everest-core/pull/638>`_.
* Multiplexer for ISO communication to decide which protocol version to use
  for communication. See
  `PR 776 <https://github.com/EVerest/everest-core/pull/776>`_.
* Supporting multiple server certificate chains. See
  `PR 919 <https://github.com/EVerest/everest-core/pull/919>`_ and
  `PR 923 <https://github.com/EVerest/everest-core/pull/923>`_.

Hardware
--------

* Micro Mega Watt Charger: Ramp up voltage slowly to reduce overshoot, see
  `PR 886 <https://github.com/EVerest/everest-core/pull/886>`_.

Also many further fixes and enhancements - see
`release 2024.10.0 overview on GitHub <https://github.com/EVerest/everest-core/releases/tag/2024.10.0>`_.

2024.9.1
========

Using libocpp version 0.18.1 (instead of 0.18.0). See
`release libocpp 0.18.1 <https://github.com/EVerest/libocpp/releases/tag/v0.18.1>`_.

2024.9.0
========

General changes
---------------

* Introducing a C++ replacement for the SLAC Simulator (former JS), see
  `PR 859 <https://github.com/EVerest/everest-core/pull/859>`_.
* Extending 1ph/3ph switching, see
  `PR 807 <https://github.com/EVerest/everest-core/pull/807>`_.
* Lock connector in CP state B defined by config, see
  `PR 865 <https://github.com/EVerest/everest-core/pull/865>`_.
* Interface for providing acces to generic errors, see
  `PR 842 <https://github.com/EVerest/everest-core/pull/842>`_.
* Bump cmake version to 3.16.
* Bugfix of legacy wakeup for PWM charging, see
  `PR 845 <https://github.com/EVerest/everest-core/pull/845>`_.
* Bugfix regarding TLS server certificate hash calculation, see
  `PR 902 <https://github.com/EVerest/everest-core/pull/902>`_.
* Further bug fixes and minor enhancements, see
  `release 2024.9.0 notes on GitHub <https://github.com/EVerest/everest-core/releases/tag/2024.9.0>`_.

OCPP
----

* Configurable unit regarding composite schedules, see
  `PR 914 <https://github.com/EVerest/everest-core/pull/914>`_.
* OCPP 2.0.1 Smart Charging integration, see
  `PR 854 <https://github.com/EVerest/everest-core/pull/854>`_.
* OCPP 2.0.1: Bugfix regarding energy_Wh_import_signed meter value, see
  `PR 840 <https://github.com/EVerest/everest-core/pull/840>`_.

Hardware
--------

* phyVERSO: Implemented request_stop_transaction via external push button, see
  `PR 791 <https://github.com/EVerest/everest-core/pull/791>`_.
* Added config option for refresh rate regarding power meters, see
  `PR 864 <https://github.com/EVerest/everest-core/pull/864>`_.

2024.8.0
========

OCPP
----

* Optimized clarification of generated code segments in header files.
  See `PR 741 <https://github.com/EVerest/libocpp/pull/741>`_ and
  `PR 825 <https://github.com/EVerest/everest-core/pull/825>`_.
* Changes due to integration of California pricing requirements, see
  `PR 768 <https://github.com/EVerest/everest-core/pull/768>`_.
* Support of connector_id's other than 1, see
  `PR 836 <https://github.com/EVerest/everest-core/pull/836>`_.

Other topics
------------

* Bugfix regrading legacy wakeup in EvseManager, see
  `PR 823 <https://github.com/EVerest/everest-core/pull/823>`_.
* Added error handling documentation to EvseManager and OCPP modules.
* Further bugfixes and minor enhancements, see
  `release 2024.8.0 overview on GitHub <https://github.com/EVerest/everest-core/releases/tag/2024.8.0>`_.

2024.7.1
========

* Bugfix regarding initialization of signed meter and TransactionFinished
  event. See `PR 820 <https://github.com/EVerest/everest-core/pull/820>`_.
* Bugfix regarding charging phase changing in powersupply_set_DC method.
  See `PR 821 <https://github.com/EVerest/everest-core/pull/821>`_.

2024.7.0
========

OCPP
----

* The device model initialization is now implemented in C++ instead of Python.
  This means, that it is possible to add more EVSEs and Connectors now.
  Also, the device model is initialized at runtime now and it is using the
  database migration support. This solves
  `issue 656 <https://github.com/EVerest/libocpp/issues/656>`_.
  Details can be found in
  `PR 681 <https://github.com/EVerest/libocpp/pull/681>`_.
* The websocket connection state gets published. (OCPP 2.0.1)
* Security events have been implemented over the generic OCPP interface.
  (OCPP 2.0.1)

OpenSSL server
--------------

To support TLS certificate status requests, an OpenSSL server has been added.
For more info, see
`PR 677 <https://github.com/EVerest/everest-core/pull/677>`_.

Hardware support
----------------

Improvements for the phyVERSO MCU:

* Keep alive message are sent from EVerest to the hardware,
* configuration handling has been harmonized with standard config handling in
  EVerest
* different motor lock styles for different charging ports are possible now
  and
* implementation of error handling has been added.

For more information, see
`PR 771 <https://github.com/EVerest/everest-core/pull/771>`_.

Additionally, some changes regarding the evse_board_support like removing a
command for getting hardware capabilities (which should be done via async
publishing instead). More information:
`PR 769 <https://github.com/EVerest/everest-core/pull/769>`_

Further changes
---------------

* A new C++ based EvManager has been added (was Javascript up to now).
  This is the C++ based car simulator in EVerest. For details, see
  `PR 643 <https://github.com/EVerest/everest-core/pull/643>`_.
* Bugfix: Stop & unplug did not work after ISO pause & resume in SIL.
* OpenSSL support has been added to the libiso15118 library.
* ev-cli needs not be installed manually anymore as this is done during the
  cmake process now.
* The output of compile warnings has been enabled by default.

2024.6.0
========

Security Fix: Prevent integer overflow in EvseV2G
-------------------------------------------------

Reading the **v2gtp** message could potentially lead to an integer overflow.
This fix has been backported to the previous stable version of EVerest
(2024.3.1).

See the corresponding security advisory with CVE ID
`CVE-2024-37310 <https://github.com/EVerest/everest-core/security/advisories/GHSA-8g9q-7qr9-vc96>`_
.

New EXI implementation and license improvement
----------------------------------------------

The EvseV2G module is now using libcbv2g as EXI implementation instead of
OpenV2G's implementation. This means, that the EVerest project has been freed
from commercial-unfriendly licenses as the LGPLv3 is not part of EVerest
anymore with this change.

The libcbv2g has been released in the 0.2.0 version with improved cmake
integration, refactoring of unit tests and a simple CI in it.

Version information displayed
-----------------------------

EVerest manager displays version information at startup including also the
version of everest-core.

A flag can be set at startup that allows also displaying the version numbers
of other modules used in your EVerest build.

Further mentions of changes
---------------------------

Further improvements regarding Bazel builds have been done (reading of
dependencies.yaml on the fly).

Ubuntu 20.04 is not supported anymore.

CableCheck as been adapted to IEC-23 (2023).

2024.5.0
========

Cloud: Refactored database exception handling
---------------------------------------------

Implemented an alternative way to exception handling to prevent from crashes
in some cases, so that the system can continue working.

OCPP 1.6: ChargeX - MREC
------------------------

The Minimum Required Error Codes (MREC) have been refactored. Further
development will follow in next releases.

New BSP driver: Phytec phyVERSO
-------------------------------

Newly integrated driver in EVerest as open-source:
`Phytec phyVERSO as part of open-source EVerest <https://github.com/EVerest/everest-core/pull/648>`_
.

Bazel improvement regarding git tags
------------------------------------

When parsing the dependencies file, it is checked whether there are tags or
commit hashes.

For implementation details, see:
`Bazel: choose tags or commit <https://github.com/EVerest/everest-core/pull/654>`_

Minor EvseManager changes
-------------------------

More logging and some bug-fixing in EvseManager has been done.

In EvseV2G, DIN 70121 is enabled by default. Given reason: Strive for a max of
compatibility.

2024.4.0
========

Added charging schedules definition
-----------------------------------

Introducing a new OCPP type in EVerest to process charging schedules.

In short, a charging schedule defines a dedicated connector which a schedule
shall be related to, a charging rate unit and a charging schedule period.

For details, see
`the corresponding PR <https://github.com/EVerest/everest-core/pull/582>`_.

Note that this is an API-breaking change.

Rust now feature-complete
-------------------------

The feature-completeness of Rust in EVerest is meant in relation to the C++
implementation. Some recent features prepared the way for that status:

Having multislot support and a proper integration of Rust testing now in
EVerest, the integration of Rust can be seen as thorough as C++.

Payment terminal integration
----------------------------

Regarding payment integration, there is now added support for ZVT protocol
based devices. In EVerest, you can have "Eichrecht"-compliant payment on
charging points on board.

See a great overview of bank card payments here:
`Integration of bank card payment in EVerest <https://everest.github.io/nightly/general/06_handling_bank_cards.html>`_
.

Admin Panel improvements
------------------------

The EVerest Admin Panel has been part of EVerest for quite some time now. It
was time to improve the handling and stability of that.

Please be aware that it is still a beta-stage frontend tool which can be used
for managing EVerest instances, do some nice fast experiments with module
configurations and comes also as a hosted version now without the need of
setting up the whole environment on your end for a first step.

For more information and use-cases, see
`the EVerest Admin Panel repository <https://github.com/EVerest/everest-admin-panel>`_
.

Support starting transaction in EvseManager
-------------------------------------------

Powermeters should trigger a transaction to start prior to a charging session.
To optimize the communication between powermeters and EVerest's EvseManager
implementation,
`this change has been introduced <https://github.com/EVerest/everest-core/pull/573>`_
.

OCPP 2.0.1: Various custom data extensions
------------------------------------------

Some custom data extensions have been introduced. To get an overview, see the
adjusted files in
`this pull request <https://github.com/EVerest/everest-core/pull/605>`_
.

Bazel support for building essential modules
--------------------------------------------

Bazel support in EVerest. See the corresponding
`Bazel in EVerest documentation <https://github.com/EVerest/EVerest/pull/162>`_
.

JsEvManager as replacement for JsCarSimulator
---------------------------------------------

The new module that replaces the JsCarSimulator is still a JavaScript based EV
simulator, but can be run on "real" hardware. This JsEvManager is the
counterpart of the EvseManager to be able to simulate charging sessions.

A C++ implementation will be coming soon.

2024.3.1
========

Security Fix: Prevent integer overflow in EvseV2G
-------------------------------------------------

Reading the **v2gtp** message could potentially lead to an integer overflow.

See the corresponding security advisory with CVE ID
`CVE-2024-37310 <https://github.com/EVerest/everest-core/security/advisories/GHSA-8g9q-7qr9-vc96>`_
.

2024.3.0
========

Plug & Charge
-------------

The full process around a Plug&Charge session has been implemented, which
involves the communication to an electric vehicle and to systems in the cloud.

This means that the implementation has been done in ISO 15118 and OCPP code
parts of EVerest. For an overview and configuration instructions, see
`Plug and Charge Configuration in EVerest <https://everest.github.io/nightly/general/07_configure_plug_and_charge.html>`_
.

Fix for YetiDriver
------------------

The YetiDriver has been fully ported to the new BSP interface. For additional
information and changes, see
`the corresponding PR <https://github.com/EVerest/everest-core/pull/595>`_.

Extended OCPP interface for transaction state and ID
--------------------------------------------

The OCPP-related information of TransactionEvents are published as part of the
`OCPP interface <https://everest.github.io/nightly/_generated/interfaces/ocpp.html>`_.
Also the transaction ID received from a CSMS is now published.

See the
`pull request about the added topics <https://github.com/EVerest/everest-core/pull/569>`_
for more information.

Removed deprecated modules and dependencies
-------------------------------------------

EVerest had a folder with deprecated modules in it. To keep EVerest clean and
prevent it from having not needed dependencies, those modules have been removed
now.

See
`the corresponding PR <https://github.com/EVerest/everest-core/pull/604/files>`_
for an overview which those were exactly.
