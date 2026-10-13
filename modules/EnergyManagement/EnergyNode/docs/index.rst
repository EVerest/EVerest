.. _everest_modules_handwritten_EnergyNode:

.. ===================
.. EnergyNode
.. ===================

The EnergyNode module is usually used in conjunction with the **EnergyManager** module.
See the :ref:`documentation <everest_modules_EnergyManager>` of the latter for a detailed explanation of energy management.

Phase rotation
==============

The ``phase_rotation`` configuration option corrects for a physical L1/L2/L3
wiring rotation at this node's powermeter. When set, the per-phase
voltage/current/power/energy/VAR values of a powermeter reading are remapped
before they are used in the energy flow request, so that the reported phases
align with the actual grid phases.

We use the OCPP phase rotation notation for this:
R can be identified as phase 1 (L1), S as phase 2 (L2), T as phase 3 (L3).

Allowed values:

- ``RST`` (default) -- no rotation; reported L1/L2/L3 already matches grid L1/L2/L3.
- ``STR`` -- reported L1 is grid L2, reported L2 is grid L3, reported L3 is grid L1.
- ``TRS`` -- reported L1 is grid L3, reported L2 is grid L1, reported L3 is grid L2.