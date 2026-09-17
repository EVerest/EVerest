.. _howto-deprecate-a-module:

##################################
Deprecate a Module or a Config Key
##################################

This guide shows how to mark an EVerest module, or a single configuration entry
of a module, as deprecated. A deprecation is declared in the module's
``manifest.yaml``; the manager turns that declaration into a warning at startup
and into a report for ``manager --check``, and the module reference
documentation renders it as a deprecation notice.

Read :ref:`project-deprecation-policy` first. It defines what may be deprecated,
the minimum deprecation period, and the channels a deprecation has to be
announced through. This guide covers the mechanics only.

****************
Before you start
****************

Pick the two releases that the declaration needs:

``deprecated_in``
   The release that deprecates the component. A component deprecated mid-cycle
   on ``main`` counts as deprecated in the next stable release.

``earliest_removal``
   The earliest release the component may be removed in. The deprecation policy
   requires at least one full stable release in between, so a component
   deprecated in ``2026.10.0`` must not be removed before ``2027.04.0``.

******************
Deprecate a module
******************

Add a top-level ``deprecated`` section to the module's ``manifest.yaml``:

..  code-block:: yaml

    description: A OCPP charge point / charging station module for OCPP-J 1.6
    deprecated:
      component: OCPP (OCPP 1.6)
      deprecated_in: 2026.10.0
      earliest_removal: 2027.04.0
      migration_guide: Migrate to the Combined OCPPmulti Module
      note: >-
        Superseded by the combined OCPPmulti module, which supports
        OCPP 1.6, 2.0.1 and 2.1 in one module.

Only ``deprecated_in`` and ``earliest_removal`` are required. ``component``
defaults to the module name and is worth setting when the module name alone does
not identify what is going away. ``migration_guide`` is a single line naming the
replacement or the migration guide; ``note`` is free text for anything that does
not fit there.

The manager logs this once per configured instance of the module, before the
modules are started:

..  code-block:: text

    DEPRECATED MODULE
      component       : OCPP (OCPP 1.6)
      module id       : ocpp
      deprecated      : 2026.10.0, earliest removal 2027.04.0
      migration guide : Migrate to the Combined OCPPmulti Module
      note            : Superseded by the combined OCPPmulti module, ...

************************
Deprecate a config entry
************************

A ``deprecated`` section can also sit on a single entry of the ``config``
section, both for the module config and for the config of a provided
implementation:

..  code-block:: yaml

    config:
      lock_connector_in_state_b:
        description: >-
          Indicates if the connector lock should be locked in state B.
        type: boolean
        default: true
        deprecated:
          component: EvseManager config entry 'lock_connector_in_state_b'
          deprecated_in: 2026.10.0
          earliest_removal: 2027.04.0
          when: false
          migration_guide: >-
            Remove the entry and set unlock_when_deauthorized to true instead

The warning is only logged when the entry is actually configured, never when it
just falls back to its default. ``when`` narrows this further to a single value,
which is what the example above uses: the entry itself stays supported, only
switching it off is deprecated. Without ``when``, any configured value warns.

..  note::

    An EVerest instance that reads its configuration from the config database
    rather than from YAML no longer knows which values were written by an
    integrator. There, an entry counts as configured when it has no default, or
    when its value differs from the default.

**************************
What you do not have to do
**************************

Nothing else in the module changes:

- **No code change.** The manager reads the manifest; the module itself is not
  involved. This also means the mechanism works the same for modules written in
  C++, Python, JavaScript and Rust.
- **No code generation.** ``ev-cli`` does not read the ``deprecated`` section,
  so there is no ``ev-cli mod update`` run and no regenerated file to commit.
  The manifest is read at runtime, so a rebuild is only needed to install the
  changed manifest into the prefix you run from.
- **No hand-written warning.** If the module logged its own deprecation warning
  before, delete it, so the deprecation is not reported twice.

**************************
The remaining manual steps
**************************

The manifest covers the runtime warning. The deprecation policy asks for two
more things, which stay manual:

1. Add a row to the :ref:`project-deprecation-index`, with the same two releases
   and a link to the migration guide.
2. Write the migration guide, or extend an existing one, and mention the
   deprecation in the release notes of the release that introduces it.

**********************
Verify the deprecation
**********************

Validate a configuration that uses the module and read the report:

..  code-block:: bash

    ./build/dist/bin/manager --check --config <full path to config yaml>

``--check`` reports every deprecation of every configured module and exits
``0``; a deprecation is not an error. Starting the same configuration normally
logs the same blocks right before the modules are spawned.

To check the rendered documentation, build the docs and open the module's
reference page: the module deprecation appears as a deprecation notice under the
module description, and a deprecated config entry is marked in the
auto-generated configuration reference.

********************
Additional Resources
********************

- :ref:`project-deprecation-policy`
- :ref:`project-deprecation-index`
- :ref:`project-breaking-changes`
