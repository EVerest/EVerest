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
       :ref:`OCPPmulti <everest_modules_OCPPmulti>`
     - 2026.10.0
     - 2027.04.0
     - :ref:`Migrate to the Combined OCPP Module <howto-ocpp-storage-migration>`
   * - :ref:`OCPP201 module <everest_modules_OCPP201>` (OCPP 2.0.1 / 2.1), superseded by
       :ref:`OCPPmulti <everest_modules_OCPPmulti>`
     - 2026.10.0
     - 2027.04.0
     - :ref:`Migrate to the Combined OCPP Module <howto-ocpp-storage-migration>`
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