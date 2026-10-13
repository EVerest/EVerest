.. _project-roadmap:

###############
EVerest Roadmap
###############

The EVerest roadmap is under constant flux.
It is not a fixed plan and not a delivery commitment.
It is a snapshot of what the community currently intends to work on, and it
moves as contributors, priorities and standards move.

.. note::

   The roadmap on this page reflects the state that was aligned in the
   community rounds of December 2025 and presented in the TSC meeting on
   22 January 2026.
   For anything more recent than that, please talk to the working groups.
   See :ref:`roadmap-between-rounds` below.


How the roadmap is aligned
==========================

One to two times a year the EVerest community runs a formal, project wide
alignment process.
At least one of these rounds is scheduled so that its result feeds into the
annual EVerest project review by LF Energy, which typically takes place in Q1.

A full alignment round looks roughly like this:

* Community members and member organisations submit their input, either
  directly into the shared roadmap document, by email to the
  `EVerest mailing list <https://lists.lfenergy.org/g/everest>`_, or via their
  working group.
* Every working group reviews and discusses the collected input in its regular
  call, merges overlapping ideas and sets priorities.
* The Technical Steering Committee (TSC) consolidates the working group
  feedback into one roadmap.
* The consolidated roadmap is presented and discussed in the annual EVerest
  review of the LF Energy EV Charging Special Interest Group.

Anyone can take part in these rounds.
You do not have to be a TSC member, a maintainer or an LF Energy member to
contribute input.


.. _roadmap-between-rounds:

Between the rounds: the working groups
======================================

Between the formal rounds, the working groups keep aligning on the next steps.
That means the working groups are always closer to the current state than this
page can be.

If you are interested in the latest and greatest, this is where to go:

* **Join a working group call.**
  All dial-in links and the full meeting schedule are in the
  `official EVerest event calendar <https://zoom-lfx.platform.linuxfoundation.org/meetings/everest?view=month>`_.
  If you are not sure which group fits, the weekly General EVerest Welcome
  Call is a good entry point.
* **Ask in Zulip.**
  Most day to day technical discussion happens in
  `EVerest Zulip <https://lfenergy.zulipchat.com/>`_, including per topic
  channels for the working groups.
* **Ask on the mailing list.**
  If email is your preferred channel, use the
  `EVerest mailing list <https://lists.lfenergy.org/g/everest>`_ or subscribe
  to the `EVerest announcement list <https://lists.lfenergy.org/g/everest-announce>`_.

See :doc:`community` for the current list of working groups, their scope and
their chairs.


Planning something bigger? Please align first
=============================================

We highly recommend aligning with the community if you are missing something
bigger in EVerest, or if you are wondering whether you should implement it at
all.

An early conversation in the relevant working group usually saves a lot of
time, because it will tell you whether:

* somebody is already working on the same thing,
* the feature is already on the roadmap for one of the next releases,
* the design you have in mind fits the EVerest architecture and interfaces, or
* there are other parties interested in co-funding or co-maintaining the work.

Contributions that were discussed with the community upfront get reviewed and
merged considerably faster.
See :doc:`contributing` for the process itself.


Roadmap 2026
============

The items below were aligned in the December 2025 working group rounds and
presented in the TSC meeting on 22 January 2026.
See :doc:`governance/tsc/meetings/2026-01-22` for the meeting notes and the
`recording of the roadmap section of that TSC meeting <https://youtu.be/-sKbGNbXBTk?t=925>`_
for the discussion.

All items depend on community priorities and on who actually picks them up.


Cloud Communication and OCPP
----------------------------

Available today:

* OCPP 1.6
* OCPP 2.0.1
* OCPP 2.1 Core

Planned for H1/2026:

* Unified storage for the different OCPP versions
* Runtime switching between OCPP versions
* OCPP 2.1 Bidirectional Power Transfer (BPT) support
* OCPP 2.1 V2X frequency control
* OCPP 2.1 energy management system support
* ISO 15118 Smart Charging

Planned for H2/2026:

* OCPP 2.1 V2X DER control
* Ad hoc payments
* Cost calculation
* Pre-paid card
* Event streams
* Resume transactions
* Priority charging

For the current implementation status of the OCPP stacks, see
:ref:`roadmap-ocpp-status` below.


Car Communication and ISO 15118
-------------------------------

Available today:

* ISO 15118-2, AC and DC
* ISO 15118-20, AC and DC
* MCS high level communication

Planned for Q1/2026:

* ISO 15118-20 AC V2X amendment
* V2X DC frequency control
* MCS low level communication
* ISO 15118-2 certificate update
* ISO 15118-2 smart charging

Planned for Q2/2026:

* ISO 15118-20 Plug and Charge and DER
* ISO 15118-2 in libiso15118
* GB/T
* libiso15118 EV side simulator
* ISO 15118-202 ESDP


Energy Management
-----------------

Available today:

* Basic load balancing
* Flexible and expandable energy tree architecture
* Integration into energy management systems

Planned for H1/2026:

* Continuous bidirectional integration of OCPP 2.1 and ISO 15118-20
* EEBus (eebus-go) integration
* JSON RPC API module
* SunSpec implementation
* Dynamic local load balancing extensions

Planned for H2/2026:

* Advanced energy management, including solar integration and dynamic pricing
* Matter EV charging profiles
* OpenADR 3.0 integration via openleadr-rs
* IEEE 2030.5
* S2


.. _roadmap-ocpp-status:

Current (August 2026) OCPP implementation status
==================================

The OCPP support in EVerest moves faster than the yearly roadmap rounds, so
this section is kept separately.

OCPP 1.6J
---------

All feature profiles are implemented:

* Core
* Firmware Management
* Local Auth List Management
* Reservation
* Smart Charging
* Remote Trigger

The following whitepapers are supported as well:

* OCPP 1.6 Security Whitepaper, 3rd edition
* Using ISO 15118 Plug and Charge with OCPP 1.6
* OCPP and California Pricing Requirements

OCPP 2.0.1
----------

All functional blocks are implemented: Security, Provisioning, Authorization,
Local Authorization List Management, Transactions, Remote Control,
Availability, Reservation, Tariff and Cost, Meter Values, Smart Charging,
Firmware Management, ISO 15118 Certificate Management, Diagnostics, Display
Message and Data Transfer.

Known gaps:

* Smart Charging: K11 to K17 are not complete, some of them are in progress.
* Diagnostics: known limitations around the use of ActiveMonitoringBase.

The EVerest OCPP 2.0.1 stack is used inside several OCA certified products.

OCPP 2.1
--------

The OCPP 2.1 Core is available in EVerest.
Everything supported in OCPP 2.0.1 is also supported in OCPP 2.1, and most of
the new smart charging requirements are implemented.

Current development focus (all close to be done):

* Combining the OCPP 1.6 and OCPP 2.x storage backends
* New functional block: Bidirectional Power Transfer
* New functional block: DER Control

Next up:

* Extending the Authorization functional block
* The remaining functional blocks

----

**Authors**: Marco Möller
