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
  run. Allocations are never modified by the observation. From the observations it
  infers where power could be redistributed and logs that (see below).

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

Power redistribution inference (log only)
=========================================

With the ``PowerRedistribution`` strategy the EnergyManager compares, after every
optimizer run, what each connector was allotted in the previous run with what its meter
now reports, and the aggregated site consumption with the import limit of the grid
connection. The result is logged; **no allocation is changed yet**. Acting on it is the
next stage of the power redistribution work.

Per connector, with allotted power ``A`` and measured power ``M``:

- ``M`` more than ``margin x A`` below ``A``: the connector is *under-consuming*. The
  allocation could shrink to ``M x (1 + margin)``, but never below the connector's
  minimum current, so a session is trimmed rather than starved.
- otherwise it consumes its allocation: *saturated* if its own static maximum leaves
  room, *at maximum* if not.
- without a previous allocation (first run of a session) or without a measurement no
  claim is made.

For the site, with grid limit ``G`` and fresh aggregate ``S`` (only when every meter is
fresh, see above): headroom ``G - S`` beyond ``margin x G`` can be handed to the saturated
connectors. The reported increase is ``gain x (headroom - margin x G)``, split equally and
clamped to each connector's static maximum, so the step is large far from the grid limit
and vanishes close to it.

Both conditions must hold continuously for ``power_redistribution_hold_time_s`` before
they are reported, which filters transients such as an EV ramping up. A report is logged
once at info level when the condition becomes held (``power can be reduced by ... W``,
``power can be increased by ... W``) and once more when it clears. With ``debug`` on,
every run additionally prints the site headroom and the classification of every
connector.

.. list-table::
   :header-rows: 1

   * - Config option
     - Default
     - Description
   * - ``power_redistribution_margin``
     - ``0.1``
     - Relative deadband, as a fraction of the allocation (per connector) or of the grid
       limit (site). Gaps inside it are treated as consuming the allocation.
   * - ``power_redistribution_gain``
     - ``0.5``
     - Fraction of the headroom beyond the deadband that is reported as increase.
       ``0`` disables the increase report.
   * - ``power_redistribution_hold_time_s``
     - ``10``
     - Time a condition must hold before it is reported [s]. ``0`` reports immediately.
