.. _htg-choosing-version:

###########################
Choosing an EVerest Version
###########################

This guide helps you choose the right EVerest version for your use case.

Please refer to our :ref:`versioning policy <project-release-and-versioning>` for detailed information on
versioning and release cycles.

.. note::

   This guide provides general guidance but is not exhaustive. Actual version and upgrade 
   requirements  depend on your specific deployment, integrations, and customizations.
   Always thoroughly review release notes and test upgrades in non-production environments.

Production Deployments
======================

For production systems, use the latest stable release (``yyyy.mm.x``). These versions provide:

- Tested and verified functionality
- Ongoing maintenance and security updates
- Stable public APIs
- Community support

Development and Testing
=======================

For development of new features or testing upcoming changes, you may use the ``main`` branch. Be aware that:

- Breaking changes may occur at any time
- APIs may be unstable
- This is not suitable for production deployments

Upgrade Planning
================

Within Stable Lines
-------------------

Upgrading within a stable release line (e.g., ``2026.01.0`` to ``2026.01.3``) should be straightforward:

- Review the release notes for the target version
- Test the upgrade in a non-production environment
- Deploy the new version

No configuration or integration changes should be required.

Across Major Releases
---------------------

Upgrading to a new stable release line (e.g., ``2026.01.x`` to ``2026.07.0``) requires more careful planning:

- Review the :ref:`release notes <project-release-notes>` and the upgrade guide of the
  target release, for example :ref:`project-upgrading-to-2026-10`
- Identify any breaking changes affecting your integration
- Update configurations and integrations as needed
- Thoroughly test in a non-production environment
- Plan a maintenance window for production deployment

Migration guides may be provided by the community for each major release documenting known breaking changes and upgrade paths.

Different major releases may be fully backwards compatible, partially compatible, or incompatible depending on the changes introduced.
It is recommended to review the release notes as well as versions of the public API components when upgrading across major releases.

.. _htg-upgrading-persisted-config:

Persisted Configuration Changes
-------------------------------

EVerest module parameters can be changed at runtime through the configuration
service, for example by a CSMS through a parameter that the OCPP 2.x module
exposes in its device model (OCPP 1.6 does not expose module parameters). Such
changes are persisted outside the config file:

- Without ``--db``, in ``user-config/<config-name>.yaml`` next to the config
  file, or in the file given with ``--user-config``. The file is merged over
  the config file on every start.
- With ``--db``, in the configuration database. The database takes precedence
  over the config file once its boot slot holds modules.

See :doc:`/reference/manager-cli` for both modes.

OCPP's own configuration is stored separately. The OCPP module keeps the
variables a CSMS changes with ``ChangeConfiguration`` (OCPP 1.6) or
``SetVariables`` (OCPP 2.x) and carries them over to new EVerest versions, so
they need no attention during an upgrade.

As long as an upgrade does not change the EVerest configuration, that is no
module is added, replaced or removed and no parameter is renamed, persisted
changes are retained in both modes.

An upgrade that changes the configuration has to take the persisted changes
into account. As of now, EVerest does not migrate them automatically; carrying
renamed parameters or the switch from a module to its successor over into the
user-config or the database is the integrator's responsibility.

- Without ``--db``, the user-config is still merged over the changed config
  file. A parameter that the new module does not define is ignored with an
  error in the log, and an entry for a module id that is no longer in the
  config file makes the configuration invalid, so the manager does not start.
- With ``--db``, a changed config file is not read while the boot slot holds
  modules, and ``manager --check`` validates only the file. ``--dump <dir>``
  on a regular start writes the configuration that was actually booted to
  ``config.json`` in that directory; ``--reset-from-yaml`` replaces the stored
  configuration with the config file and discards the persisted changes.
- Databases created by releases before 2026.10 also held the manager
  ``settings`` (MQTT broker and prefixes, installation paths). The schema
  migration drops them, so a deployment that ran with ``--db`` alone needs
  ``--config`` alongside it to keep non-default settings (see
  :doc:`/explanation/configuration-service`).

Whether persisted changes survive an update at all depends on where the
user-config directory and the database file are placed. On storage that an
update preserves, such as a data partition or a writable overlay, they persist
and have to be migrated. On storage that an update replaces, such as the root
file system of an A/B scheme, they are lost with the old release, and the new
release starts from the config file it ships with.
