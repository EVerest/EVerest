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

Only EVSE nodes contribute to the aggregate. An intermediate node's own meter measures
the sum of its children, so including it as well would double count. For each EVSE the
leaves side measurement is used, falling back to the root side measurement.

Notes on behaviour:

* The aggregate is rebuilt from scratch on every optimizer run, so a connector that
  disappears from the tree stops contributing immediately.
* A reading timestamped slightly in the future is treated as fresh -- small clock skew
  between a meter and the controller must not discard data.
* An unparsable timestamp is logged and the reading treated as stale, so a misbehaving
  meter cannot skew the sum. Detection relies on the parser returning the epoch rather
  than raising an error, and applies even when the staleness filter is disabled.
* Per phase sums are reported only when *every* contributing meter supplied per phase
  values, so the per phase figures always cover the same set of meters as the total.
* Setting the window to ``0`` disables the staleness filter entirely.

Power meter measurement tracking
================================

By default the EnergyManager distributes energy using the static limits reported by
the nodes of the energy tree (typically fuse limits from ``EnergyNode`` and the
EVSE's own capabilities). Setting ``use_power_meter_tracking`` to ``true`` switches
each connector to a broker that follows the live power meter readings instead.

On the first optimizer run of a charging session the broker requests
``power_meter_tracking_initial_current_A`` per phase. On every subsequent run it
limits its request to the measured power plus ``power_meter_tracking_margin_W``, so
the allocation converges to what the vehicle actually draws and the unused headroom
becomes available to other connectors.

The measurement is taken from the EVSE's own power meter, reported through the
``energy_usage_leaves`` field of the energy flow request (with ``energy_usage_root``
as fallback). Tracking is therefore per connector. If no measurement is available the
broker logs a warning and falls back to the static limits for that connector, so a
failed meter cannot starve a charging session.

Tracking is AC only: a node that declares no ``ac_max_current_A`` (typically a DC
charger) is left on its static limits, since an amps-to-watts budget has no meaning
there. The per-session state is only armed while a session is active — optimizer runs
during which the connector is ``Unplugged`` or ``Finished`` neither consume the
initial-current request nor produce warnings.

Three properties of the implementation are worth knowing when reading the code:

* **The limit is a budget for a whole optimizer run, not for a single trade.** The
  optimizer performs many trading rounds per run, each against a freshly built offer,
  so the broker computes its limit once in its constructor and then applies the
  remainder of that budget -- the limit minus what the connector already bought during
  the run -- on each round.
* **The limit never falls below the minimum current.** An EVSE cannot signal a duty
  cycle below ``ac_min_current_A``, and a lower limit would make the minimum current
  purchase fail and allocate nothing at all. Tracking releases surplus capacity; it
  must never stop a session.
* **The budget is priced the way the trading engine prices purchases** — at
  ``ac_max_phase_count`` phases. A measurement from a single phase session is scaled up
  accordingly, so a single phase vehicle drawing full power is not mistaken for a small
  load and tracked down to the minimum current.

The tracking limit is applied as an additional watt limit on top of the existing
limits -- it can only ever lower the allocation, never raise it above a static or
external limit.

.. list-table::
   :header-rows: 1

   * - Config option
     - Default
     - Description
   * - ``power_meter_aggregation_window_s``
     - ``5``
     - Validity window for a power meter reading when aggregating multiple meters [s].
       ``0`` disables the staleness filter.
   * - ``use_power_meter_tracking``
     - ``false``
     - Enable measurement tracking instead of pure static limit distribution.
   * - ``power_meter_tracking_initial_current_A``
     - ``16.0``
     - Current per phase requested on the first optimizer run of a session.
   * - ``power_meter_tracking_margin_W``
     - ``200.0``
     - Margin added to the measured power to form the tracking limit.
