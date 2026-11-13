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
``firmware_update_metadata.disable_connectors_during_install``; the OCPP side is described in
:ref:`howto-firmware-update-connector-availability`. System reads the flag from the update itself:

- **Unsigned update:** append the flag as a fragment to the firmware location the CSMS sends, for example
  ``https://updates.example.com/images.pnx#disable_connectors_during_install=false``. System strips the
  fragment before downloading and attaches the flag to every status of that update.
- **Signed update:** put the flag in the package's metadata block. System runs
  ``signed_firmware_metadata_parser.sh`` on the downloaded file, which prints the lines between
  ``===== BEGIN METADATA =====`` and ``===== END METADATA =====``, one ``key=value`` per line:

  .. code-block:: text

     ===== BEGIN METADATA =====
     disable_connectors_during_install=false
     ===== END METADATA =====

  The parsed flag is attached to the ``SignatureVerified`` status.

System reports ``InstallScheduled`` only for a signed update whose ``installDateTime`` lies in the future,
without the flag, and it does not wait for ``allow_firmware_installation``: its installer scripts are
placeholders for a real update mechanism, so the module is meant for software-in-the-loop setups rather than
production.
