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
preserve the boot reason across resets: ``FirmwareUpdate`` for both signed and
unsigned firmware updates, ``ScheduledReset`` for a ``reset`` with ``scheduled``
set (OCPP ``OnIdle``, after the transactions ended) and ``RemoteReset`` for any
other ``reset``. Without a store, the boot reason is always ``PowerUp``.
Post-boot ``Installed`` reporting applies only to signed firmware updates.
With ``ResetAfterUpdate`` enabled and a store connected, System publishes
``InstallRebooting`` before resetting and ``Installed`` with the original request
ID when the stack starts again. Without a store, it publishes only ``Installed``
before resetting. With ``ResetAfterUpdate`` disabled, it publishes ``Installed``
without resetting.

Whether an update takes the connectors out of service is controlled per update through
``firmware_update_metadata.disable_connectors_during_install``, read from the ``#`` fragment of an
unsigned update's location or from the metadata block of a signed package. See
:ref:`howto-firmware-update-connector-availability`.
