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

Behavior
========

- **Capabilities** are read from the module once at start and capped by the ``max_export_*`` and
  ``min_export_voltage_V`` config options.
- **Polling** every ``poll_interval_ms``: flags, contactor status, voltage and current. ``voltage_current`` is
  published on every poll.
- **Switch on** follows the order the module requires: current setpoint 0, close the contactor, then PowerOn. The
  module refuses PowerOn (abort ``0x08000022``) until the contactor reports closed, about 0.8 s, so PowerOn is
  retried until accepted. The current setpoint is written last.
- **Switch off** writes PowerOff, opens the contactor and sets the current to 0. ``setMode(Off)`` returns once the
  module reports its power stage off or the current is below ``off_current_threshold_A``, at most after
  ``power_off_timeout_s``, so the charger relays do not open under load.
- **Reported mode** is ``Export`` only when the module confirms power on and contactor closed, otherwise ``Off``.
- **Commands the module does not follow** within 3 s are sent again.

Errors
======

======================================== =====================================================================
Error                                    Raised when
======================================== =====================================================================
``power_supply_DC/CommunicationFault``   the CAN interface cannot be opened, or 3 polls in a row get no answer
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
                                         ``power_off_timeout_s``
======================================== =====================================================================

Each error clears when its condition is gone.
