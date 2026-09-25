.. _everest_modules_handwritten_TelemetryRouter:

Overview
========

TelemetryRouter receives the telemetry of all modules and forwards it to sinks according to an ordered list
of rules, similar to a firewall. Modules declare their telemetry in the ``telemetry`` section of their
manifest and publish it through generated handles (see ``lib/everest/framework/docs/Telemetry.md``).

Telemetry does not travel over MQTT. With ``settings.telemetry_socket_enabled``, the manager binds a Unix
datagram socket at ``settings.telemetry_socket_path`` before any module starts and hands it to this module,
the only active module with ``telemetry_receiver: true`` in its manifest. The configuration is rejected
otherwise. Modules send to the socket without blocking; datagrams sent before TelemetryRouter reads them are
queued by the kernel.

The kernel queues at most ``net.unix.max_dgram_qlen`` datagrams (often 10, 512 with systemd's defaults).
Telemetry beyond that is dropped and counted by the senders. Set it to 512 or more on production systems.

Element catalog
===============

At startup, TelemetryRouter reads the telemetry declarations of all active modules together with their
EVSE/connector mapping from the configuration. It logs a summary and validates the rules against it.

Processes without a manifest in the EVerest configuration can add elements at runtime by sending a
declaration to the telemetry socket before their samples:

.. code-block:: json

    {"t": "d", "module": "ext:meter", "module_type": "ExternalMeter",
     "elements": {"voltage": {"kind": "gauge", "type": "number", "unit": "V", "description": "Voltage"}}}

The element descriptors use the keys of the manifest telemetry section; enum values are listed inline.
Declarations using the id of an active EVerest module are rejected, a new declaration of the same id replaces
its elements, and ``max_dynamic_producers`` and ``max_dynamic_elements`` limit the catalog. Rule errors of
declared elements are logged, the affected routes are skipped.

Rules file
==========

The ``rules_file`` config key names a YAML file; relative paths are resolved against the module's share
directory, where ``telemetry_rules.yaml`` (drops everything) and ``telemetry_rules.example.yaml`` are
installed.

.. code-block:: yaml

    version: 1
    sinks:
      csms: {type: ocpp, flush_interval_ms: 1000, min_interval_s: 10, max_batch: 50, dead_retry_s: 600}
      debug: {type: log, max_per_second: 20}
    default_action: drop            # or {forward: [{sink: debug}]}
    rules:
      - name: silence-simulators
        match: {module_id: "*_simulator"}
        action: drop
      - name: meters
        match: {module_type: GenericPowermeter, element: [voltage, "temp*"], kind: gauge}
        forward:
          - {sink: csms, component: {name: Meter, instance: "${module_id}"}, variable: {name: "${element}"}}
          - {sink: debug}

- ``match`` compares ``module_id``, ``module_type`` and ``element`` with glob patterns (a string or a list)
  and ``kind`` with a kind or a list of kinds. Omitted fields match everything.
- Rules are evaluated in order. The first matching rule decides: ``action: drop`` drops the element,
  ``forward`` sends it to every listed target. A forwarding rule with ``continue: true`` lets later rules add
  further targets. When no rule decides, ``default_action`` applies.
- Target options may use ``${module_id}``, ``${module_type}``, ``${element}`` and ``${unit}``. Sink options
  may use ``${env:NAME}``.
- Rules are applied to every declared element at startup. Errors, such as forwarding an event to an OCPP
  sink, stop the module unless ``strict_rules`` is false. Rules that match no element are reported.

Sinks
=====

``log``
    Logs samples, at most ``max_per_second`` lines.

``ocpp``
    Writes gauges, counters and states to OCPP device model variables through the ``ocpp`` requirement,
    attribute Actual. Target options: ``component`` (``name``, ``instance``, ``evse``, ``connector``),
    ``variable`` (``name``, ``instance``), ``min_interval_s``, and for numeric elements ``deadband``,
    ``decimals`` and ``scale``. ``evse`` and ``connector`` default to ``auto``, the producer's mapping;
    ``none`` omits them and a number sets them.

    Only the latest value per variable is kept. A value equal to the last written one, or within the
    deadband, is not written again, and a variable is written at most once per ``min_interval_s``. Writes go
    through ``DeviceModel::set_value``, so variable monitors of a CSMS fire.

    The variables must exist in the device model. Provide a component config file, e.g. the installed
    ``Telemetry_example.json`` in ``component_config/custom`` of the OCPP module. Unknown variables are
    reported once and retried every ``dead_retry_s``. Without an ``ocpp`` connection the sink only logs what
    it would write.

``otel``
    Exports telemetry to an OpenTelemetry collector over OTLP/HTTP with protobuf encoding. Gauges and
    counters become observable gauges and cumulative counters named ``everest.<module_type>.<element>``
    (target option ``metric_name`` overrides it), exported every ``export_interval_ms`` with their latest value.
    States become log records on every change, events become log records with the JSON value as body and
    their top-level scalar fields as ``everest.event.<field>`` attributes (``flatten_event_fields``). With
    ``states_as_metrics``, boolean and enum states are also exported as gauges: 0/1 per value with the
    ``everest.state`` attribute. All data carries ``everest.module.id``, ``everest.module.type``,
    ``everest.element`` and, from the mapping, ``everest.evse.id`` and ``everest.connector.id``.

    .. code-block:: yaml

        backend:
          type: otel
          endpoint: http://collector:4318       # /v1/metrics and /v1/logs are appended
          headers: {Authorization: "Bearer ${env:OTEL_TOKEN}"}
          export_interval_ms: 30000
          resource: {service.instance.id: charger-4711}
          stale_after_s: 300                    # series without new values are no longer exported
          states_as_metrics: false

    The sink is only available if TelemetryRouter was built with opentelemetry-cpp (1.28 or newer, with the
    OTLP/HTTP exporters): CMake looks for an installed package with ``find_package(opentelemetry-cpp)``; set
    ``CMAKE_PREFIX_PATH`` if it is not installed system-wide. ``EVEREST_TELEMETRY_ROUTER_OTEL`` (``AUTO``,
    ``ON``, ``OFF``) controls whether the sink is built. A rules file using an ``otel`` sink fails to load in a
    build without it.

Statistics
==========

Every ``stats_log_interval_s``, TelemetryRouter logs the number of received datagrams, forwarded samples and
the reasons for dropped ones (no route, unknown producer or element, invalid value, undecodable,
truncated) since the last report.
