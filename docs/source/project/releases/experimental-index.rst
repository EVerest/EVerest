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

The manager options below are documented in full in
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
