.. _project-deprecation-index:

########################
Active Deprecation Index
########################

This page lists all public API components that are currently deprecated in
EVerest. It is at least updated with every stable release.

Each entry records the deprecated component, the release in which it was
deprecated, the earliest release in which it may be removed (following the
:ref:`minimum deprecation period <project-deprecation-policy>`), and a link
to the corresponding migration guide.

.. list-table::
   :header-rows: 1
   :widths: 30 20 20 30

   * - Component
     - Deprecated in
     - Earliest removal
     - Migration guide
   * - :ref:`OCPP module <everest_modules_OCPP>` (OCPP 1.6), superseded by
       :ref:`OCPPmulti <everest_modules_OCPPmulti>` (stable since 2026.10.0).
     - 2026.10.0
     - 2027.04.0
     - :ref:`Migrate to the Combined OCPP Module <howto-ocpp-storage-migration>`.
       The module logs a deprecation warning at startup.
   * - :ref:`OCPP201 module <everest_modules_OCPP201>` (OCPP 2.0.1 / 2.1), superseded by
       :ref:`OCPPmulti <everest_modules_OCPPmulti>` (stable since 2026.10.0)
     - 2026.10.0
     - 2027.04.0
     - :ref:`Migrate to the Combined OCPP Module <howto-ocpp-storage-migration>`.
       The module logs a deprecation warning at startup.
   * - The `RsIskraMeter` deprecates its `meter` implementation_id in favor of `main`.
     - 2026.10.0
     - 2027.04.0
     - Change `implementation_id: meter` to `implementation_id: main` in the config connection
   * - The `evse_id` topics of the `evse_manager_consumer_API` AsyncAPI
       (`e2m/evse_id` and `m2e/evse_id/get`), superseded by the `get_evse`
       command
     - 2026.10.0
     - 2027.04.0
     - Use the `get_evse` command, which provides both the eMI3-format EVSE ID
       (ISO 15118-2 Annex H) and the DIN 70121 EVSE ID (DIN SPEC 91286)
   * - ``manager --conf``, alias of ``--config``
       (see :ref:`reference-manager-cli`)
     - 2026.10.0
     - 2027.04.0
     - Replace ``--conf <path>`` with ``--config <path>`` in start scripts and
       service units. Passing both is rejected; using ``--conf`` logs a warning
       at startup.
   * - ``manager --db-init``, now a no-op
       (see :ref:`reference-manager-cli`)
     - 2026.10.0
     - 2027.04.0
     - Drop the flag. Seeding the database from the YAML config when it holds no
       valid configuration is the default with ``--config --db``. Use the
       experimental ``--reset-from-yaml`` to force re-seeding.
   * - ``ocpp_consumer_API``: ``is_connected`` variable and its
       ``get_is_connected`` request/reply (operations ``receive_is_connected``,
       ``send_request_get_is_connected``, ``receive_reply_get_is_connected``,
       schema ``ConnectedStatus``), superseded by ``connection_status``
     - 2026.10.0
     - 2027.04.0
     - Subscribe to ``receive_connection_status`` (or request it via
       ``send_request_get_connection_status``) and read its ``connected``
       property; the message also carries the details of the connection the
       status refers to.
   * - ``ocpp_consumer_API`` and ``ocpp`` interface: OCPP 1.6 key-only variable
       addressing (empty ``component.name``, ``variable.name`` = configuration
       key) in ``get_variables``, ``set_variables`` and ``monitor_variables``,
       superseded by canonical component/variable addressing
     - 2026.10.0
     - 2027.04.0
     - :ref:`Migration from OCPP 1.6 key addressing
       <handwritten_ocppmulti_migration-from-ocpp16-key-addressing>`. OCPPmulti
       logs a warning naming the canonical address on the first use of each
       legacy key.
   * - :ref:`EvseManager <everest_modules_EvseManager>` configuration option
       ``lock_connector_in_state_b: false``
     - 2026.10.0
     - 2027.04.0
     - Remove the option and set ``unlock_when_deauthorized: true`` instead.
       The connector then stays unlocked in CP state B until the session is
       authorized, which covers the original use case (no lock on plug-in
       before authorization). Unlike the deprecated option it keeps the
       connector locked while an authorized session is paused in state B and
       does not lock in state C/D without authorization or closed relays.
       Both options violate IEC 61851-1:2019 D.6.5 Table D.9 line 4 and must
       not be used in public environments; the module logs a warning at
       startup for either.
   * - :ref:`API module <everest_modules_API>` (``everest_api/<connector>/cmd/*`` and
       ``everest_api/<connector>/var/*`` topics), superseded by the
       :ref:`EVerest API modules <everest_apis>`
     - 2026.10.0
     - 2027.04.0
     - Replace the module with the EVerest API modules covering the topics in use:
       ``evse_manager_consumer_API`` for charging control (``enable_disable``,
       ``pause_charging``, ``resume_charging``, ``stop_charging``,
       ``force_unlock``, ``uk_random_delay``) and EVSE state (``session_info``,
       ``ev_info``, ``hardware_capabilities``, ``powermeter``, ``limits``,
       ``selected_protocol``), ``external_energy_limits_consumer_API`` for
       ``set_limit_amps``, ``set_limit_amps_phases`` and ``set_limit_watts``,
       ``ocpp_consumer_API`` for ``ocpp/var/connection_status`` and
       ``ocpp/var/charging_schedules``, and ``error_history_consumer_API`` for
       ``errors/var/active_errors``. ``info/var/info`` (charger information) has
       no replacement. The module logs a deprecation warning at startup.
   * - :ref:`EvAPI module <everest_modules_EvAPI>` (``everest_api/ev_connectors``
       and ``everest_api/<connector>/var/*`` topics)
     - 2026.10.0
     - 2027.04.0
     - No replacement. The module logs a deprecation warning at startup.
   * - ``error_history_consumer_API``: ``severity`` property of ``ErrorObject``
       (all messages carrying errors: ``get_errors``, ``active_errors``,
       ``error_raised``, ``error_cleared``), the ``severity_filter`` argument of
       ``get_errors``, and the ``Severity`` and ``SeverityFilter`` schemas
     - 2026.10.0
     - 2027.04.0
     - No replacement: EVerest defines no clear semantics for error severity.
       Stop reading ``severity`` from error objects and do not treat it as
       required; drop ``severity_filter`` from ``get_errors`` requests and
       filter by ``type_filter``, ``origin_filter`` or ``state_filter`` instead.
