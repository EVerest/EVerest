.. _everest_modules_handwritten_isolation_monitor_API:

.. *******************************************
.. isolation_monitor_API
.. *******************************************

The complete API specification can be found in the

``docs/source/reference/EVerest_API/isolation_monitor_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/isolation_monitor_API/index.html>`_ automatically generated from it.

``tests/async_api_tests/test_isolation_monitor_api.py`` exercises the module in the DC
software-in-the-loop against this specification: a fake IMD implements the ``m2e`` side while
EVerest runs cable checks and charging sessions with passing and failing self tests, isolation
faults, voltage-to-earth faults, device errors and communication loss.
