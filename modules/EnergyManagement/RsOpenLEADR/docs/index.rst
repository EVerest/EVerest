.. _everest_modules_handwritten_RsOpenLEADR:

RsOpenLEADR
===========

OpenADR 3.0 VEN (client) that retrieves events from a VTN (server) using
`openleadr-rs <https://github.com/OpenLEADR/openleadr-rs>`_ and applies
capacity limits from these events to an
:ref:`EnergyNode <everest_modules_EnergyNode>` via its ``external_energy_limits``
interface.

.. warning::

   This module is currently **experimental**: configuration parameters and its
   integration in EVerest may change without further notice. It is exempt from
   the stability guarantees and the deprecation period of the EVerest public
   API until promoted to stable (see :ref:`project-experimental-components`).

Requirements
------------

- EVerest built with ``EVEREST_ENABLE_RS_SUPPORT=ON``
- An OpenADR 3.0 VTN, with client credentials for this VEN and the id of the
  program to follow
- One ``external_energy_limits`` consumer, typically the
  :ref:`EnergyNode <everest_modules_EnergyNode>` at the grid connection point

Every ``set_external_limits`` call replaces the external limits of the
EnergyNode. If other sources such as OCPP or EEBUS also set external limits,
connect this module to a dedicated EnergyNode in the energy tree, e.g. between
the grid connection point and the downstream nodes. The EnergyManager then
applies the most restrictive of all limits.

Supported events
----------------

======================= ======================
OpenADR payload type    EVerest schedule
======================= ======================
IMPORT_CAPACITY_LIMIT   ``schedule_import``
EXPORT_CAPACITY_LIMIT   ``schedule_export``
======================= ======================

The ``units`` declared in the event's payload descriptor determine the limit:

=================== ==================================================
Unit                EVerest limit
=================== ==================================================
``KW``              ``total_power_W`` (value × 1000)
``AMPS``            ``ac_max_current_A``, current per phase
=================== ==================================================

If the payload descriptor declares no unit, the value is interpreted as kW.
Payloads in any other unit are ignored with a warning. The limit is applied to
both ``limits_to_root`` and ``limits_to_leaves`` with source ``OpenADR``. Other
payload types are ignored.

How events become limits
------------------------

The events of the configured program, optionally filtered by target, are
merged into a timeline, separately for import and export limits: where events
overlap, the event with the higher priority applies. From each timeline the
module builds a schedule starting at the current time:

- every interval with a limit becomes an entry with that limit
- every gap between intervals, and the time after the last one, becomes an
  entry without limits, so the EnergyNode falls back to its own configured
  limits
- without any events the schedule consists of a single entry without limits,
  which clears previously published limits

The whole schedule is published at every retrieval, so limits change at the
right time even when retrievals are infrequent.

Retrieval
---------

Events are retrieved once at start-up and then according to
``openadr_vtn_retrieve_schedule``. If the VTN cannot be reached, the error is
logged and retrieval is retried at the next scheduled time; the previously
published schedule stays in effect.

Configuration
-------------

``openadr_vtn_retrieve_schedule`` is a cron expression with a seconds field,
evaluated in UTC: ``sec min hour day-of-month month day-of-week``. The default
``0 0 14 * * *`` retrieves events daily at 14:00 UTC, ``0 */15 * * * *`` every
15 minutes.

``openadr_vtn_event_target_type`` is one of ``POWER_SERVICE_LOCATION``,
``SERVICE_AREA``, ``GROUP``, ``RESOURCE_NAME``, ``VEN_NAME``, ``EVENT_NAME`` or
``PROGRAM_NAME``. ``openadr_vtn_event_target_values`` is a comma separated
list of values. If either is empty, events are retrieved without filter.

Leave ``openadr_vtn_client_id`` and ``openadr_vtn_client_secret`` empty for a
VTN without authentication.

Limitations
-----------

- Only OpenADR 3.0 is supported, using openleadr-rs 0.1.0 (the newest 3.0
  release that builds with EVerest's Rust 1.86 toolchain)
- ``randomizeStart`` of intervals is ignored
- Events are polled, notifications from the VTN are not supported
- No reports are sent back to the VTN

Testing with the openleadr-rs VTN
---------------------------------

``config/config-sil-energy-management-openadr.yaml`` runs the energy
management SIL setup with this module and expects a VTN at
``http://localhost:3000`` with the test fixtures of openleadr-rs: VEN
credentials ``ven-1``/``ven-1`` and program ``program-1`` targeting group
``group-1``.

#. Start a PostgreSQL database, e.g.
   ``podman run -d --name oadr-db -e POSTGRES_USER=openadr -e POSTGRES_PASSWORD=openadr -p 127.0.0.1:5432:5432 docker.io/library/postgres:16``
#. Apply the migration from the ``migrations`` folder of the ``openleadr-vtn``
   crate and the fixtures ``programs.sql`` and ``test_user_credentials.sql``
   from the ``fixtures`` folder of the openleadr-rs repository, then connect
   the VEN to the program and target group::

     UPDATE ven SET targets = '[{"type": "GROUP", "values": ["group-1"]}]' WHERE id = 'ven-1';
     INSERT INTO ven_program (program_id, ven_id) VALUES ('program-1', 'ven-1');

#. Build and run the VTN (the database must be reachable at build time)::

     export DATABASE_URL=postgres://openadr:openadr@127.0.0.1:5432/openadr
     cargo install openleadr-vtn --version 0.1.0 --locked
     OAUTH_BASE64_SECRET=$(head -c 48 /dev/urandom | base64 -w0) openleadr-vtn

#. Create an event with the ``any-business``/``any-business`` credentials::

     TOKEN=$(curl -s -X POST localhost:3000/auth/token \
       -d 'grant_type=client_credentials&client_id=any-business&client_secret=any-business' | jq -r .access_token)
     curl -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' localhost:3000/events -d '{
       "objectType": "EVENT", "programID": "program-1", "eventName": "limit", "priority": 1,
       "targets": [{"type": "GROUP", "values": ["group-1"]}],
       "payloadDescriptors": [{"objectType": "EVENT_PAYLOAD_DESCRIPTOR", "payloadType": "IMPORT_CAPACITY_LIMIT", "units": "KW"}],
       "intervalPeriod": {"start": "'$(date -u +%Y-%m-%dT%H:%M:%SZ)'", "duration": "PT30M"},
       "intervals": [{"id": 0, "payloads": [{"type": "IMPORT_CAPACITY_LIMIT", "values": [7.0]}]}]}'

#. Run ``run-scripts/run-sil-energy-management-openadr.sh`` from the build
   directory. The module logs the published schedules, e.g.
   ``Publishing external limits: import [... 7000W, ... none], export [...]``.
