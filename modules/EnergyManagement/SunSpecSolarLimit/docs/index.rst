.. _everest_modules_handwritten_SunSpecSolarLimit:

.. *****************
.. SunSpecSolarLimit
.. *****************

The module ``SunSpecSolarLimit`` limits charging power to the production of a
PV inverter, so that vehicles charge from solar power. It reads the inverter
over Modbus TCP using SunSpec and sends the resulting limit to an
``EnergyNode`` as an external total power limit (``total_power_W`` in
``limits_to_leaves``). EnergyManager converts the power limit to a current
from the node's voltage and phase configuration.

Charging limit
==============

The limit is the PV production minus ``reserve_W``, and never below 0 W. The
reserve keeps power back for the household's own consumption; it is a fixed
estimate, not a measurement of the actual load.

Before the first valid reading, and when no valid reading arrived for
``stale_timeout_s``, the limit is ``fallback_power_W``:

* The default of 0 W stops charging when solar data is missing, so the
  vehicle never charges from the grid because of a communication fault.
* A value at or above the site's charging capacity lets charging continue
  from the grid instead.

The limit is sent when it changes by at least 1 W, and at least every
10 seconds.

Inverter requirements
=====================

The module looks for the SunSpec marker at base address 40000, 0, and 50000
and walks the model chain. It reads the AC power output ``W`` and its scale
factor ``W_SF`` from:

* Model 701 (DER AC Measurement), when present, otherwise
* Model 101, 102, or 103 (single-phase, split-phase, or three-phase inverter).

Inverters that implement only the floating-point models 111 to 113 are not
supported. A reading is invalid when ``W`` or ``W_SF`` holds the SunSpec "not
implemented" value; invalid readings count as missing data.

The module reconnects and repeats discovery after any communication error.
A problem is logged once, and recovery is logged when readings resume.

Example configuration
=====================

.. code-block:: yaml

  solar_limit:
    module: SunSpecSolarLimit
    config_module:
      inverter_host: 192.168.1.50
      inverter_port: 502
      unit_id: 1
      reserve_W: 500
      fallback_power_W: 0
    connections:
      energy_node:
        - module_id: grid_connection_point
          implementation_id: external_limits
