.. _everest_modules_handwritten_session_storage_consumer_API:

.. *******************************************
.. session_storage_consumer_API
.. *******************************************

.. warning::

   This API module is currently **experimental**: its AsyncAPI channels,
   operations and message payloads may change without further notice. It is
   exempt from the stability guarantees and the deprecation period of the
   EVerest public API until promoted to stable (see
   :ref:`project-experimental-components`).

This module bridges the ``session_storage`` interface of a session record store,
such as the ``PersistentSessionStorage`` module, to external MQTT.

The complete API specification, including the pagination, deletion and
concurrency semantics, can be found in the

``docs/source/reference/EVerest_API/session_storage_consumer_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/session_storage_consumer_API/index.html>`_ automatically generated from it.

Commands
========

``get_sessions`` returns one page of stored session records, ``get_session``
returns a single record identified by its session id or its OCPP transaction
id, and ``clear_sessions`` deletes stored records.

Pagination
==========

``get_sessions`` returns records oldest first. A reply carries a
``continuation_token`` if more matching records follow; pass it in the next
request to continue the iteration. A reply without a token means the iteration
is complete. A page can hold fewer records than requested, in rare cases none,
while still carrying a token. An invalid or outdated token starts the iteration
from the beginning. The payload is optional: an empty object requests the first
page with the default page size. The ``started_after`` filter is compared at
millisecond precision.

Deleting records
================

``clear_sessions`` cannot be undone. It also deletes records that are still
open, and when such a session finishes later the finish is dropped and the
record does not reappear. The payload is optional: an empty object deletes every
stored record. To delete exactly the records already read, pass the session id
of the last record received from ``get_sessions`` as ``up_to_session_id``; that
record and every older one are deleted, sessions that started in the meantime
are kept. The reply carries the number of deleted records, 0 if no record with
the given session id exists.

Concurrent clients
==================

Neither the bridge nor the store keeps per-client state, so several clients may
use the API at the same time. A ``clear_sessions`` issued by one client while
another is paging removes records from that iteration; the other client's token
stays valid and resumes at the oldest surviving record. Clients sharing one store
should coordinate clearing, or clear only what they have read via
``up_to_session_id``.

Missing records
===============

``get_session`` replies with the JSON literal ``null`` when no stored record
matches the given identifier.
