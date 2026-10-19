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

A DC PowerDelivery(Stop) publishes ``welding_detection`` ``Started``; ``Finished`` follows on
SessionStop or connection teardown, also when the EV sends no WeldingDetectionReq. The first
WeldingDetectionRes waits for an ``update_dc_present_values`` with ``welding_detection`` set, for
at most 1 s, so it reports the voltage source EvseManager selected for welding detection
(IEC 61851-23:2023 CC.7.6.17). Without such a value it reports the last present voltage and logs a
warning.
