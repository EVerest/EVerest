.. _howto-firmware-update-connector-availability:

#######################################################
Control Connector Availability During a Firmware Update
#######################################################

This guide shows how an update mechanism decides whether a firmware update takes the connectors out of service,
how it can defer that decision until the downloaded package has been inspected, and how an update is installed
while the charging station has no connection to the CSMS. It is written for anyone integrating an update mechanism
with EVerest through the ``system_API`` module, as an API client of the
`EVerest System API <../reference/api/system_API/index.html>`_.

Without any of this, every firmware update delivered over OCPP makes the OCPP module disable all connectors, wait
for running transactions to finish, and only then tell the update mechanism that the install may proceed. That is
the right behaviour for a system image that reboots the station. It is unnecessary for a package that only replaces
configuration, images or other content, and operators who ship both kinds want to choose per update.

How the pieces fit together
===========================

A firmware update is a conversation between three parties, with the ``system_API`` module connecting the OCPP
module to the update mechanism over MQTT. Its topics are below ``everest_api/1/system/<module_id>/``, where
``<module_id>`` is the module's id in the EVerest configuration.

- **The CSMS** sends ``UpdateFirmware`` (or ``SignedUpdateFirmware`` in OCPP 1.6 with the Security Whitepaper).
- **The OCPP module** (``OCPP``, ``OCPP201`` or ``OCPPmulti``) forwards the request to the API client as an
  ``e2m/update_firmware`` request, sends each reported status to the CSMS as a ``FirmwareStatusNotification``, and
  manages connector availability for the install.
- **The API client** downloads and installs the package. It reports progress by publishing to
  ``m2e/firmware_update_status`` and waits for ``e2m/allow_firmware_installation`` before it installs anything that
  needs the connectors out of service.

Each ``m2e/firmware_update_status`` message carries the status itself, the request id (``-1`` for an unsigned OCPP
1.6 update) and an optional ``firmware_update_metadata`` object with one field:

.. code-block:: json

   {
     "firmware_update_status": "Downloaded",
     "request_id": -1,
     "firmware_update_metadata": {"disable_connectors_during_install": false}
   }

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
sends ``allow_firmware_installation`` to the API client once no connector is available any more. That message is
sent once per update cycle, and only for an install that disables the connectors: connectors that become
unavailable for another reason earlier in the cycle, during the download for example, do not trigger it. The
connectors are restored to their stored availability when the cycle ends with ``Installed``,
``InstallationFailed``, ``InstallVerificationFailed``, ``InvalidSignature``, ``DownloadFailed`` or ``Idle``. A
status after ``Idle`` starts a new cycle, and so does a newly accepted update request (in OCPP 1.6 the
``Downloading`` status of an unsigned update); connectors that an update which never reported its end left
unavailable are restored at that point. A change the CSMS scheduled with ``ChangeAvailability`` for a connector
that is still charging is queued alongside the one for the update: it applies when the transaction ends, whether
or not the update cycle has ended by then.

Connector availability never depends on the CSMS connection. The connectors are disabled and
``allow_firmware_installation`` is sent as soon as the status arrives, whether or not the BootNotification has
been accepted. Only the ``FirmwareStatusNotification`` message to the CSMS waits for registration; the latest
status is sent once the CSMS accepts the station.

Keep the connectors available for an update
===========================================

When the API client already knows that a package does not need the connectors out of service, it reports the
status before the install with the flag set to ``false``.

Unsigned update (OCPP 1.6 without the Security Whitepaper):

.. code-block:: json

   {
     "firmware_update_status": "Downloaded",
     "request_id": -1,
     "firmware_update_metadata": {"disable_connectors_during_install": false}
   }

Signed update (OCPP 1.6 Security Whitepaper, OCPP 2.0.1 and 2.1):

.. code-block:: json

   {
     "firmware_update_status": "SignatureVerified",
     "request_id": 42,
     "firmware_update_metadata": {"disable_connectors_during_install": false}
   }

The OCPP module forwards the status to the CSMS and leaves the connectors as they are. The API client does not
receive ``allow_firmware_installation`` in this case and continues with ``Installing`` and ``Installed`` on its
own.

Decide after inspecting the package
===================================

Often the API client only knows what kind of package it has once it has downloaded it and read its manifest. The
cycle then looks like this:

1. Report ``Downloaded`` or ``SignatureVerified`` with the flag set to ``false``. The connectors stay available
   while the package is inspected.
2. Inspect the package. If it can be installed without taking the connectors out of service, continue with
   ``Installing``; the rest of this list does not apply.
3. Otherwise report ``InstallScheduled`` with the flag set to ``true``:

   .. code-block:: json

      {
        "firmware_update_status": "InstallScheduled",
        "request_id": 42,
        "firmware_update_metadata": {"disable_connectors_during_install": true}
      }

   The OCPP module disables the idle connectors at once and queues the change for connectors with a running
   transaction.

4. Wait for ``e2m/allow_firmware_installation``. It arrives as soon as every connector is unavailable,
   which may be immediately or after the last transaction has finished.
5. Install the package and report ``Installing`` and ``Installed`` as usual.

An ``InstallScheduled`` status without the flag, or with the flag set to ``false``, changes nothing. That keeps
existing update mechanisms that report ``InstallScheduled`` for a timed install behaving as before.

Repeating ``InstallScheduled`` with the flag does not send ``allow_firmware_installation`` a second time within
the same cycle. A new cycle starts when the CSMS sends the next ``UpdateFirmware`` request that the API client
accepts.

Install while the station is offline
====================================

Nothing extra is needed. Because connector availability and ``allow_firmware_installation`` do not wait for the
CSMS, an API client that has the package already (for example from a local USB stick, or because the connection
was lost after the download) can report the statuses above and will be told to install even when the station is
unregistered. The ``FirmwareStatusNotification`` for the latest status is held by the OCPP module and sent once
the CSMS accepts the BootNotification. A session that ends while the station is offline counts as ended for the
install, although the CSMS has not confirmed the stop of its transaction yet.

For an update the CSMS did not request, OCPP 2.x cannot tell from a request whether the package is signed. The
status right before the install is then ``Downloaded`` when the status is reported with request id ``-1`` and
``SignatureVerified`` for any other request id. Set the flag explicitly to decide otherwise.
