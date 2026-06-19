.. _everest_modules_handwritten_EvseSlacNeo :

.. ===================
.. EvseSlacNeo 
.. ===================

This is an implementation of the ISO 15118-3 EVSE side SLAC protocol.
The general flow of operation will be like this:

- start of operation begins with a control pilot transition from state
  A, E or F to Bx, Cx or Dx. This is indicated by the EvseManager by calling
  the commands enter_bcd/leave_bcd.
- Once started, it waits for a CM_SLAC_PARM_REQ from the EV side to start the SLAC session.
- In case of success, SLAC finishes with a CM_SLAC_MATCH.CNF, which sends a new NMK to the EV side.
  The EV then joins the logical network and the two modems are paired. A dlink_ready(true) is published
  to signal to the EvseManager that the low level PLC link is ready for communication.

If not run as root user, this module requires the capability CAP_NET_RAW.

Set-key handling can run in two modes:

- legacy_single_attempt: send one CM_SET_KEY.REQ and accept any CM_SET_KEY.CNF.
- retry_confirmed: retry on timeout/failure and only promote the pending NMK after a successful CM_SET_KEY.CNF.

The module replies to CM_VALIDATE.REQ with a failed CM_VALIDATE.CNF, matching the legacy EVSE SLAC behavior.

Todo
====

- make use of the enable flag in the reset command or drop it, if not needed
- review whether PLC chip reset should also be driven after unplug or failed SLAC sessions
