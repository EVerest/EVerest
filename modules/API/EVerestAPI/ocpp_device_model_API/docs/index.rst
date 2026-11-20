.. _everest_modules_handwritten_ocpp_device_model_API:

.. *******************************************
.. ocpp_device_model_API
.. *******************************************

.. warning::

   This API is **experimental**: its topics and types may change without further notice.

The module lets a client outside EVerest add components and variables to the OCPP 2.x :term:`device model`, as an
:term:`owner <external owner>` of the ``ocpp_device_model`` interface. See :ref:`howto-ocpp-device-model` for a
walk-through and the :ref:`reference-ocpp-device-model-glossary` for the terms used here.

The OCPP module uses the module through its ``device_model`` requirement:

.. code-block:: yaml

   active_modules:
     device_model_api:
       module: ocpp_device_model_API
     ocpp:
       module: OCPPmulti
       connections:
         device_model:
           - module_id: device_model_api
             implementation_id: main

At startup, the module answers ``get_registrations`` from the registrations clients publish on ``m2e/registration``:

- With ``cfg_expected_owners`` set, it answers as soon as every listed owner has registered, at the latest after
  ``cfg_expected_owners_timeout_s``, and logs the owners still missing.
- Otherwise it collects registrations for ``cfg_registration_timeout_s``.

Connect the module to one OCPP module. Each ``get_registrations`` call starts a collection, and while one runs, the
registrations clients publish are kept for its answer and passed on to no OCPP module.

After the answer, each registration is passed on at once. It may reach the OCPP module before the answer itself, so
a client replaces or releases an owner registered at startup only after it received the owner's
``e2m/set_registration_result``.

Value changes are passed on at once, also while the module collects registrations. The OCPP module keeps the latest
value of each attribute it receives before the answer and applies it to the registration of the owner.

If the OCPP module's ``get_registrations`` call fails, for example because EVerest aborted it after 120 s, the
registrations collected for it do not reach the OCPP module. Their owners are missing from the device model until a
client publishes their registration again or EVerest restarts.

A write to a registered variable is answered ``Rejected`` when the client does not reply within
``cfg_request_reply_to_s``.

The three timeouts are at most 100 s each, below the 120 s after which EVerest aborts a command. A registration that
does not fit the types of the specification, a string beyond its maximum length for example, is not passed on; the
module answers it on ``e2m/set_registration_result`` with ``Invalid`` for its owner, or for the module id when it names
none, unless the message is not a JSON object or its owner is not a non-empty string. A value change that does not
fit the types is ignored and logged.

Communication supervision is per module instance, not per client or owner. ``e2m/heartbeat`` is one heartbeat that
every client can subscribe to, and ``m2e/communication_check`` feeds one timer:

- ``true`` from any client restarts the timer, so ``generic/CommunicationFault`` is raised only when all clients
  stay silent for ``cfg_communication_check_to_s``. A client that stopped is not detected while another one still
  sends.
- ``false`` from any client switches the supervision off for all of them, until the module restarts.
- The error does not name a client or an owner.

.. mermaid::

   sequenceDiagram
       participant App as Client
       participant API as ocpp_device_model_API
       API-)App: e2m/heartbeat
       App-)API: m2e/communication_check true
       Note over App,API: the client stays silent<br/>for cfg_communication_check_to_s
       Note over API: raises generic/CommunicationFault
       App-)API: m2e/communication_check true
       Note over API: clears generic/CommunicationFault

A ``CommunicationFault`` does not touch the registrations: the variables stay registered with their last values, and
writes are answered ``Rejected`` after ``cfg_request_reply_to_s``.

To supervise clients separately, configure one module instance per client. The topics contain the module id, so
each instance has its own heartbeat, timer and error.

The complete API specification can be found in the

``docs/source/reference/EVerest_API/ocpp_device_model_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/ocpp_device_model_API/index.html>`_ automatically generated from it.
