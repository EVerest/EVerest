.. _project-experimental-index:

##########################
Experimental Feature Index
##########################

This page lists all public API components that are currently marked as
**experimental** in EVerest. It is at least updated with every stable release.

Experimental components are part of the public API surface but are exempt from
the stability guarantees and the deprecation period. They may change in
incompatible ways or be removed in any release, outside the normal deprecation
process. See :ref:`project-experimental-components` for how they are marked,
promoted and removed, and :ref:`project-deprecation-index` for components that
are on the way out rather than on the way in.

With every stable release the maintainers review this list and decide, per
component, whether to promote it to stable, keep it experimental, or remove it.

The manager options in the table below are documented in full in
:ref:`reference-manager-cli`, and the manager logs a warning at startup naming
any experimental option it was given.

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Component
     - Experimental since
     - Notes
   * - ``manager --graceful-shutdown``
     - 2026.10.0
     - Opt-in graceful module shutdown. Without it, module processes are
       terminated immediately on shutdown, restart and crash.
   * - ``manager --into-idle``
     - 2026.10.0
     - Boot into Idle without starting modules, keeping the configuration API
       available.
   * - ``manager --recover-module-crashes``
     - 2026.10.0
     - Bounded automatic module restart after an unexpected module exit.
   * - ``manager --reset-from-yaml``
     - 2026.10.0
     - Discard the database slot and re-seed it from the YAML config. Intended
       for development use.
   * - ``manager --idle-on-failure``
     - 2026.10.0
     - Stay alive in Idle when there is nothing startable, instead of exiting
       with an error.
   * - ``manager --configuration-api`` and its ``configuration_API`` AsyncAPI
       specification
     - 2026.10.0
     - :ref:`Management API <exp_management_apis>` in the manager for
       configuration slot management, loading raw YAML and updating
       configuration parameters. The option values, channels, operations and
       message payloads may change without further notice.
   * - ``manager --lifecycle-api`` and its ``lifecycle_API`` AsyncAPI
       specification
     - 2026.10.0
     - :ref:`Management API <exp_management_apis>` in the manager for starting
       and stopping modules and monitoring their status. The option values,
       channels, operations and message payloads may change without further
       notice.
   * - :ref:`EEBUS module <everest_modules_EEBUS>`
     - 2026.10.0
     - Bridge to an external EEBUS gRPC service, implementing the Limitation of
       Power Consumption use case. Configuration parameters and the integration
       in EVerest may change without further notice. The maintainer of
       ``eebus-go``, the EEBUS stack underneath the ``eebus_grpc_api`` sidecar
       the module drives, will not maintain it going forward.
   * - :ref:`evse_security_consumer_API module <everest_modules_evse_security_consumer_API>`
       and its ``evse_security_consumer_API`` AsyncAPI specification
     - 2026.10.0
     - External read access to the EvseSecurity module (``is_ca_certificate_installed``,
       ``get_leaf_certificate_info``, ``get_verify_location``). Channels,
       operations and message payloads may change without further notice.
   * - :ref:`PersistentSessionStorage module <everest_modules_PersistentSessionStorage>`
     - 2026.10.0
     - Persists a session record per charging session in a SQLite database and
       provides paginated read and clear access. Configuration parameters, the
       stored record format and the integration in EVerest may change without
       further notice.
   * - :ref:`session_storage_consumer_API module <everest_modules_session_storage_consumer_API>`
       and its ``session_storage_consumer_API`` AsyncAPI specification
     - 2026.10.0
     - External read and clear access to the session records of the
       PersistentSessionStorage module (``get_sessions``, ``get_session``,
       ``clear_sessions``). Channels, operations and message payloads may change
       without further notice.
