.. _everest_modules_handwritten_TelemetryExample:

Overview
========

TelemetryExample shows how a module publishes telemetry declared in its manifest. It declares one element
of every kind and publishes simulated values every ``publish_interval_ms``:

- gauges ``temperature`` (number with unit) and ``supply_voltage_raw`` (integer),
- counters ``plug_ins`` and ``energy_delivered``,
- states ``firmware_state`` (inline enum), ``charge_mode`` (enum referenced from ``types/``),
  ``relay_closed`` (boolean) and ``firmware_version`` (string, published once in ``init()``),
- the event ``cp_event`` (object referenced from ``types/``).

ev-cli generates one typed handle per element in ``tel::Elements``; the module uses them through its
``tel`` member:

.. code-block:: cpp

    tel.temperature.set(41.2);
    tel.plug_ins.increase();
    tel.firmware_state.set(tel::types::FirmwareState::Measuring);
    tel.cp_event.publish({types::board_support_common::Event::B});

Telemetry is only sent when ``settings.telemetry_socket_enabled`` is set and a telemetry receiver such as
TelemetryRouter is active; otherwise the handles do nothing. See ``config/config-sil-telemetry.yaml`` and
``lib/everest/framework/docs/Telemetry.md``.
