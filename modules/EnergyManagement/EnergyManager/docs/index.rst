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
are excluded as stale rather than contributing a wrong value. The grid connection's own
meter is used wherever there is one; otherwise the EVSE meters are summed, and then only
the EVSE nodes contribute, so no meter is ever counted together with meters it already
measures. Power and per phase current are aggregated together.

Size the window at or above the publish interval of the slowest meter in the tree. A
window shorter than that discards readings the meter has had no chance to refresh, and
the aggregate keeps reporting fewer contributing meters than the installation has. The
filter cannot be switched off: it is the only guard between a meter that has stopped
updating and a limit computed from its last reading, so the smallest window is ``1``.

**When a value is unknown it is reported as absent, never as zero.** If no meter has a
fresh reading, the aggregate carries no total at all -- a consumer must read that as
"unknown" and keep distributing on the static limits, never as "no power is flowing". The
same holds per phase: a phase is summed only when every contributing meter reports it, so
one single phase meter leaves the site L2 and L3 sums absent instead of understating them.

.. list-table::
   :header-rows: 1

   * - Config option
     - Default
     - Description
   * - ``power_meter_aggregation_window_s``
     - ``5``
     - Validity window for a power meter reading, both for the site aggregate and for the
       per connector measurement [s]. Set it at or above the publish interval of the
       slowest meter. Minimum ``1``: a slow meter needs a larger window, not no window.

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
- ``redistribution_reduction_hold_s``: how long a condition has to stay pending before it
  is acted on - a reduction before the limit is lowered, and a reduce or increase condition
  of the site inference before it is reported or granted. Increases of the measurement
  based limit always apply immediately; the hold is what keeps a briefly dipping EV (or a
  freshly started one with the upper start value) from being cut before it had a chance to
  draw. ``0`` acts on the first run that sees the condition.

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
and ``power_meter_aggregation_window_s`` is judged against it - the module has one
staleness rule, so a meter is never alive for the connector limit and stale for the site
aggregate. This is what lets the
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

Power redistribution inference
==============================

With the ``PowerRedistribution`` strategy the EnergyManager compares, after every
optimizer run, what each connector was allotted in the previous run with what its meter
now reports, and the aggregated site consumption with the import limit of the grid
connection.

The reduce side is reported only: lowering a connector already happens continuously
through the measurement based limit above, which needs no inference to do it. The increase
side is handed out - see `Handing out the site headroom`_.

Per connector, with allotted power ``A`` and measured power ``M``:

- ``M`` more than the deadband below ``A``: the connector is *under-consuming*. This is
  reported, not acted on: the measurement based limit above already holds the connector at
  what it draws plus the margin.

  The deadband is ``power_redistribution_connector_margin x A``, but never less than
  ``redistribution_margin_A`` on the connector's phases. A connector that follows its limit
  is allotted precisely its own measurement plus that margin, so a gap of no more than the
  margin is the limit's own doing and says nothing about the EV. Without the floor, no
  connector drawing less than ``redistribution_margin_A`` divided by the fraction - 18 A at
  the defaults, whatever the phase count and voltage - could ever count as consuming its
  allocation, and the site would never hand it anything.
- otherwise it consumes its allocation: *saturated* if its own static maximum leaves
  room, *at maximum* if not.
- without a previous allocation (first run of a session), without a measurement, or with a
  measurement that is stale or negative, no claim is made. Negative is export, and this
  inference only looks at the import schedule: a discharging connector is not the same
  thing as one using none of its import allocation.

For the site, with grid limit ``G`` and the site measurement ``S`` (only when every
contributing meter is fresh, see above): headroom ``G - S`` beyond
``power_redistribution_site_margin x G`` is handed to the saturated connectors. The
increase is ``gain x (headroom - deadband)``, split equally and clamped to each
connector's static maximum, so the step is large far from the grid limit and vanishes close
to it. A saturated connector whose static maximum is unknown is not a candidate: there is
nothing to clamp its share against, and counting it would shrink the share of the
connectors that can actually use one.

