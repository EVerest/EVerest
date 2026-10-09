.. _everest_modules_handwritten_CharxPowerDS:

.. ############################
.. Phoenix Contact CHARX power DS
.. ############################

Native driver for the Phoenix Contact CHARX power 40 kW DS module (article 1740541). It talks CANopen to the module
over SocketCAN, without a PLC in between.

Status
======

Proof of concept, tested on one bench with one module (50-1000 V, 125 A, 40 kW). A full DC charging session with a
real vehicle ran through CableCheck, PreCharge and CurrentDemand at about 29 A / 380 V. Not tested with several
modules on one bus, nor in bidirectional (import) mode, which the driver does not implement.

Bus and CANopen
===============

- 500 kbit/s on ``device``, the module at ``node_id`` (DIP switch GROUP).
- The driver is the CANopen master. It sends NMT start and a master heartbeat (``0x700 + master_node_id``, every
  1 s). The module supervises that heartbeat (object ``0x1016:01``, factory setting node 1 / 10 s) and floods
  emergency messages without it.
- Only one master may run on the bus. Stop any PLC or other master that drives the module first.
- All access is by expedited SDO. The module answers within about 1 ms. Writes are sent without size indication
  (command ``0x22``): the module rejects size-indicated writes to its 1-byte objects with abort ``0x06070010``.

Objects used
------------

==================== ========== ===========================================
Object               Index      Use
==================== ========== ===========================================
Readiness            0x4000:01  1 = PowerOn, 0 = PowerOff
DS output            0x4000:0C  0 open, 1 close contactor A, 2 close B
Flags 1              0x4001:16  status and fault bits
DS output status     0x4001:1C  confirmed contactor state
Contactor error      0x4001:1D  read every 8th poll
I measured           0x4002:02  mA
I max available      0x4002:06  mA, capabilities
V min / max          0x4002:09  mV, capabilities (0x4002:0A for max)
V measured           0x4002:0C  mV
I setpoint           0x4004:02  mA
V setpoint           0x4004:06  mV
==================== ========== ===========================================

Structure
=========

- ``main/charx_canopen.*``: the CANopen master. Frame encoding and decoding are free functions; ``Canopen`` adds
  SocketCAN, the heartbeat thread and serialised SDO transfers. Once constructed, no call throws: a frame that
  cannot be sent (interface down, transmit queue full because no node acknowledges) is reported as an I/O error.
- ``main/charx_controller.*``: the logic - switch sequence, validity checks and error mapping - without EVerest
  or bus dependencies. It talks to the module through the ``SdoClient`` interface and reports through
  ``ControllerOutputs``; the clock is passed in.
- ``main/power_supply_DCImpl.*``: the EVerest glue. It runs the poll loop, maps the controller's outputs to the
  ``power_supply_DC`` variables and errors, and opens the CAN interface again when frames cannot be sent for a
  while (e.g. an adapter used directly that was replugged). Behind a virtual CAN link (``vxcan`` plus ``can-gw``
  routes, as in a container) sends always succeed: there a replugged adapter shows as ``CommunicationFault``
  until whoever set up the link sets it up again.
- ``tests/``: unit tests for the frame codec and for the controller against a simulated module.

Behavior
========

- **Capabilities** are read from the module once at start and capped by the ``max_export_*`` and
  ``min_export_voltage_V`` config options.
- **Polling** every ``poll_interval_ms``: flags, contactor status, voltage and current. ``voltage_current`` is
  published on every poll.
- **Switch on** follows the order the module requires: current setpoint 0, close the contactor, then PowerOn. The
  module refuses PowerOn (abort ``0x08000022``) until the contactor reports closed, about 0.8 s, so PowerOn is
  retried until accepted. The current setpoint is written last.
- **Switch off** sets the current to 0 and writes PowerOff first. The module's contactor is opened once a reading
  polled *after* PowerOff shows the current below ``off_current_threshold_A`` (after 3 s at the latest). The flags
  do not count here: they blink while the DC stages switch. ``setMode(Off)`` returns by the same rule - contactor
  open, or low current read after PowerOff - so the charger relays do not open under load either; at most after
  ``power_off_timeout_s``, and at once while ``CommunicationFault`` is active.
- **Reported mode** is ``Export`` only when the module confirms power stage up and contactor closed, otherwise
  ``Off``. Once in ``Export``, the ``DcOutputSideOff`` flag alone does not end it for up to 3 s: the module drops
  that flag for about 1.3 s while it switches its DC stages. Longer, the DC side counts as off.
- **A module that does not follow** - contactor open or power stage off after PowerOn was accepted, or the module
  still on after an Off - gets its commands again once the deviation has lasted 3 s, so a single odd poll never
  interrupts the current.
- **After a communication loss** NMT start is sent, and the setpoints (or the Off) are sent again before
  ``CommunicationFault`` clears. A module that kept running is not switched on again, so the current does not drop;
  one that lost its state (e.g. rebooted) gets the whole switch-on sequence in that same cycle.

Errors
======

======================================== =====================================================================
Error                                    Raised when
======================================== =====================================================================
``power_supply_DC/CommunicationFault``   the CAN interface cannot be opened, or 3 polls in a row get no answer
                                         (NMT start is sent then and every 20 failed polls)
``power_supply_DC/HardwareFault``        internal failure, converter error, short circuit, emergency stop,
                                         discharge problem, fan fault, module ID repetition, contactor error
``power_supply_DC/OverTemperature``      flag OTP
``power_supply_DC/UnderTemperature``     flag UTP
``power_supply_DC/UnderVoltageAC``       AC input under voltage or phase loss
``power_supply_DC/OverVoltageAC``        AC input over voltage
``power_supply_DC/OverVoltageDC``        DC output over voltage
``power_supply_DC/OverCurrentDC``        DC over current or over power
``power_supply_DC/OverCurrentAC``        AC overload
``power_supply_DC/VendorError``          switch-on or switch-off not confirmed within ``power_on_timeout_s`` /
                                         ``power_off_timeout_s``, or the module left Export on its own for 3 s
======================================== =====================================================================

Each error clears when its condition is gone.
