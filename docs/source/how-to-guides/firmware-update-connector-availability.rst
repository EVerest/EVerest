.. _howto-firmware-update-connector-availability:

#######################################################
Control Connector Availability During a Firmware Update
#######################################################

This guide shows how a system implementation decides whether a firmware update takes the connectors out of
service, how it can defer that decision until the downloaded package has been inspected, and how an update is
installed while the charging station has no connection to the CSMS. It is written for anyone implementing the
``system`` interface (``interfaces/system.yaml``), either by extending the ``System`` module or by replacing it with
an implementation of their own.

Without any of this, every firmware update delivered over OCPP makes the OCPP module disable all connectors, wait
for running transactions to finish, and only then tell the system that the install may proceed. That is the right
behaviour for a system image that reboots the station. It is unnecessary for a package that only replaces
configuration, images or other content, and operators who ship both kinds want to choose per update.

How the pieces fit together
===========================

A firmware update is a conversation between three parties over the ``system`` interface:

- **The CSMS** sends ``UpdateFirmware`` (or ``SignedUpdateFirmware`` in OCPP 1.6 with the Security Whitepaper).
- **The OCPP module** (``OCPP``, ``OCPP201`` or ``OCPPmulti``) forwards the request through the ``update_firmware``
  command, publishes each reported status to the CSMS as a ``FirmwareStatusNotification``, and manages connector
  availability for the install.
- **The system implementation** downloads and installs the package. It reports progress by publishing the
  ``firmware_update_status`` variable and waits for the ``allow_firmware_installation`` command before it installs
  anything that needs the connectors out of service.

Each published ``firmware_update_status`` carries the status itself, the request id (``-1`` for an unsigned OCPP
1.6 update) and an optional ``firmware_update_metadata`` object with one field:

.. code-block:: yaml

   firmware_update_status: Downloaded
   request_id: -1
   firmware_update_metadata:
     disable_connectors_during_install: false

The OCPP module reads ``disable_connectors_during_install`` at two points of the cycle. The first is the status
right before the install, which is ``Downloaded`` for an unsigned update and ``SignatureVerified`` for a signed
one. The second is ``InstallScheduled``.

.. list-table::
   :header-rows: 1
   :widths: 24 19 19 38

   * - Status
     - Flag unset
     - Flag ``true``
     - Flag ``false``
   * - ``Downloaded`` (unsigned) or ``SignatureVerified`` (signed)
     - Connectors are disabled
     - Connectors are disabled
     - Connectors stay available
   * - ``InstallScheduled``
     - No change
     - Connectors are disabled
     - No change
   * - Any other status
     - No change
     - No change
     - No change

"Connectors are disabled" means the OCPP module makes every connector without a running transaction unavailable,
queues the same change for connectors that are still charging so it applies once their transaction ends, and
calls ``allow_firmware_installation`` on the system once no connector is available any more. That call is made
once per update cycle. The connectors are restored to their stored availability when the cycle ends with
``Installed``, ``InstallationFailed``, ``InstallVerificationFailed``, ``InvalidSignature``, ``DownloadFailed`` or
``Idle``. A status after ``Idle`` starts a new cycle, and so does a newly accepted update request (in OCPP 1.6
the ``Downloading`` status of an unsigned update); connectors that an update which never reported its end left
unavailable are restored at that point. A change the CSMS scheduled with ``ChangeAvailability`` for a connector
that is still charging is queued alongside the one for the update: it applies when the transaction ends, whether
or not the update cycle has ended by then.

Connector availability never depends on the CSMS connection. The connectors are disabled and
``allow_firmware_installation`` is called as soon as the status arrives, whether or not the BootNotification has
been accepted. Only the ``FirmwareStatusNotification`` message to the CSMS waits for registration; the latest
status is sent once the CSMS accepts the station.

Keep the connectors available for an update
===========================================

When the system already knows that a package does not need the connectors out of service, it reports the status
before the install with the flag set to ``false``.

Unsigned update (OCPP 1.6 without the Security Whitepaper):

.. code-block:: yaml

   firmware_update_status: Downloaded
   request_id: -1
   firmware_update_metadata:
     disable_connectors_during_install: false

Signed update (OCPP 1.6 Security Whitepaper, OCPP 2.0.1 and 2.1):

.. code-block:: yaml

   firmware_update_status: SignatureVerified
   request_id: 42
   firmware_update_metadata:
     disable_connectors_during_install: false

