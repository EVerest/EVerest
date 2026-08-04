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

Broker strategy and power redistribution
========================================

The ``broker_strategy`` option selects the broker that trades energy on behalf of each
EVSE:

- ``FastCharging`` (default): allocate as much as the limits allow.
- ``PowerRedistribution``: trade with the ``FastCharging`` algorithm, but limit each
  connector to its measured current plus ``redistribution_margin_A``, so a connector
  that draws less than its allocation frees the unused budget for the other connectors
  on the same fuse. The margin is the headroom the current can rise by per
  ``update_interval`` (2 A per second at the defaults), so a connector whose budget
  frees up again ramps back at that rate.

The limit only ever lowers what ``FastCharging`` would allocate, never raises it: fuse
limits and the equal split between connectors remain entirely with the market. Because
every optimizer run re-trades all allocations from zero, a session joining a saturated
fuse receives its start allocation within one update interval and the running sessions
give up the difference in equal parts; as the newcomer's consumption rises, all
connectors converge on the equal share.

The limit applies per connector, only while the connector reports the ``Charging``
state (a session that measures zero because it is authorizing, preparing or paused is
not limited by that zero, and a resumed session starts over at its start value), only
to the schedule slot covering now (later slots are forecast and keep the full request),
and never below the connector's minimum current. Nodes that offer no AC current limit -
DC connectors requesting energy as a watt limit - are traded exactly like
``FastCharging`` trades them.

Configuration:

- ``redistribution_margin_A``: margin added on top of the measured current, and the
  per-interval rise rate.
- ``redistribution_start_with_lower_limit``: start value for a new charging session -
  ``true`` starts at the minimum current plus the margin and ramps up; ``false`` starts
  at the full allocation and tracks down once the reduction hold has elapsed.
- ``redistribution_reduction_hold_s``: how long a reduction has to stay pending before
  the limit is lowered. Increases always apply immediately; the hold is what keeps a
  briefly dipping EV (or a freshly started one with the upper start value) from being
  cut before it had a chance to draw. ``0`` follows the measurement down immediately.
- ``redistribution_measurement_max_age_s``: maximum age of a reading, judged by the
  reading's own timestamp, before it no longer carries the limit.

A connector without a usable, fresh measurement is limited to its minimum current plus
the margin rather than left uncapped - a dead meter must not hold an allocation open.
This includes connectors that have no power meter at all: with this strategy such a
connector charges pinned at its minimum plus the margin, and is warned about once per
session.

The measurement is taken from the EVSE's own power meter, reported through the
``energy_usage_leaves`` field of the energy flow request (with ``energy_usage_root``
as fallback), so both observation and limit are per connector. One reading is selected
per run and the power, the per-phase current and the timestamp all come from it: a node
whose two sides each report a different half of a measurement is read from one side
only, so no value is ever paired with another meter's phases or age. The last observed
value is retained per connector for the duration of the session and reset on unplug.

Each observation carries the reading's own measurement timestamp alongside its values,
and ``redistribution_measurement_max_age_s`` is judged against it. This is what lets the
broker tell a live reading from a frozen one: ``EnergyNode`` and ``EvseManager``
republish the last power meter reading they received in every energy flow request, so a
meter that stopped updating is indistinguishable from one holding steady unless the
reading's own timestamp is checked. A reading whose timestamp cannot be parsed is
treated like a missing one rather than a current one.

The limit is computed per phase - from the measured per-phase current, falling back to
per-phase power over the nominal voltage, then to the total power spread over the active
phases - and collapsed to the single ``ac_max_current_A`` the energy interface expresses
today by taking the highest of the known phases (the value applies to every phase, so
the lowest would starve the phase that legitimately draws most). Trading each phase
individually needs a per-phase limit in the energy types first.
