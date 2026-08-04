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

Measurement based boosting
==========================

With ``use_power_meter_tracking`` enabled, each connector is capped at its measured
consumption plus ``power_meter_tracking_margin_W``. That converges the allocation down
towards actual use, but on its own it can only ever shrink an allocation. Boosting adds
the opposite direction: when the installation as a whole has unused capacity, the
tracking limit of every connector is widened so a vehicle that wants more power can get
it.

Two different comparisons drive the two behaviours:

* **Boosting** compares the aggregated leaf measurement against the *grid connection's*
  own import limit. When the spare capacity there exceeds ``boost_threshold_W`` for
  ``boost_hysteresis_cycles`` consecutive optimizer cycles, the tracking limit is widened
  by ``boost_step_A``. The offset accumulates while the headroom persists and is released
  again one step per cycle once it disappears.

* **``power_can_be_reduced``** compares the previous cycle's total *allocation* against
  the aggregated measurement. When the allocation exceeds consumption by more than
  ``boost_threshold_W``, the flag is published as true, so an external entity can see that
  capacity could be released without curtailing any active session.

Boosting is deliberately *not* driven by the difference between allocation and
measurement. Once tracking is active that difference settles at roughly the number of
charging connectors multiplied by the margin: it never reaches the threshold on a small
site, and on a larger one it self-reinforces, since boosting widens the very gap that
triggered it. The grid limit is bounded by real capacity and cannot ratchet.

Safety properties:

* **Boosting can never exceed the static limits.** The boost only widens the *tracking*
  limit, which is applied with the same "lower only" rule as every other limit, so
  configured fuse limits and external limits remain a hard ceiling. The offset is
  additionally bounded by the grid connection's own ampere limit.
* **The starting current is never boosted.** The first cycle of a session always requests
  ``power_meter_tracking_initial_current_A`` exactly, since that is an explicit
  configuration choice about how conservatively a session begins.
* **A stale aggregate causes no action.** If no power meter reading is fresh enough, the
  boost offset is held where it is and ``power_can_be_reduced`` is published as false
  rather than acting on data that may no longer reflect reality.
* **Hysteresis prevents oscillation.** Headroom must persist for several consecutive
  cycles before the limit widens, but is released promptly when it disappears --
  deliberately asymmetric, since over-allocating is the more dangerous direction.

Setting ``boost_step_A`` to ``0`` disables boosting while keeping measurement tracking
and the ``power_can_be_reduced`` flag.

Sizing ``boost_threshold_W`` for the reducibility flag
-----------------------------------------------------

With measurement tracking active, each connector's allocation sits about
``power_meter_tracking_margin_W`` above what its vehicle actually draws, so across a
cluster the total allocation exceeds total consumption by roughly
``connector_count x margin``. ``boost_threshold_W`` must be set above that product or
``power_can_be_reduced`` saturates to permanently true and stops carrying information.
The 500 W default suits one or two connectors at the default 200 W margin; a six
connector cluster wants 1500 W or more. Boosting itself is unaffected -- it compares
against the grid limit, which does not scale with connector count.

.. list-table::
   :header-rows: 1

   * - Config option
     - Default
     - Description
   * - ``boost_threshold_W``
     - ``500.0``
     - Sensitivity of both boosting and the ``power_can_be_reduced`` flag [W].
   * - ``boost_step_A``
     - ``2.0``
     - Widening of the tracking limit per boost step [A per phase]. ``0`` disables boosting.
   * - ``boost_hysteresis_cycles``
     - ``3``
     - Consecutive cycles the grid headroom must persist before the limit is widened.
