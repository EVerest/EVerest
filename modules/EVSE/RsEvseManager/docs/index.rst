.. _everest_modules_handwritten_RsEvseManager:

.. ************************
.. RsEvseManager
.. ************************

:ref:`Link <everest_modules_RsEvseManager>` to the module's reference.

The module ``RsEvseManager`` is a Rust implementation of
:ref:`EvseManager <everest_modules_handwritten_EvseManager>`. It replaces that
module rather than sitting beside it: it provides the same five interfaces
(``evse_manager``, ``energy``, ``auth_token_provider``, ``uk_random_delay``,
``dc_external_derate``), takes the same configuration keys, and requires the same
hardware and protocol modules. Read the EvseManager documentation for what an EVSE
manager does and how it behaves; this page covers only what is specific to the
Rust module.

Selecting it
============

A config selects this module by naming it as the module type where it would
otherwise name ``EvseManager``. Nothing else in a config changes, because the
configuration keys are the same.

``config/config-sil-rs-evse-manager.yaml`` (AC) and
``config/config-sil-dc-rs-evse-manager.yaml`` (DC) are the stock SIL configs with
that one substitution made, and serve as worked examples.

Building it
===========

The module is built only when CMake is configured with
``-DEVEREST_ENABLE_RS_SUPPORT=ON``. That option defaults to ``OFF``. The module's
``README.md`` covers building it, running it, and its gates.

.. warning::

   No CI job sets that option, so this module is never compiled, tested or linted
   by CI. It is checked by the gates in its own ``scripts/`` directory and nowhere
   else, and so carries none of the assurance the rest of the tree gets from CI.

Design documentation
====================

Three Markdown documents in this directory carry the design. They are not rendered
into these docs; read them at ``modules/EVSE/RsEvseManager/docs/``.

* ``architecture.md`` — the layering, the effect model, the concurrency domains,
  the charge modes, and every deliberate divergence from and preserved defect of
  the C++ module, each recorded with the C++ site it was read from.
* ``effect-ordering-timing.md`` — which effects are ordered against which, and why
  the window in which a stale actuation can still be delivered has no finite bound.
* ``shutdown-contract-scope.md`` — the bounded options for closing that window, and
  why none of them is in this module.
