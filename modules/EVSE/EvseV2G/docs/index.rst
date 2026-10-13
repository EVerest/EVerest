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

DC charging stop
================

On a DC stop request (DIN 70121 PowerDeliveryReq with ReadyToChargeState false, ISO 15118-2
PowerDeliveryReq with ChargeProgress Stop), EvseV2G publishes ``dc_open_contactor`` and waits for
the ``dc_output_disabled`` command before it sends the PowerDeliveryRes, at most 1.4 s on DIN 70121
(the EV gives up after 2 s) and 3 s on ISO 15118-2 (4.5 s performance time). Without the
confirmation it sends the response after the timeout and logs a warning.
