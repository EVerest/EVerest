.. _everest_modules_handwritten_EvseV2G:

.. *******************************************
.. EvseV2G
.. *******************************************

This module includes a DIN70121 and ISO15118-2 implementation provided by chargebyte GmbH

.. warning::

   **Deprecated** in 2026.10.0, earliest removal in 2027.04.0. Use
   :ref:`Evse15118D20 <everest_modules_Evse15118D20>` instead, which runs DIN SPEC
   70121, ISO 15118-2 and ISO 15118-20 on the C++ ISO 15118 stack. See the how-to
   guide :ref:`Migrate from EvseV2G and IsoMux to Evse15118D20
   <howto-iso15118-stack-migration>` for the config mapping and the behavioral
   differences. The module logs a deprecation warning at startup.

Feature List
============

This document contains feature lists for DIN70121 and ISO15118-2 features, which EvseV2G supports.
These lists serve as a quick overview of which features are supported.

DIN70121
--------

===============  ==================
Feature          Supported
===============  ==================
DC               ✔️
ExternalPayment  ✔️
===============  ==================

ISO15118-2
----------

=======================  ==================
Feature                  Supported
=======================  ==================
AC                       ✔️
DC                       ✔️
TCP & TLS 1.2            ✔️
ExternalPayment          ✔️
Plug&Charge              ✔️
CertificateInstallation  ✔️
CertificateUpdate        
Pause/Resume             ✔️
Schedule Renegotation    
Smart Charging           
Internet Service         
=======================  ==================

Welding detection
=================

On a DC stop request (DIN 70121 PowerDeliveryReq with ReadyToChargeState false, ISO 15118-2
PowerDeliveryReq with ChargeProgress Stop), EvseV2G publishes ``dc_open_contactor`` and holds the
PowerDeliveryRes until ``contactor_closed(false)`` reports the contactor open, so that side B is
disabled before the EV learns of it (IEC 61851-23:2023 Table CC.9, t103 before t104): at most 1.4 s
on DIN 70121 (1.5 s performance time) and 3 s on ISO 15118-2 (4.5 s), then it sends the response
anyway and logs a warning. The wait ends early on an emergency shutdown, an HLC stop or a terminated
connection.

The same stop publishes ``welding_detection`` ``Started``; ``Finished`` follows on SessionStop or
connection teardown, also when the EV sends no WeldingDetectionReq. Every WeldingDetectionRes waits
for two ``update_dc_present_values`` with ``welding_detection`` set after its request, for at most
1.4 s, so it reports the voltage source EvseManager selected for welding detection, measured after
the request (CC.6.3, CC.7.6.17, Table CC.9 t105 to t106): the first value after the request can still
be a measurement taken just before it, the second was taken after it. With one value at the deadline
the response carries it (info), with none the last present voltage (warning).
