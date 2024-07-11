.. release_notes:

.. _release_notes_main:

#####################
EVerest Release Notes
#####################

See also our detailed release information based on all PRs and source code
commits on GitHub:
`EVerest releases <https://github.com/EVerest/everest-core/releases>`_

EVerest releases are scheduled monthly. A release for a dedicated month can be
expected in the beginning of the following month. Each third release (March,
June, September and December) will be a stable release including a two week
feature freeze phase before the release date.

Here are the some high-level changes for the last releases (starting from the
most recent one backwards):

2024.6.0 - LATEST STABLE
========================

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
