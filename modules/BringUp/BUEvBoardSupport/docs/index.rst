.. _everest_modules_handwritten_BUEvBoardSupport:

BUEvBoardSupport
================

Interactive bring up helper for the ``ev_board_support`` interface: a
terminal UI that displays ``bsp_event`` and ``bsp_measurement`` and offers
the interface's commands (``set_cp_state``, ``enable``, ``allow_power_on``,
``diode_fail``, ``set_ac_max_current``, ``set_three_phases``) as buttons.
The EV-side counterpart of BUEvseBoardSupport.

On a ChargeBridge MCS board in the EV role (``ev_board_support_API`` +
``pionix_chargebridge`` with ``charge_bridge.type: EV``), the CP idioms map
onto IEC 61851-23-3 basic signalling:

* ``set_cp_state C`` - the readiness claim (``mcs_sv3_request_from_cp_command``):
  with the EVSE offering a session (S S3 closed) the firmware runs the S V3
  sequencing, lock interlock first. Issued while the EVSE is idle (S S3
  open), the only sensible meaning is "wake it up" and the firmware runs the
  Figure CC.114 wake toggle (>= 1 s wait, 2 s pulses, M <= 3) instead.
* ``set_cp_state B`` - withdraw the claim: S V3 opens. This is also what
  clears the firmware's ``sustain_blocked`` latch after an aborted wake
  pulse - the claim must be withdrawn at least once before S V3 closes
  again.
* ``set_cp_state A`` / ``E`` - simulate unplugged / error on the EV's own
  view; the physical ID line is what the EVSE actually sees.

``allow_power_on`` gates the EV-side power stage as on CCS. PP/ampacity and
phase commands have no MCS meaning; they are kept for CCS boards.
