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
   * - ``use_power_meter_tracking``
     - ``false``
     - Enable measurement tracking instead of pure static limit distribution.
   * - ``power_meter_tracking_initial_current_A``
     - ``16.0``
     - Current per phase requested on the first optimizer run of a session.
   * - ``power_meter_tracking_margin_W``
     - ``200.0``
     - Margin added to the measured power to form the tracking limit.
