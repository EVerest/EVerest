.. _everest_modules_handwritten_AcTemperatureDerating:

AcTemperatureDerating
=====================

Applies AC current limits to an :ref:`EnergyNode <everest_modules_EnergyNode>` based on
``temperature_sensor`` readings and configurable derating curves (linear interpolation between points).

Requirements
--------------

- At least one connected ``temperature_sensor`` provider (e.g. :ref:`CarloGavazzi_EM580 <everest_modules_CarloGavazzi_EM580>` or :ref:`YetiSimulator <everest_modules_YetiSimulator>`)
- One ``external_energy_limits`` consumer, typically a dedicated :ref:`EnergyNode <everest_modules_EnergyNode>` inserted in the energy tree

Energy tree placement
---------------------

Place a dedicated ``EnergyNode`` for temperature derating between the grid connection and downstream nodes.
The EnergyManager merges limits from all nodes; OCPP/API external limits on other nodes continue to work independently.

Configuration
-------------

``derating_curves_json`` maps ``module_id.identification`` to an array of ``{"temp_C", "max_current_A"}`` points.

- ``module_id``: temperature provider instance id from the Everest config (``active_modules`` / ``connections.temperature.module_id``)
- ``identification``: ``Temperature.identification`` published by that provider

Every connected temperature provider must have at least one curve. Providers publishing multiple
temperature values need one curve per ``identification``. The effective limit is the minimum across
all readings. Missing curves log a warning and use ``fallback_max_current_A``. Temperature readings
without ``identification`` are treated as misconfigured and also use ``fallback_max_current_A``.

``temperature_provider_ignore_list`` is a comma-separated list of ``module_id.identification`` entries
to exclude from derating. If a derating curve is configured for an ignored entry, the module fails
to start.

Runtime behaviour
-----------------

- **Stale readings:** After ``temperature_stale_timeout_ms`` without an update, a reading is treated
  as stale and ``fallback_max_current_A`` applies. Limits are re-evaluated periodically (at most half
  the stale timeout) so a provider that stops publishing still triggers fallback.
- **Limit decreases** (more derating, e.g. temperature rising) are published immediately.
- **Limit increases** (relaxing derating, e.g. temperature falling) are debounced by
  ``update_debounce_ms``; the periodic re-evaluation retries a debounced increase once the window
  has elapsed.

Example module config::

  ac_temp_derating:
    module: AcTemperatureDerating
    config_module:
      fallback_max_current_A: 0
      temperature_stale_timeout_ms: 10000
      update_debounce_ms: 1000
      derating_curves_json: |
        {"cgem580.Powermeter": [{"temp_C": 25, "max_current_A": 32}, {"temp_C": 70, "max_current_A": 10}]}
    connections:
      temperature:
        - module_id: cgem580
          implementation_id: temperature_sensor
      energy_node:
        - module_id: temp_derate_node
          implementation_id: external_limits

No derating (constant limit at all temperatures)::

  derating_curves_json: |
    {"yeti_driver_1.Powermeter": [{"temp_C": -20, "max_current_A": 32}, {"temp_C": 25, "max_current_A": 32}, {"temp_C": 80, "max_current_A": 32}]}

See also ``config/config-sil-ac-temp-derating.yaml`` for a full SIL example using
:ref:`TemperatureSensorSimulator <everest_modules_handwritten_TemperatureSensorSimulator>`.
