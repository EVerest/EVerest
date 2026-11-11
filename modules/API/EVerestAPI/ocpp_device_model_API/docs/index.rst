.. _everest_modules_handwritten_ocpp_device_model_API:

.. *******************************************
.. ocpp_device_model_API
.. *******************************************

.. warning::

   This API is **experimental**: its topics and types may change without further notice.

The module lets a client outside EVerest add components and variables to the OCPP 2.x device model, as an owner of
the ``ocpp_device_model`` interface. See :ref:`howto-ocpp-device-model` for a walk-through.

At startup, the module answers ``get_registrations`` from the registrations clients publish on ``m2e/registration``:

- With ``cfg_expected_owners`` set, it answers as soon as every listed owner has registered, at the latest after
  ``cfg_expected_owners_timeout_s``, and logs the owners still missing.
- Otherwise it collects registrations for ``cfg_registration_timeout_s``.

After the answer, each registration is passed on at once. It may reach the OCPP module before the answer itself, so
a client replaces or releases an owner registered at startup only after it received the owner's
``e2m/set_registration_result``.

A write to a registered variable is answered ``Rejected`` when the client does not reply within
``cfg_request_reply_to_s``.

The three timeouts are at most 240 s each, below the 300 s after which EVerest aborts a command. A registration or
value change that does not fit the types of the specification, a string beyond its maximum length for example, is
ignored and logged.

Communication supervision is per module instance, not per client or owner. ``e2m/heartbeat`` is one heartbeat that
every client can subscribe to, and ``m2e/communication_check`` feeds one timer:

- ``true`` from any client restarts the timer, so ``generic/CommunicationFault`` is raised only when all clients
  stay silent for ``cfg_communication_check_to_s``. A client that stopped is not detected while another one still
  sends.
- ``false`` from any client switches the supervision off for all of them, until the module restarts.
- The error does not name a client or an owner.

To supervise clients separately, configure one module instance per client. The topics contain the module id, so
each instance has its own heartbeat, timer and error.

The complete API specification can be found in the

``docs/source/reference/EVerest_API/ocpp_device_model_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/ocpp_device_model_API/index.html>`_ automatically generated from it.
