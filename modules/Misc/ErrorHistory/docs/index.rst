.. _everest_modules_handwritten_ErrorHistory:

.. *******************************************
.. ErrorHistory
.. *******************************************

Stores every error raised in the system in a SQLite database at ``database_path`` and
returns the stored errors through the ``get_errors`` command of the ``error_history``
interface. An error is stored when it is raised and updated when it is cleared; errors
still active at startup are marked ``ClearedByReboot``.

Each error is returned with the vendor id of its raiser, which is left out when empty,
and with its trigger value, if the raiser named one.

Database schema
===============

The module checks the schema of an existing database at startup:

- A file that is not an SQLite database, a database without an ``errors`` table, or one
  whose ``errors`` table lacks one of the columns the module needs, is reset. Its stored
  errors are lost.
- The nullable column ``trigger_value_text_repr`` is added in place when it is missing.
  Errors stored without it keep all their data and are NULL in it.
- Additional columns, for example written by another version of the module, are ignored
  and kept, unless they are ``NOT NULL`` without a default. The module could not store
  errors in such a database, so it is reset as well. Columns are only ever added as
  nullable columns, so that every version of the module can still store errors in the
  database.
- If the check or the migration fails, for example because the database is locked or can
  not be written, the database is kept as it is and the error is logged. Until the
  migration succeeds at a startup, errors are stored without the fields of the missing
  columns.

If the database can not be reset or created, the error is logged and the module runs
without storing errors.
