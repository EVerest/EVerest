.. _everest_modules_handwritten_EvseV2G:

.. *******************************************
.. EvseV2G
.. *******************************************

This module includes a DIN70121 and ISO15118-2 implementation provided by chargebyte GmbH

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
