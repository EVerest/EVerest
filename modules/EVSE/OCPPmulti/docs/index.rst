.. _everest_modules_handwritten_OCPPmulti:

..  This file is a placeholder for optional multiple files
    handwritten documentation for the OCPPmulti module.
    
..  This handwritten documentation is optional. In case
    you do not want to write it, you can delete the doc/ directory.

..  The documentation can be written in reStructuredText,
    and will be converted to HTML and PDF by Sphinx.
    This index.rst file is the entry point for the module documentation.

..  Use underlined-only headlines inside this document (highest-level
    sub-section headline should use "=" characters)

..  The content of this file will be included in the auto-generated HTML
    page for the module. You can link to it using the following
    reference: everest_modules_OCPPmulti.

.. *******************************************
.. OCPPmulti
.. *******************************************

A OCPP charge point / charging station module, (supporting 1.6, 2.0.1, 2.1)

Requires: grid_support
^^^^^^^^^^^^^^^^^^^^^^^

**Interface**: :ref:`grid_support <everest_interfaces_grid_support>`

This optional requirement (0-128) connects the module to per-EVSE DER devices (inverter / grid-support hardware
abstraction) implementing the grid_support interface, exposing the OCPP 2.1 DER / grid-code functional block (block R).
DER is an OCPP 2.x-only feature: when the active protocol is OCPP 1.6, any wired grid_support connection is inert.
One connection represents one EVSE and is routed by its framework ``mapping``: the connection whose mapping names an
EVSE serves that EVSE. A connection without a mapping is a configuration error and is excluded from routing.

The module maintains a per-EVSE snapshot of the DER directives currently applied by the CSMS and pushes it to that
EVSE's connection through the **set_active_directives** command. When libocpp applies, schedules, clears, supersedes, or
expires a DER control, the snapshot is rebuilt and re-sent for every registered EVSE on its own connection.

At startup the module pre-provisions an ``ACDERCtrlr`` or ``DCDERCtrlr`` device-model component (chosen by the EVSE's
energy-transfer modes) for every DER-capable EVSE, so no static device-model JSON is required for the DER controllers.
Any EVSE without a wired grid_support connection has its DER controller forced to ``Available="false"`` (preserving a
CSMS-written ``"false"`` and its source), so the CSMS does not see DER as available after the wiring is removed.

The device declares its inverter capability through the ``capability`` variable. The module stores the capability, writes
its config variables (``ModesSupported`` and the DC nameplate values) through the device model, and republishes the
current active directives filtered to the declared control types. If the device model rejects a capability re-report, the
module rolls back to the last accepted capability; an EVSE whose very first capability is rejected is unregistered.

The CSMS may write the ``Enabled`` variable (``ReadWrite``). Writing ``Enabled="false"`` makes the module push an empty
directive replacement set to that EVSE, so the device clears the EV's curves; writing ``Enabled="true"`` republishes the
filtered active set for that EVSE. A CSMS-written ``Enabled`` persists across reboots and is rehydrated at boot.

The device reports grid event faults through the ``alarm`` variable, forwarded to the CSMS as a **NotifyDERAlarm.req**.
Alarms raised before the backend has accepted a capability for any EVSE are buffered and delivered once the first
capability is accepted; if no capability is ever accepted, the buffered alarms are dropped. Capability and alarm updates
received before the charge point is initialized are queued and replayed once the charge point is ready.

In addition to enabling the DER device-model component, the module asserts DER availability to the matching EvseManager
via its **set_der_available** command, so that EvseManager can advertise the corresponding ISO 15118-20 DER energy
transfer modes. If the device model rejects the capability, DER availability is withdrawn instead.

The configuration parameter **grid_support_heartbeat_s** sets the interval (in seconds) at which the current active
directive set is re-sent for every registered EVSE. A value of ``0`` disables the heartbeat; the set is then sent only
when it changes.
