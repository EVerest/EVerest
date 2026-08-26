.. _everest_modules_handwritten_EnergyManager:

.. *************
.. EnergyManager
.. *************

This module implements logic to distribute power to energy nodes based on
energy requests.
One of its central ideas is to represent the energy system for which power is
distributed as an energy tree containing energy nodes.
This enables the representation of arbitrarily complex configurations of
physical and logical components within the targeted energy system.

Please see :doc:`Energy Management in EVerest </explanation/energymanagement/index>`
for a detailed explanation of the concepts behind this module.

Broker strategy and power meter observation
===========================================

The ``broker_strategy`` option selects the broker that trades energy on behalf of each
EVSE:

- ``FastCharging`` (default): allocate as much as the limits allow.
- ``PowerRedistribution``: currently trades identically to ``FastCharging`` and
  additionally observes the connector's live power meter reading once per optimizer
  run. Allocations are never modified by the observation.

The measurement is taken from the EVSE's own power meter, reported through the
``energy_usage_leaves`` field of the energy flow request (with ``energy_usage_root``
as fallback), so the observation is per connector. The last observed value is retained
per connector for the duration of the session and reset on unplug. A connector in an
active charging session that reports no measurement is warned about once per session;
connectors that are ``Unplugged`` or ``Finished`` are not observed.
