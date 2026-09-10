.. _everest_modules_handwritten_Example:

..  This file is a placeholder for optional multiple files
    handwritten documentation for the Example module.
    
..  This handwritten documentation is optional. In case
    you do not want to write it, you can delete the doc/ directory.

..  The documentation can be written in reStructuredText,
    and will be converted to HTML and PDF by Sphinx.
    This index.rst file is the entry point for the module documentation.

..  Use underlined-only headlines inside this document (highest-level
    sub-section headline should use "=" characters)

..  The content of this file will be included in the auto-generated HTML
    page for the module. You can link to it using the following
    reference: everest_modules_Example.

.. *******************************************
.. Example
.. *******************************************

Simple example module written in C++

OpenTelemetry prototype
=======================

This module doubles as a prototype for exporting `OpenTelemetry
<https://opentelemetry.io>`_ signals from an EVerest module. The code is split
the way a framework integration would be:

* ``Example::init_opentelemetry()`` installs the SDK for the module process: an
  OTLP/HTTP exporter for traces and metrics plus a resource that tags every
  signal with ``service.name`` (the module type), ``service.instance.id`` (the
  module id) and ``service.namespace=everest``.
  ``Example::shutdown_opentelemetry()`` flushes it on shutdown.
* ``exampleImpl`` only uses the OpenTelemetry API: a server span around every
  handled ``uses_something`` command with client spans for the ``kvs`` calls it
  makes, and the counter ``everest.example.commands``.

The exporters are configured through the standard environment variables, most
importantly ``OTEL_EXPORTER_OTLP_ENDPOINT`` (default ``http://localhost:4318``)
and ``OTEL_METRIC_EXPORT_INTERVAL`` (milliseconds, default ``60000``).

To see it in action, start a collector that prints what it receives and run
the demo environment from the everest-core root:

.. code-block:: bash

    docker run --rm -p 4318:4318 \
        -v $PWD/modules/Examples/CppExamples/Example/otel-collector.yaml:/etc/otelcol/config.yaml \
        otel/opentelemetry-collector
    OTEL_METRIC_EXPORT_INTERVAL=5000 bazel run //modules/Examples/CppExamples/Example:otel_env
