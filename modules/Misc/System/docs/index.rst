.. _everest_modules_handwritten_System:

.. ******
.. System
.. ******

This module implements system wide operations.

Currently this includes the following commands:

-  Log Uploads
-  Firmware Updates
-  Setting of System time 

Corresponding variables signal the state of Log Uploads and Firmware Updates.

The bundled simulation installer stub treats firmware URLs whose basename ends in
``-bad.pnx`` as installation failures, which is useful for exercising failure handling.

Integration in EVerest
======================

This module provides implementation for the system interface. It does not require any other modules.
