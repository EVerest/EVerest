.. _everest_modules_handwritten_evse_board_support_API:

.. *******************************************
.. evse_board_support_API
.. *******************************************

The complete API specification can be found in the

``docs/source/reference/EVerest_API/evse_board_support_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/evse_board_support_API/index.html>`_ automatically generated from it.

Capabilities
============

Publish ``capabilities`` once as soon as they are known after start and before
the first charging session. EVerest offers no current until it received them: on
a plug-in it requests a PWM duty cycle of 100 % (state X1) and waits. The phase
count used by the energy management is initialised from
``max_phase_count_import``. Publish the capabilities again whenever a limit
changes at runtime, for example when the maximum current drops because the
hardware gets too hot; a changed current limit reaches the PWM within a few
seconds, also during charging.

``tests/async_api_tests/test_evse_board_support_api.py`` exercises the module in the AC
software-in-the-loop against this specification: a fake board implements the ``m2e`` side
and plays the car while EVerest runs basic charging sessions, faults and communication loss.
