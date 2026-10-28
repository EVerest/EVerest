.. _everest_modules_handwritten_over_voltage_monitor_API:

.. *******************************************
.. over_voltage_monitor_API
.. *******************************************

The complete API specification can be found in the

``docs/source/reference/EVerest_API/over_voltage_monitor_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/over_voltage_monitor_API/index.html>`_ automatically generated from it.

``tests/async_api_tests/test_over_voltage_monitor_api.py`` exercises the module in the DC
software-in-the-loop against this specification: a fake OVM implements the ``m2e`` side while
EVerest runs charging sessions with the limits, start, stop and reset sequence, the backup
watchdog on the published voltage, device-reported error and emergency shutdowns, the
plausibility check, device errors and communication loss.
