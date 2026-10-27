.. _everest_modules_handwritten_evse_board_support_API:

.. *******************************************
.. evse_board_support_API
.. *******************************************

The complete API specification can be found in the

``docs/source/reference/EVerest_API/evse_board_support_API.yaml``

file in the source repository, or in the `AsyncAPI HTML documentation <../../../../api/evse_board_support_API/index.html>`_ automatically generated from it.

Capabilities
============

Publish ``capabilities`` once as soon as they are known after start and before
the first charging session. EVerest offers no current until it received them: on
a plug-in it requests a PWM duty cycle of 100 % (state X1) and waits. The phase
count used by the energy management is initialised from
``max_phase_count_import``. Publish the capabilities again whenever a limit
changes at runtime, for example when the maximum current drops because the
hardware gets too hot; a changed current limit reaches the PWM within a few
seconds, also during charging.

Error mapping
=============

The module provides the ``evse_board_support``, ``connector_lock`` and
``ac_rcd`` interfaces but takes one flat error type list over the API. It picks
the implementation to raise or clear an error on by the error name:

.. list-table::
   :header-rows: 1

   * - EVerest error type
     - API error names
   * - ``evse_board_support/<name>``
     - DiodeFault, VentilationNotAvailable, BrownOut, EnergyManagement,
       PermanentFault, MREC2GroundFailure, MREC3HighTemperature,
       MREC4OverCurrentFailure, MREC5OverVoltage, MREC6UnderVoltage,
       MREC8EmergencyStop, MREC10InvalidVehicleMode, MREC14PilotFault,
       MREC15PowerLoss, MREC17EVSEContactorFault, MREC18CableOverTempDerate,
       MREC19CableOverTempStop, MREC20PartialInsertion, MREC23ProximityFault,
       MREC24ConnectorVoltageHigh, MREC25BrokenLatch, MREC26CutCable,
       TiltDetected, WaterIngressDetected, EnclosureOpen, VendorError,
       VendorWarning, CommunicationFault
   * - ``connector_lock/<name>``
     - ConnectorLockCapNotCharged, ConnectorLockUnexpectedOpen,
       ConnectorLockUnexpectedClose, ConnectorLockFailedLock,
       ConnectorLockFailedUnlock, MREC1ConnectorLockFailure
   * - ``ac_rcd/<name>``
     - Selftest, DC, AC

The ``connector_lock`` and ``ac_rcd`` interfaces also define VendorError and
VendorWarning, and ``ac_rcd`` defines MREC2GroundFailure; through this API those
names always address the ``evse_board_support`` implementation. EvseManager
keeps charging only through MREC3HighTemperature, MREC18CableOverTempDerate and
VendorWarning; every other error makes the EVSE inoperative until it is cleared.
``clear_error`` must repeat the sub type of the ``raise_error``.
