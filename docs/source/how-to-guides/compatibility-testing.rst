.. _htg-compatibility-testing:

#######################################
Checking Compatibility Across Versions
#######################################

This guide shows how to check that a version of EVerest keeps the
:ref:`public API <project-release-and-versioning-public-api>` of an earlier one, as a maintainer
before merging a change and as an integrator before upgrading a deployment. The checks implement the
definitions of :ref:`project-breaking-changes`; they live under ``tests/compat`` in the repository.

The contract diff
=================

The contract diff (``tests/compat/contract``) compares the public API contract of two git refs
without building anything: the AsyncAPI specifications, the JSON RPC API types, the module names and
configuration options of every manifest, the OCPP 1.6 profile schemas and the device model component
configuration, the SQL migrations of every persistent store, the command line options of the
manager and the schemas of its configuration and settings files. Each difference is classified as
breaking, non-breaking or unclassified following :ref:`project-breaking-changes`. It runs on every
pull request against the merge base.

Where the checks are enforced
=============================

``main`` is developed towards the next release line and may break what the latest release
documents as deprecated or experimental. A stable branch only receives patch releases, which never
break the public API. The checks follow that:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Event
     - Contract diff
   * - Pull request to ``main``
     - Against the merge base. Only changes to what the latest release ships count, with the
       exceptions that release documents.
   * - Scheduled run on ``main``
     - Also against the latest release tag.
   * - Pull request to ``stable/<line>``
     - Against the merge base and against the latest release of the line. Only experimental components may change.

The comparison with the release tag does not run on pull requests to ``main``. A release is cut
from its stable branch, so it can contain a change that ``main`` does not have yet. The comparison
reports such a change as removed, whatever the pull request changes. The scheduled run shows what
still has to be forward-ported or announced before the next release.

A tree states the release it becomes as its CMake project version. A deprecated component may only
be removed from the release its entry in the deprecation index names as the earliest removal, so the
version on ``main`` is raised before such a removal.

Running the contract diff
=========================

.. code-block:: bash

   pip install pyyaml
   python3 tests/compat/contract/compat_contract.py check --base 2026.10.0 --tree .

``--base`` is any git ref, ``--tree`` the source tree to compare it with (``--head`` takes a ref
instead). The report groups findings by classification and names each one by a pointer such as
``modules/EvseManager/config/connector_id/default``. The exit code is 1 when a breaking or
unclassified change remains. A change is unclassified when it is in a construct the rules do not
know; it is resolved by adding a rule.

A breaking change is accepted when the release documents the component as an exception, following
the :ref:`deprecation policy <project-deprecation-policy>`. The
:ref:`deprecation index <project-deprecation-index>` and the
:ref:`experimental index <project-experimental-index>` are rendered from ``deprecations.yaml`` and
``experimental.yaml`` in ``docs/source/project/releases``, where an entry names the parts of the
public API it covers. The contract diff reads these files from the release it compares with:

- A change to a component the release lists as experimental is accepted.
- A change to a component the release lists as deprecated is accepted once the checked tree has
  reached the earliest removal release of the entry. The release a tree becomes is its CMake
  project version unless ``--target-release`` names another.
- A component that was deprecated and removed between two releases is reported as breaking,
  because no release announced the removal.

``tests/compat/contract/allowlist.yaml`` accepts changes that the rules call breaking although no
deployment is affected. An entry names the finding by pointer and states the reason, and a pull
request that adds one needs the ``EVerestAPI breaking change`` label.

Compared with another ref, such as the merge base of a pull request, ``--released`` names the
release that deployments run. Its registry applies, and only changes to what it ships count:
something added after the release may change freely until a release ships it.

.. code-block:: bash

   python3 tests/compat/contract/compat_contract.py check --base origin/main --tree . \
       --released latest-release

For a stable branch pass ``--patch-release`` and use the ``.0`` release of the line as the base,
since patch releases never break the public API: only experimental components may change.

.. code-block:: bash

   python3 tests/compat/contract/compat_contract.py check --base 2026.10.0 --tree . --patch-release

Checking your own deployment
============================

The contract diff answers whether the configuration options, module names, API channels and
messages a deployment uses still exist and mean the same; integrators can run it against the release
they run and the release they plan to upgrade to:

.. code-block:: bash

   python3 tests/compat/contract/compat_contract.py check --base 2026.02.1 --head 2026.10.0 --report upgrade.md
