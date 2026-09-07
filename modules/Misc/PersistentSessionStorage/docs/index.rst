.. _everest_modules_handwritten_PersistentSessionStorage:

..  This file is a placeholder for optional multiple files
    handwritten documentation for the PersistentSessionStorage module.

..  This handwritten documentation is optional. In case
    you do not want to write it, you can delete the doc/ directory.

..  The documentation can be written in reStructuredText,
    and will be converted to HTML and PDF by Sphinx.
    This index.rst file is the entry point for the module documentation.

..  Use underlined-only headlines inside this document (highest-level
    sub-section headline should use "=" characters)

..  The content of this file will be included in the auto-generated HTML
    page for the module. You can link to it using the following
    reference: everest_modules_PersistentSessionStorage.

.. *******************************************
.. PersistentSessionStorage
.. *******************************************

Persists a session record for every charging session of the connected EvseManagers in a SQLite database and provides read and clear access to the stored records. Records survive restarts and power cycles, so they can be read out after the fact, for example by an EVerest API client.

Record lifecycle
=================

A record is created ``Open`` when a session starts and completed to ``Finished`` when
it finishes. While a record is ``Open`` only the fields known so far are present; the
stop-side fields are filled in once the session finishes. Each record is identified by
the session id: a session contains at most one transaction, so the session id also
identifies the transaction charged within it.

A session spans from plug-in or authorization until the EVSE is available again. The
transaction nested in it starts at the point where all preconditions for charging are
met -- EV connected and user authorized -- and finishes when one of them irrevocably
becomes false. Sessions that never reach that point are stored just like any other
session, only without a ``transaction`` member: an EV that was plugged in but never
authorized still produces a record.

Sessions with a running transaction that are interrupted by a power loss are closed by
the module during recovery on the next boot. Their transaction gets stop reason
``PowerLoss`` and the session stop timestamp is the time of the recovery, because the
EvseManager reports the recovery finish of the transaction but no session finish of its
own for the resumed session. A session without a running transaction gets no recovery:
its record stays ``Open`` until the next session on the same EVSE marks it ``Stale``.

A record can also end up ``Stale``: this happens when a newer session is started on the
same EVSE while the previous record is still ``Open``, which means that record's finish
was lost, for example in a crash. A ``Stale`` record keeps the fields it already had,
its stop-side fields stay absent, and it will never be finished.

Stored data
===========

For the start and stop of the transaction the total imported energy in Wh is
persisted. There are no energy readings at session level, since a session without a
transaction never charges.

The id token that started a transaction is not stored in the clear. Instead, a SHA256
hash of the id token type concatenated with the id token value is stored, matching the
hash used by the OCPP stack's authorization cache. The one exception is signed meter
values (see ``store_signed_meter_values`` below): formats such as OCMF typically embed
the raw id token in their ID field, so enabling that option persists raw id tokens
after all.

Configuration
==============

``database_path``
  Path to the SQLite database file that holds the session records. It has no default
  and must be configured explicitly, pointing to persistent storage (not ``/tmp``),
  since the records are meant to survive restarts and power cycles. The parent
  directory must exist and be writable. If the database cannot be opened or migrated
  to the expected schema, the module fails to start instead of silently recording
  nothing.

``max_sessions``
  Hard cap on the number of stored session records. When a new record is stored while
  the limit is already reached, the oldest records are deleted first, regardless of
  their state -- this can delete ``Open`` or ``Stale`` records just as well as
  ``Finished`` ones.

``store_signed_meter_values``
  Disabled by default. When enabled, the signed meter values (e.g. OCMF records) of
  transaction start and stop are stored in the ``signed_meter_value_start`` / stop
  fields of the transaction. Enabling this persists raw id tokens, since OCMF
  typically embeds the raw id token in its ID field; without it, the id token is only
  ever stored as the hash described above.

Integration in EVerest
=======================

This module requires one or more ``evse_manager`` connections (1 to 128), each of
which is the source of the session records for one EVSE. The numeric ``evse_id`` and
the ``evse_id_string`` of a record are the ids the ``evse_manager`` reports via
``get_evse``; the connected EvseManagers must have distinct numeric ids.

An optional ``ocpp`` connection (0 or 1) attaches the CSMS transaction id and the OCPP
transaction start and stop timestamps to a record. For OCPP 1.6 the id is the numeric
id assigned by the CSMS, which only becomes available after the transaction has already
started, so it may be missing from a record for a while even after ``Finished``. With an
early OCPP ``TxStartPoint`` an OCPP transaction can also exist for a session that never
reaches an EVerest transaction, so an ``ocpp_transaction_id`` may be present on a record
without a ``transaction`` member.

``ocpp_transaction_timestamp_start`` and ``ocpp_transaction_timestamp_stop`` are the
timestamps of the OCPP transaction itself and can differ from the session and
transaction timestamps of the record: in OCPP 2.x the points at which a transaction
starts and stops are configurable via ``TxStartPoint`` and ``TxStopPoint``, and for
OCPP 1.6 they are the timestamps reported in ``StartTransaction.req`` and
``StopTransaction.req``.

An optional ``session_cost`` connection (0 or 1) attaches cost and tariff information
reported by the CSMS to a record. Its ``id_tag`` member is always stripped, since the
raw id token is not persisted.

The module provides the ``session_storage`` interface with three commands:
``get_sessions`` (one page of stored records, oldest first), ``get_session`` (a single
record looked up by session id or OCPP transaction id) and ``clear_sessions`` (deletes
every stored record, including ``Open`` ones). Clearing cannot be undone; if a finish
arrives later for a record that was already deleted, that finish is simply dropped.

``get_sessions`` is paginated: the caller passes an optional page size and filter
(state, EVSE id, started-after timestamp compared against the session start), and
iterates by passing the ``continuation_token`` of a reply into the next call until a
reply carries no token. The token is opaque and held only by the caller; the module
keeps no iteration state. Records stored during the iteration appear at its end, pruned
records are skipped, and a token that is invalid or refers to a replaced database file
restarts the iteration from the oldest record, so a stale token can re-deliver records
but never silently skip them. Page sizes are additionally capped by a byte budget, so a
page may contain fewer records than requested.