Both conditions must hold continuously for ``redistribution_reduction_hold_s`` - the same
hold the measurement based limit waits out before it lowers a connector - before they are
reported. This hold is the **only** thing filtering an EV that is still ramping:
IEC 61851-1 allows a vehicle up to 5 s to follow a duty cycle change and real cars ramp over
longer, so every ramp looks like under-consumption until the hold expires. Set it above the
worst case ramp of the vehicles on site.

A line is logged at info level when a condition becomes held (``power can be reduced by
... W``, ``granting ... W of headroom to N of M saturated connector(s)``) and once more when
it clears. With ``debug`` on, every run additionally prints the site headroom, which meter
measured it, the classification of every connector and what it was granted.

Handing out the site headroom
-----------------------------

The increase is handed out: every saturated connector receives its share of the headroom on
top of its measurement based limit, so it climbs faster than one ``redistribution_margin_A``
per optimizer run while the grid connection has room to spare. Setting
``power_redistribution_gain`` to ``0`` hands out nothing and reduces the site inference to a
report - that is how the measurement based limit is kept without the site ever relaxing it.

Three things bound what a connector receives:

- only connectors classed as *saturated* receive a share. A connector that is not using
  what it already has is not short of an allocation.
- only a fresh measurement can carry a share. Without one the connector falls back to its
  minimum current plus the margin, as it does without a distributed share.
- the connector's own maximum, and after that the energy tree: the share relaxes the
  measurement based limit and never reaches past what the market would have allocated
  anyway, so the fuse limits still bound every connector. Whatever the site infers, the
  fuse decides.

The site figure itself is the grid connection's own meter wherever there is one. Where
there is not, it is the sum of the EVSE meters, which understates consumption by the whole
house load and therefore overstates the headroom. The grid limit of the energy tree is what
keeps that from mattering - it bounds every connector regardless - but it does mean the
connectors reach that limit faster than the meters can justify. The ``info`` log names
which of the two meters the figure came from.

A grant reaches the broker one optimizer run after it was inferred, because the inference
needs the enforced limits of the run it belongs to and those only exist once trading is
over. That delay is also what makes the loop settle: a grant acts on a measurement taken
before it was handed out, so applying it twice within one interval would count the same
headroom twice. In the run after a grant the connector is usually classed as
under-consuming again - it has been given room it has not yet taken up - and receives
nothing further until its measurement has caught up. ``redistribution_reduction_hold_s``
is what holds the raised limit in place while the EV ramps into it.

Where the limits and the measurements come from
-----------------------------------------------

Both are read from what the module already computed for the run, not re-derived:

- **Limits** come from the ``Market``'s import offer at the slot in force, which is the
  request schedule after it has been resampled onto the optimizer's timestamp grid, had the
  leaves side and root side limits merged, and had the conversion efficiency applied.
  Reading ``schedule_import[0].limits_to_root`` instead would skip all three. On the sites
  this feature exists for that is not a detail: an external limit (an OCPP charging profile,
  any DLM input) is exactly what produces a multi-slot schedule and a one-sided limit, and
  each difference overstates the limit.

- **The site measurement** is the grid connection's own power meter
  (``energy_usage_root`` on the root node) wherever there is one, falling back to the sum of
  the EVSE meters only when there is not. The sum of the EVSE meters is not a site
  measurement: it is a site measurement minus every load the energy tree does not know
  about, and on a connection shared with a building it understates consumption by exactly
  the house load. The ``debug`` log names which of the two was used.

- **Per connector measurements** are subject to the same freshness window as the site
  aggregate. A reading older than ``power_meter_aggregation_window_s`` classifies the
  connector as *unknown*, exactly as a missing one would. Without that check a meter that
  stopped publishing keeps offering power back forever, because ``EnergyNode`` and
  ``EvseManager`` republish the last reading they received in every request.

