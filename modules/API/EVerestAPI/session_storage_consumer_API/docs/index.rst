.. _everest_modules_handwritten_session_storage_consumer_API:

.. *******************************************
.. session_storage_consumer_API
.. *******************************************

This module bridges the ``session_storage`` interface of a session record store,
such as the ``PersistentSessionStorage`` module, to external MQTT.

The complete API specification can be found in the

``docs/source/reference/EVerest_API/session_storage_consumer_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/session_storage_consumer_API/index.html>`_ automatically generated from it.

Commands
========

``get_sessions`` returns one page of stored session records, ``get_session``
returns a single record identified by its session id or its OCPP transaction
id, and ``clear_sessions`` deletes every stored record.

Pagination
==========

``get_sessions`` returns records oldest first. A reply carries a
``continuation_token`` while more matching records may exist; pass it in the
next request to continue the iteration. A reply without a token means the
iteration is complete. An invalid or outdated token starts the iteration from
the beginning. The payload is optional: an empty object requests the first page
with the default page size.

Deleting records
================

``clear_sessions`` cannot be undone. It also deletes records that are still
open, and when such a session finishes later the finish is dropped and the
record does not reappear.

Missing records
===============

``get_session`` replies with the JSON literal ``null`` when no stored record
matches the given identifier.
