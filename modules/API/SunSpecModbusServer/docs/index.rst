.. _everest_modules_handwritten_SunSpecModbusServer:

.. *******************
.. SunSpecModbusServer
.. *******************

The module ``SunSpecModbusServer`` exposes the AC measurements of one EVSE as a
SunSpec device over Modbus TCP. An external energy management system (EMS) can
discover the charger with standard SunSpec discovery and read its power,
current, voltage, frequency, and energy.

The module requires one ``evse_manager`` connection and reads its
``powermeter`` variable, so it works with any charger driver that EvseManager
uses. The server is read-only: write requests are answered with the Modbus
exception ILLEGAL_FUNCTION.

Register map
============

The map starts at the SunSpec base address 40000 (the Modbus PDU address of
holding register 40001).

=======  ======  ================================================
Address  Length  Content
=======  ======  ================================================
40000    2       ``SunS`` marker
40002    68      Model 1, Common (manufacturer, model, serial, ...)
40070    155     Model 701, DER AC Measurement
40225    2       End marker (0xFFFF, 0)
=======  ======  ================================================

Model 701 points filled from the ``powermeter`` variable:

==================================  ==========================================
SunSpec point                       Source
==================================  ==========================================
``ACType``                          ``ac_type`` configuration
``W``, ``WL1`` ... ``WL3``          ``power_W`` total and per phase, negated
``A``                               Sum of ``current_A`` L1 to L3
``AL1`` ... ``AL3``                 ``current_A`` per phase
``LNV``                             Average of ``voltage_V`` L1 to L3
``VL1`` ... ``VL3``                 ``voltage_V`` per phase
``Hz``                              ``frequency_Hz`` L1
``TotWhAbs``, ``TotWhAbsL1`` ...    ``energy_Wh_import`` total and per phase
``TotWhInj``, ``TotWhInjL1`` ...    ``energy_Wh_export`` total and per phase
==================================  ==========================================

Every other point, and every point whose source value is missing, holds the
SunSpec "not implemented" value of its type. With ``ac_type`` set to
``single_phase`` only the L1 block is filled; ``split_phase`` fills L1 and L2.

Sign convention
---------------

SunSpec DER points count power flowing into the grid as positive. EVerest
counts power flowing into the vehicle as positive. The module therefore
negates power: a vehicle charging at 7.2 kW reads as ``W`` = -7200 W.
Energy imported by the vehicle is reported as absorbed (``TotWhAbs``), and
energy exported by the vehicle as injected (``TotWhInj``).

Scale factors
-------------

Scale factors are fixed at startup from the configured ratings, so a client
never combines a value with a scale factor from a different update:

* ``W_SF`` covers ``max_power_W``.
* ``A_SF`` covers ``max_current_A`` times the number of phases, because the
  total current shares it with the per-phase values.
* ``V_SF`` covers ``max_voltage_V``.
* ``Hz_SF`` is -2 and ``TotWh_SF`` is 0.

A value beyond the range of its scale factor is reported as the largest
representable value.

Network
=======

The server listens on ``listen_address`` and ``port``. The default address
127.0.0.1 accepts local clients only. Modbus TCP has no authentication, so
expose the server only on a network the EMS shares with the charger.

Port 502 is privileged; the module declares ``CAP_NET_BIND_SERVICE`` for it.
The server accepts up to four clients at a time and closes a connection that
sends no request for 60 seconds.

Example configuration
=====================

.. code-block:: yaml

  sunspec_modbus_server:
    module: SunSpecModbusServer
    config_module:
      listen_address: 0.0.0.0
      port: 502
      manufacturer: Example Inc
      model: AC Wallbox
      serial_number: "0001"
      ac_type: three_phase
      max_power_W: 22000
      max_current_A: 32
    connections:
      evse_manager:
        - module_id: evse_manager
          implementation_id: evse
