.. _everest_modules_handwritten_OtlpCollector:

OtlpCollector
=============

Prototype of an OpenTelemetry receiver for EVerest. It accepts metrics over
OTLP/HTTP (protobuf only, no gRPC, no JSON, no TLS) and mirrors every gauge and
sum data point into the OCPP device model through the ``ocpp`` interface, so a
CSMS can read the values and put OCPP 2.0.1 monitors on them.

Mapping
-------

The OTLP payload is parsed with the generated protobuf classes of the
``opentelemetry-proto`` Bazel module, the same ones opentelemetry-cpp uses for
its exporters. Each number data point becomes one ``set_variables`` request:

* component name: the ``component_name`` config value (default ``Telemetry``)
* component instance: the resource attribute ``service.instance.id``, which the
  Example module sets to its EVerest module id
* variable name: the metric name, e.g. ``everest.example.commands``
* variable instance: the data point attributes as ``key=value,key=value`` in key
  order, e.g. ``everest.cmd=uses_something``, absent when there are none
* value: the number as text, attribute type ``Actual``

Histograms, exponential histograms and summaries are skipped. The writes use
the module id as source, so OCPP201 lets them update ReadOnly variables; the
variables themselves have to exist in the device model, see
``component_config/Telemetry_example.json`` for the component matching the
Example module.

Demo
----

``config-otel.yaml`` runs the Example module against this collector without an
OCPP module, so the collector logs what it would write. Point the metrics
exporter at it and keep the traces going wherever they went before:

.. code-block:: bash

    OTEL_EXPORTER_OTLP_METRICS_ENDPOINT=http://localhost:4320/v1/metrics \
    OTEL_METRIC_EXPORT_INTERVAL=5000 \
    bazel run //modules/Misc/OtlpCollector:otel_env -- \
        --prefix . --config etc/everest/config-otel.yaml --graceful-shutdown

Anything but ``POST /v1/metrics`` is answered with 404, other content types
with 415, undecodable payloads with 400.
