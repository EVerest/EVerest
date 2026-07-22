.. _everest_modules_handwritten_ocpp_consumer_API:

.. *******************************************
.. ocpp_consumer_API
.. *******************************************

The complete API specification can be found in the

``docs/source/reference/EVerest_API/ocpp_consumer_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/ocpp_consumer_API/index.html>`_ automatically generated from it.

Variable addressing
===================

``get_variables`` / ``set_variables`` / ``monitor_variables`` use canonical
component/variable addressing (e.g. component ``OCPPCommCtrlr``, variable
``HeartbeatInterval``), identical for OCPP 1.6 and 2.x. The OCPP 1.6 key-only
form (empty ``component.name``, ``variable.name`` = configuration key) is
deprecated: it is only accepted while OCPP 1.6 is active and will be removed
per the deprecation policy. See the "Configuration access" and "Migration from
OCPP 1.6 key addressing" sections in the OCPPmulti module documentation
(:ref:`everest_modules_handwritten_OCPPmulti`) for examples and the migration
guide.
