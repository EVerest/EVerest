.. _project-upgrading-to-2026-10:

######################
Upgrading to 2026.10
######################

This page lists the steps needed to upgrade a deployment from 2026.02.x to
2026.10.0. The release notes on the
`GitHub releases page <https://github.com/EVerest/EVerest/releases>`_ summarize
the same changes together with new features and fixes.

Work through the sections that apply to your deployment. Then validate the
configuration with ``manager --check --config <yaml>`` before the first start.

.. contents:: On this page
   :local:
   :depth: 1

.. _project-upgrading-to-2026-10-manager-settings:

Manager settings and the configuration database
===============================================

Schema migration 4 of the configuration database drops the ``SETTING`` table.
The database holds module configurations only; manager settings (installation
paths, MQTT broker and prefixes, telemetry, schema validation and exception
forwarding) come from a settings file or the deprecated ``settings:`` block of
the configuration file.

- ``manager --db <file>`` without ``--settings`` or ``--config`` runs with the
  built-in defaults for every setting. A database created by 2026.02.x with a
  non-default broker or prefix loses those values on the automatic migration.
- Keep non-default settings in a settings file and pass it with
  ``--settings <file>``. The option works with or without ``--config`` and
  ``--db``. The file is validated against ``schemas/settings.yaml`` and rejects
  unknown and deprecated keys: drop ``www_dir``, ``controller_port``,
  ``controller_rpc_timeout_ms``, ``prefix`` and ``config_file``, and see
  :ref:`project-upgrading-to-2026-10-capabilities` for ``run_as_user``. The
  installed examples ``etc/everest/settings-mqtt-tcp.yaml``,
  ``settings-mqtt-uds.yaml`` and ``settings-all-options.yaml`` are a starting
  point. See :ref:`exp_config_service_settings`.
- The ``settings:`` block of the configuration file is deprecated. It still
  applies with ``--config`` as long as ``--settings`` is not given; with
  ``--settings`` it is ignored with a warning.
- Tools that read the database directly must follow the new schema: a
  ``CONFIG`` table, composite primary keys, and renumbered ``MUTABILITY`` and
  ``DATATYPE`` ids. Configurations marked invalid (``CONFIG_META.VALID = 0``)
  are deleted during the migration.

File-based SQLite databases, including the configuration, OCPP and device model
databases, use WAL journal mode. Back up the ``-wal`` and ``-shm`` files
together with the ``.db`` file, or checkpoint the database first.

See :ref:`reference-manager-cli` and :ref:`exp-configuration-service`.

.. _project-upgrading-to-2026-10-manager-cli:

Manager command line
====================

- Options must be spelled in full. Abbreviated spellings no longer parse.
- ``--conf`` is a deprecated alias of ``--config`` and cannot be combined
  with it. Replace it in start scripts and service units.
- ``manager --check`` requires ``--config``. Change health checks and CI jobs
  to ``manager --check --config <yaml>``. The check needs no MQTT broker and
  does not touch the configuration database.
- ``--db-init`` has no effect and is deprecated. Seeding the database from the
  YAML configuration is the default when the database holds no valid
  configuration.

.. _project-upgrading-to-2026-10-capabilities:

Linux capabilities
==================

The manager no longer grants Linux capabilities to modules. The
``capabilities`` key of a module entry in the configuration file is ignored
and logs a warning. This change did not go through a deprecation period and is
an exception to the :ref:`deprecation policy <project-deprecation-policy>`. Modules declare the capabilities they need in their
``manifest.yaml``, and the deployment grants them as file capabilities.

- On Yocto, use the ``everest_file_capabilities`` class of meta-everest.
- Elsewhere, run ``applications/utils/scripts/set_module_capabilities.py``
  after installing.

This affects deployments that run the manager as an unprivileged user and use
``EvseSlac``, ``PacketSniffer``, ``Setup`` or ``Huawei_V100R023C10``.
``settings.run_as_user`` is deprecated as well: start the manager as the
unprivileged user from the service manager, for example with ``User=everest``
in the systemd unit. See :doc:`/how-to-guides/security-best-practices`.

