.. _everest_modules_handwritten_power_supply_DC_API:

.. *******************************************
.. power_supply_DC_API
.. *******************************************

The complete API specification can be found in the

``docs/source/reference/EVerest_API/power_supply_DC_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/power_supply_DC_API/index.html>`_ automatically generated from it.

Setpoints below the minimum current
===================================

When the EV or an energy limit asks for less than the minimum current offered
to the EV, EVerest sends a current below ``min_export_current_A`` or
``min_import_current_A``, usually 0 A. This means: deliver no current (0 A,
+/- 0.15 A, IEC 61851-23:2023 CC.5.5.2). Do not raise it to the minimum. If the
hardware cannot regulate to 0 A, switch its output off, keep the mode, and switch
it on again when a current at or above the minimum is requested.
During cable check and precharge, EVerest never sends less than the minimum.

``tests/async_api_tests/test_power_supply_dc_api.py`` exercises the module in the DC
software-in-the-loop against this specification: an external fake supply implements the
``m2e`` side while EVerest runs charging sessions, faults, derating and communication loss.
