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

Aggregating multiple power meters
=================================

Power meters in the energy tree publish independently of each other and of the optimizer
cycle, so at any instant the last reading of each meter has a different age. Summing them
naively mixes a fresh value with values from several seconds ago and yields a site total
that never actually existed on the installation.

The EnergyManager therefore sums only those readings whose own measurement timestamp lies
within ``power_meter_aggregation_window_s`` of the optimizer's start time; older readings
are excluded as stale rather than contributing a wrong value. Only EVSE nodes contribute,
so no meter is ever counted together with meters it already measures. Power and per phase
current are aggregated together.

Size the window at or above the publish interval of the slowest meter in the tree. A
window shorter than that discards readings the meter has had no chance to refresh, and
the aggregate keeps reporting fewer contributing meters than the installation has.
Setting it to ``0`` disables the filter and always sums the last reading of every meter.

**When a value is unknown it is reported as absent, never as zero.** If no meter has a
fresh reading, the aggregate carries no total at all -- a consumer must read that as
"unknown" and keep distributing on the static limits, never as "no power is flowing". The
same holds per phase: a phase is summed only when every contributing meter reports it, so
one single phase meter leaves the site L2 and L3 sums absent instead of understating them.

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

.. list-table::
   :header-rows: 1

   * - Config option
     - Default
     - Description
   * - ``power_meter_aggregation_window_s``
     - ``5``
     - Validity window for a power meter reading when aggregating multiple meters [s].
       Set it at or above the publish interval of the slowest meter. ``0`` disables the
       staleness filter.