EvseSecurity writes private keys with mode 0600 and restricts the key
directories to 0700. Run modules that read key files, such as the OCPP and
ISO 15118 modules, as the same user as EvseSecurity.

.. _project-upgrading-to-2026-10-configuration:

Module configuration
====================

The following configuration values fail validation or change meaning:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Module and key
     - Change and action
   * - ``RsIskraMeter`` ``read_meter_values_interval_ms``
     - Limited to 1000–4000 ms, default 2000 ms. Lower larger values.
   * - ``Evse15118D20`` ``negative_bidirectional_limits``
     - Defaults to ``true``, so ISO 15118-20 discharge limits are sent as
       negative values. Set it to ``false`` to keep positive limits.
   * - ``EvseManager`` ``ac_limit_slac_init_retries``
     - Defaults to ``true``, which limits SLAC retries as ISO 15118-3
       requires. Set it to ``false`` for unlimited retries.
   * - ``EvseManager`` ``lock_connector_in_state_b: false``
     - Deprecated. Use ``unlock_when_deauthorized: true``.
   * - ``RsIskraMeter`` ``implementation_id: meter``
     - Deprecated. Connect to ``implementation_id: main``.
   * - ``Evse15118D20`` ``logging_path``
     - Deprecated and without effect, since the session logger was removed.
       Delete it.

EvseManager also treats errors from the SLAC and ISO 15118 modules as faults:
anything other than ``generic/VendorWarning`` makes the EVSE Inoperative.

.. _project-upgrading-to-2026-10-module-switches:

Replacing deprecated modules
============================

Two module families are deprecated in 2026.10.0 and may be removed in
2027.04.0:

- ``OCPP`` and ``OCPP201``, superseded by
  :ref:`OCPPmulti <everest_modules_OCPPmulti>`, see
  :ref:`howto-ocpp-storage-migration` and
  :ref:`project-upgrading-to-2026-10-ocpp`.
- ``EvseV2G`` and ``IsoMux``, superseded by
  :ref:`Evse15118D20 <everest_modules_Evse15118D20>`, see
  :ref:`howto-iso15118-stack-migration`.

Replacing a module changes the module set of the configuration. Parameters
changed at runtime are persisted outside the configuration file, in the
user-config or the configuration database, and EVerest does not carry them
over to the successor module. :ref:`htg-upgrading-persisted-config` describes
what to do for each storage mode.

.. _project-upgrading-to-2026-10-ocpp:

OCPP 2.x component configuration
================================

- ``InternalCtrlr.ChargePointId`` and ``InternalCtrlr.NumberOfConnectors``
  are removed from the component configuration, and existing device-model
  databases drop them on the next start. Neither was used for the connection;
  the station identity comes from ``SecurityCtrlr.Identity`` as before. In
  OCPP 1.6 mode ``NumberOfConnectors`` is provided by ``OCPP16LegacyCtrlr``.
  A CSMS or tool that reads either variable through GetVariables or
  GetBaseReport gets UnknownVariable.
- Network connection profiles live in per-slot ``NetworkConfiguration_<N>``
  components. An existing ``InternalCtrlr.NetworkConnectionProfiles`` value is
  migrated once at startup, see :ref:`tutorial-ocpp2-migration`. Provision
  new deployments through ``NetworkConfiguration_<N>.json``.
- ``minLimit`` and ``maxLimit`` stored in an existing device-model database
  are kept, so changed limits in the shipped component configuration apply
  only to new databases. ``SecurityCtrlr.SecurityProfile`` accepts ``0``
  regardless of a stored minimum.
- ``InternalCtrlr.EnableTLSKeylog`` is ReadOnly. Set it in the component
  configuration instead of through SetVariables.
