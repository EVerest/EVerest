.. _everest_modules_handwritten_evse_board_support_API:

.. *******************************************
.. evse_board_support_API
.. *******************************************

The complete API specification can be found in the

``docs/source/reference/EVerest_API/evse_board_support_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/evse_board_support_API/index.html>`_ automatically generated from it.

``tests/async_api_tests/test_evse_board_support_api.py`` exercises the module in the AC
software-in-the-loop against this specification: a fake board implements the ``m2e`` side
and plays the car while EVerest runs basic charging sessions, faults and communication loss.