The OCPP module forwards the status to the CSMS and leaves the connectors as they are. The system does not receive
``allow_firmware_installation`` in this case and continues with ``Installing`` and ``Installed`` on its own. Set
the ``System`` module's ``ResetAfterUpdate`` configuration key to ``false`` if such an update must not reboot the
station either.

Decide after inspecting the package
===================================

Often the system only knows what kind of package it has once it has downloaded it and read its manifest. The
cycle then looks like this:

1. Report ``Downloaded`` or ``SignatureVerified`` with the flag set to ``false``. The connectors stay available
   while the package is inspected.
2. Inspect the package. If it can be installed without taking the connectors out of service, continue with
   ``Installing``; the rest of this list does not apply.
3. Otherwise report ``InstallScheduled`` with the flag set to ``true``:

   .. code-block:: yaml

      firmware_update_status: InstallScheduled
      request_id: 42
      firmware_update_metadata:
        disable_connectors_during_install: true

   The OCPP module disables the idle connectors at once and queues the change for connectors with a running
   transaction.

4. Wait for the ``allow_firmware_installation`` command. It arrives as soon as every connector is unavailable,
   which may be immediately or after the last transaction has finished.
5. Install the package and report ``Installing`` and ``Installed`` as usual.

An ``InstallScheduled`` status without the flag, or with the flag set to ``false``, changes nothing. That keeps
existing systems that report ``InstallScheduled`` for a timed install behaving as before.

Repeating ``InstallScheduled`` with the flag does not call ``allow_firmware_installation`` a second time within
the same cycle. A new cycle starts when the CSMS sends the next ``UpdateFirmware`` request that the system
accepts.

Install while the station is offline
====================================

Nothing extra is needed. Because connector availability and ``allow_firmware_installation`` do not wait for the
CSMS, a system that has the package already (for example from a local USB stick, or because the connection was
lost after the download) can report the statuses above and will be told to install even when the station is
unregistered. The ``FirmwareStatusNotification`` for the latest status is held by the OCPP module and sent once
the CSMS accepts the BootNotification. A session that ends while the station is offline counts as ended for the
install, although the CSMS has not confirmed the stop of its transaction yet.

For an update the CSMS did not request, OCPP 2.x cannot tell from a request whether the package is signed. The
status right before the install is then ``Downloaded`` when the status is reported with request id ``-1`` and
``SignatureVerified`` for any other request id. Set the flag explicitly to decide otherwise.

Set the flag with the System module
===================================

The ``System`` module shipped with EVerest reads the flag from the update itself, so a deployment can use it
without changing any code:

- **Unsigned update:** append the flag as a fragment to the firmware location the CSMS sends, for example
  ``https://updates.example.com/images.pnx#disable_connectors_during_install=false``. The module strips the
  fragment before downloading and attaches the flag to every status of that cycle.
- **Signed update:** put the flag in the package's metadata block. The module runs
  ``signed_firmware_metadata_parser.sh`` on the downloaded file, which prints the lines between
  ``===== BEGIN METADATA =====`` and ``===== END METADATA =====``, one ``key=value`` per line:

  .. code-block:: text

     ===== BEGIN METADATA =====
     disable_connectors_during_install=false
     ===== END METADATA =====

  The parsed flag is attached to the ``SignatureVerified`` status.

The ``System`` module reports ``InstallScheduled`` only for a signed update whose ``installDateTime`` lies in the
future, without the flag, and it does not wait for ``allow_firmware_installation``: its installer scripts are
placeholders for the real update mechanism. A system that wants the "decide after inspecting the package" cycle
implements the ``system`` interface itself, as described in :ref:`exp_linux_yocto_ota_updates`, and
publishes the statuses listed above.

Verify the behaviour
====================

The OCPP integration tests drive these cycles through a probe module and check the connector commands and the
``allow_firmware_installation`` call:

.. code-block:: bash

   . build/venv/bin/activate
   ./tests/run-tests.sh ocpp16 -k firmware
   ./tests/run-tests.sh ocpp201 -k firmware

In the libocpp unit tests, ``ChargePointFirmwareStatusTest`` (OCPP 1.6) and ``FirmwareUpdateDeferredDownloadTest``
(OCPP 2.x) cover the flag, the ``InstallScheduled`` opt-in and the offline case:

.. code-block:: bash

   cd build && ctest -R libocpp_ --output-on-failure
