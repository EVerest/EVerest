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

Power meters in the energy tree publish independently of each other and of the
optimizer cycle, so at any instant the last reading of each meter has a different age.
Summing them naively mixes a fresh value with values from several seconds ago and
yields a total that never actually existed on the installation.

The EnergyManager therefore aggregates the leaf power meter readings through a windowed
filter. Each reading carries its own measurement timestamp; a reading is included in the
sum only if that timestamp lies within ``power_meter_aggregation_window_s`` of the
optimizer's start time. Older readings are counted as stale and excluded rather than
contributing a wrong value.

Only EVSE nodes contribute to the aggregate, and the tree walk does not descend below
an EVSE node. An intermediate node's own meter measures the sum of its children — and an
EVSE's meter covers everything downstream of it — so counting either together with its
descendants would double count. For each EVSE the leaves side measurement is used,
falling back to the root side measurement.

Notes on behaviour:

* The aggregate is rebuilt from scratch on every optimizer run, so a connector that
  disappears from the tree stops contributing immediately.
* A reading timestamped slightly in the future is treated as fresh -- small clock skew
  between a meter and the controller must not discard data.
* An unparsable timestamp is logged — once per meter, not once per optimizer cycle —
  and the reading treated as stale, so a misbehaving meter cannot skew the sum or flood
  the log. Detection relies on the parser returning the epoch rather than raising an
  error, and applies even when the staleness filter is disabled.
* Power and per phase current are aggregated together, in the same shape a single
  connector's measurement uses, so one consumer type covers a connector and the site.
* A per phase figure is reported only when *every* contributing meter supplied that
  phase, so a phase sum never silently covers fewer meters than the total. A phase a
  meter does not measure is reported as absent, never as zero -- a single phase meter
  therefore leaves the site L2 and L3 sums unreported.
* Setting the window to ``0`` disables the staleness filter entirely.

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
       ``0`` disables the staleness filter.