.. list-table::
   :header-rows: 1

   * - Config option
     - Default
     - Description
   * - ``power_redistribution_connector_margin``
     - ``0.1``
     - Relative deadband per connector, as a fraction of its allocation. A gap inside it
       counts as consuming the allocation.
   * - ``power_redistribution_site_margin``
     - ``0.1``
     - Reserve kept at the grid connection, as a fraction of the grid limit. Separate from
       the connector margin: at ``0.1`` on a 100 kW site this is a permanent 10 kW reserve,
       which is a different decision from a 10 % tolerance per session.
   * - ``power_redistribution_gain``
     - ``0.5``
     - Fraction of the headroom beyond the deadband handed to the saturated connectors.
       ``0`` hands out nothing and leaves the site inference a report.

Phase imbalance correction
==========================

A site fed by three phases may not load one of them much more than the others; grid
operators typically allow a difference of about 20 A. Single phase EVs make that easy to
violate: three of them on L2 and none on L1 is 48 A of difference before the building has
drawn anything. With ``phase_symmetry_enabled`` the EnergyManager keeps the difference
between the most and the least loaded phase under ``max_phase_imbalance_A``, driven by
measurements and in three steps per optimizer run:

1. **Aggregate per phase.** The site measurement of the run (see above) says how much each
   grid phase carries: the grid connection's own meter where there is one, else the sum of
   the EVSE meters.

2. **Detect the overshoot.** The least loaded phase is the reference. Any phase carrying
   more than ``max_phase_imbalance_A`` above it has an *overshoot* of that excess, and that
   is what has to go.

3. **Limit the involved connectors equally.** The overshoot of a phase is divided equally
   over the connectors *involved* on it, and each of them is limited to what it draws minus
   its share. Which phases a connector draws on is read from its own per phase measurement
   (current above 1 A), never guessed from a total.

Only a connector that draws on the overloaded phase **and not on the reference phase** is
involved. Lowering a connector lowers every phase it draws on by the same amount, so a
three phase EV cannot change the difference between two phases at all - limiting it would
cost charging power and correct nothing. That leaves single phase EVs, and two phase EVs
whose other phase is not the reference; a connector involved on two overloaded phases at
once is limited by the larger of its two shares, not their sum.

Two worked examples with a 20 A limit. Site at L1 66 A, L2 90 A, L3 66 A: L2 is 24 A above
the reference, 4 A too much. CP 02 and CP 05 are single phase on L2, so each is limited to
2 A below what it draws; CP 06 on L1 is left alone. Site at L1 90 A, L2 90 A, L3 66 A: both
L1 and L2 are 4 A too much. CP 02 and CP 05 on L1 and CP 06 and CP 09 on L2 each give up
2 A.

A limit is applied in the run that computed it, then **held** for ``phase_imbalance_hold_s``:
during the hold the connector is left out of the next decisions, so a reading that does
not yet reflect the limit cannot earn it a second share of the same overshoot. After the
hold, whatever overshoot remains in fresh measurements is cut again. A limit is
**released**, in full, once handing back everything it holds would predictably keep every
phase the connector draws on at least 1 A under ``max_phase_imbalance_A``; released too
early it would only re-trigger.

A connector is never limited below its minimum current. What cannot be corrected because
of that - or because no involved connector exists, as with three phase EVs only or with a
skew that sits in the building load behind the grid meter - is logged once as a *residual*
and not fought. The correction requires ``broker_strategy`` ``PowerRedistribution``: it is
the strategy that observes the per phase measurements and applies the limit as one more
upper bound on the connector's current, next to the measurement based cap. With
``FastCharging`` the option has no effect. As everywhere in this module an unknown phase
is absent, not zero: a phase the site meter does not report is neither reference nor
candidate, and with fewer than two known phases nothing is corrected.

.. list-table::
   :header-rows: 1

   * - Config option
     - Default
     - Description
   * - ``phase_symmetry_enabled``
     - ``false``
     - Switches the correction on. Requires ``broker_strategy`` ``PowerRedistribution``.
   * - ``max_phase_imbalance_A``
     - ``20.0``
     - Largest allowed difference between the most and the least loaded grid phase [A].
   * - ``phase_imbalance_hold_s``
     - ``10``
     - Seconds a limited connector is left alone before it is judged again, so the next
       decision sees a measurement that reflects the limit. Above the update interval of the
       slowest meter on the site.