- The device model holds its values in memory and writes changes through to
  the database. Edit the device-model database only while EVerest is stopped;
  changes made while it runs are not seen until the next start. An attribute
  marked ``"persistent": false`` in a custom component configuration is no
  longer stored.

.. _project-upgrading-to-2026-10-build:

Building and packaging
======================

- EVerest builds from the repository root only. It installs the CMake
  packages ``everest-base``, ``everest-iso15118``, ``everest-ocpp`` and
  ``everest-core``, selected with ``EVEREST_PACKAGES``, and all targets are
  named ``everest::<name>``. Remove ``EVEREST_LIBS_ONLY``,
  ``EVEREST_INCLUDE_LIBS``, ``EVEREST_EXCLUDE_LIBS``, ``EVEREST_IO_WITH_MQTT``
  and the per-library ``*_INSTALL`` and ``*_BUILD_TESTING`` options; configure
  fails with them. Builds with EDM install no CMake packages, so consumers that
  use ``find_package()`` install from a ``-DDISABLE_EDM=ON`` build.
- everest-cmake 0.9 or newer is required. The ``install_everest_testing``
  target is removed.
- Rust modules need Rust 1.86 or newer.
- The CMake options ``EVEREST_ENABLE_ADMIN_PANEL_BACKEND`` and
  ``EVEREST_INSTALL_ADMIN_PANEL`` are removed.

Yocto
-----

- Recipes shared between Yocto releases live in the
  ``meta-everest-common`` layer, which meta-everest adds automatically. They
  use ``${UNPACKDIR}``. Update bbappends that refer to ``${WORKDIR}`` paths of
  ``everest-core``, ``ev-cli`` or ``python3-iso15118``.
- The compatibility recipes ``everest-framework``, ``everest-sqlite``,
  ``libcbv2g``, ``libevse-security``, ``libfsm``, ``libiso15118``,
  ``liblog``, ``libocpp``, ``libslac`` and ``libtimer``, the ``asyncapi``
  backport recipe and the admin-panel packages are removed. Drop them from
  ``IMAGE_INSTALL``, ``DEPENDS`` and ``RDEPENDS``.
- ``everest.service`` runs the manager with the configuration database
  ``/var/lib/everest/everest.db`` and the settings file
  ``settings-mqtt-uds.yaml``, and with ``systemd`` meta-everest configures
  Mosquitto for a Unix domain socket. Edits of ``config.yaml`` take effect only
  until the database is initialized, and ``/var/lib/everest`` has to persist
  across root file system updates.

See :ref:`exp_linux_yocto_building_yocto`, including its *systemd service*
section for the service and the Mosquitto setup.

.. _project-upgrading-to-2026-10-apis:

EVerest APIs
============

- ``evse_board_support`` (all BSP drivers, ``evse_board_support_API`` included):
  EvseManager requests state X1 only with ``cp_state_X1``. Until 2026.10 most
  paths already did, but a duty cycle of 100 % (for example a current limit
  below 6 A in basic AC charging) was sent as ``pwm_on(100)``. ``pwm_on`` is
  only called with a duty cycle strictly between 0 and 100 %.

The deprecated topics keep working until 2027.04.0 at the earliest. See
:ref:`project-deprecation-index` for the full list, including:

- ``ocpp_consumer_API``: replace ``is_connected`` with ``connection_status``,
  and use component/variable addressing instead of OCPP 1.6 key-only
  addressing, see
  :ref:`handwritten_ocppmulti_migration-from-ocpp16-key-addressing`.
- ``evse_manager_consumer_API``: replace the ``evse_id`` topics with
  ``get_evse``.
- ``error_history_consumer_API``: stop relying on ``severity`` and
  ``severity_filter``.
- ``API`` module: move to the :ref:`EVerest API modules <everest_apis>`.

API modules publish compact JSON. Only clients that compare raw payload
strings are affected.

API modules raise the initial ``CommunicationFault``, and EvseManager its
``Inoperative``, only if no ``communication_check`` arrives within
``cfg_communication_check_to_s``, no longer right at start.
