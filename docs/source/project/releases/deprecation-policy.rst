.. _project-deprecation-policy:

##############################
Deprecation Policy for EVerest
##############################

This document describes how EVerest handles the deprecation of public API
components. It complements the :ref:`project-release-and-versioning` and
:ref:`project-breaking-changes` documents and applies to all components covered
by the EVerest public API.

********
Overview
********

EVerest follows a pragmatic deprecation approach: public API components are not
removed without warning, and integrators always have at least one stable release
cycle to migrate before any breaking change occurs.

*****
Scope
*****

This policy applies to all components of the EVerest public API:

- External AsyncAPIs
- Energy Management JSON RPC API
- Configuration and storage contracts (EVerest module configuration, OCPP
  configuration)

Internal interfaces, types, and the configuration/storage contracts of Dummy
and Simulation modules are explicitly excluded and may be changed or removed
without a deprecation period.

**********************
Deprecation Lifecycle
**********************

A deprecated public API component passes through three phases:

1. **Active**: The component is fully supported. No deprecation notice exists.
2. **Deprecated**: The component still works as documented, but is marked as
   deprecated. A replacement (if applicable) is documented, and a planned
   removal release is announced.
3. **Removed**: The component is no longer available. Its removal is a breaking
   change and follows the rules defined in :ref:`project-breaking-changes`.

**************************
Minimum Deprecation Period
**************************

A public API component **must not be removed without first being deprecated**.
Removal from one stable release to the next without a preceding deprecation
period is not permitted.

Once deprecated, the component must remain functional in at least one stable
release before it can be removed. For example, a component deprecated in
``2026.09.0`` cannot be removed before ``2027.03.0``.

A component deprecated mid-cycle on ``main`` counts as deprecated in the next
stable release and must therefore not be removed before the one after that.

Longer deprecation periods may be applied for widely used or
integration-critical components.

************************
Active Deprecation Index
************************

All currently active deprecations are tracked in the
:doc:`Active Deprecation Index <deprecation-index>`. Each entry records the
deprecated component, the release in which it was deprecated, the earliest
release in which it may be removed, and a link to or a description of 
the corresponding migration guide.

Maintainers must ensure that an entry is added when introducing a deprecation.

******************************
How Deprecations Are Announced
******************************

Every deprecation must be announced through all of the following channels:

- **Release notes**: The deprecation is listed in the release notes of the
  version in which it is introduced, including the planned removal release and
  a link to the migration guide.
- **Runtime warnings**: Where reasonable, EVerest emits a clearly identifiable
  warning when a deprecated component is used (e.g. a log message at startup
  for a deprecated configuration option or when a deprecated operation is invoked).
- **Migration guide**: A short migration note describes the replacement and
  the recommended upgrade path, and is linked from the release notes entry.

**********************
What Can Be Deprecated
**********************

Examples of valid deprecations include:

- An AsyncAPI channel, operation, or message field superseded by a new
  equivalent
- An EVerest module that is replaced by an improved version
- A configuration option that has been renamed or replaced
- A JSON RPC method that has been renamed or replaced

Deprecation is the standard path for any planned breaking change to the public
API.

***************************************
Stability Guarantees During Deprecation
***************************************

While a component is deprecated:

- Its observable behavior must not change in incompatible ways. Bug fixes and
  security fixes are allowed.
- It must continue to work in all patch releases of the stable line in which
  it was deprecated.
- The deprecation itself is **not** considered a breaking change. It is a
  non-breaking annotation that signals intent.

The actual removal of a deprecated component **is** a breaking change and must
only occur in a new stable release (``yyyy.mm.0``), never in a patch release.

**********
Exceptions
**********

This policy does not cover every use case and will evolve as the project
grows. The EVerest maintainers reserve the right to deviate from this policy
where circumstances require it. In any case, the priority is to avoid unexpected
breaking changes for integrators. Any exception must be documented in the release
notes of the affected release.

********************
Additional Resources
********************

- :ref:`project-release-and-versioning`
- :ref:`project-breaking-changes`

For questions about a specific deprecation or to propose one, please contact
the EVerest maintainers via Zulip.
