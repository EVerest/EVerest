.. _everest_modules_handwritten_power_supply_DC_API:

.. *******************************************
.. power_supply_DC_API
.. *******************************************

The complete API specification can be found in the

``docs/source/reference/EVerest_API/power_supply_DC_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/power_supply_DC_API/index.html>`_ automatically generated from it.

``tests/async_api_tests/test_power_supply_dc_api.py`` exercises the module in the DC
software-in-the-loop against this specification: an external fake supply implements the
``m2e`` side while EVerest runs charging sessions, faults, derating and communication loss.
