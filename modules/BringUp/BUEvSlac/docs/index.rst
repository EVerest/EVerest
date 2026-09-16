.. _everest_modules_handwritten_BUEvSlac:

BUEvSlac
========

Interactive bring up helper for the ``ev_slac`` interface: a terminal UI that
displays the provider's published variables (``state``, ``dlink_ready``,
``ev_mac_address``) and offers the two commands (``trigger_matching``,
``reset``) as buttons. The EV-side counterpart of BUSlac.

Works against any ``ev_slac`` provider. Against McsEvDataLink it drives the
MCS (ISO 15118-10) EV-side data link by hand: ``trigger_matching`` is what
the EV stack would issue on plug-in, MATCHED appears when the SPE link
carrier is up, and the connector MAC arrives via neighbour discovery once
the EVSE answers on IP level. Note that per V2G10-039 the EV side never
retries on its own - after a failed or lost link it returns to UNMATCHED
and waits for the next ``trigger_matching``.
