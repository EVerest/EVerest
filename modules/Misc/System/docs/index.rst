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

Integration in EVerest
======================

This module provides implementation for the system interface. It does not require any other modules.

Connect the optional ``store`` requirement to a persistent key-value store to
preserve the boot reason for both signed and unsigned firmware updates across resets.
Post-boot ``Installed`` reporting applies only to signed firmware updates.
With ``ResetAfterUpdate`` enabled and a store connected, System publishes
``InstallRebooting`` before resetting and ``Installed`` with the original request
ID when the stack starts again. Without a store, it publishes only ``Installed``
before resetting. With ``ResetAfterUpdate`` disabled, it publishes ``Installed``
without resetting.
