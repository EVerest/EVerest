.. _everest_modules_handwritten_ErrorHistory:

.. *******************************************
.. ErrorHistory
.. *******************************************

Stores every error raised in the system in a SQLite database at ``database_path`` and
returns the stored errors through the ``get_errors`` command of the ``error_history``
interface. An error is stored when it is raised and updated when it is cleared; errors
still active at startup are marked ``ClearedByReboot``.

Database schema
===============

The module checks the schema of an existing database at startup:

- A file that is not an SQLite database, a database without an ``errors`` table, or one
  whose ``errors`` table lacks one of the columns the module needs, is reset. Its stored
  errors are lost.
- Additional columns, for example from a newer version of the module, are ignored and
  kept, unless they are ``NOT NULL`` without a default. The module could not store errors
  in such a database, so it is reset as well.
- If the check itself fails, for example because the database is locked or can not be
  read, the database is kept as it is and the error is logged.

If the database can not be reset or created, the error is logged and the module runs
without storing errors.
