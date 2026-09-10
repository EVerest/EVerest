.. _everest_modules_handwritten_McsEvDataLink:

..  This file is included in the auto-generated HTML page for the module.

*******************************************
McsEvDataLink
*******************************************

EV-side data link supervision for the Megawatt Charging System (MCS) according to
ISO 15118-10:2025. The counterpart to :ref:`McsDataLink
<everest_modules_handwritten_McsDataLink>`, which does the same job on the EVSE
side.

What it is
==========

For MCS, single-pair ethernet (SPE, 10BASE-T1S) replaces HomePlug Green PHY power
line communication. **There is no SLAC** - no CM_SET_KEY, no sounding, no
CM_SLAC_MATCH, no logical network to join. What ISO 15118-10 keeps are the
**D-LINK service primitives** of ISO 15118-3, which is what the EVerest
``ev_slac`` interface carries, so this module provides ``ev_slac`` and
``EvManager`` needs no modification.

The interface name is a misnomer here and the states read differently:

=================  ==========================================================
``UNMATCHED``      No data link. Also where the EV waits after a failed
                   communication initialization - see the retry section.
``MATCHING``       The EV stack asked for the link and it is not up yet.
                   Nothing is being negotiated; the setup deadline is running.
``MATCHED``        The link is up and D-LINK_READY has been issued.
=================  ==========================================================

Unlike the EVSE side, these three are also the module's *only* states: there is
no paused state and no restart-guard state, because ``ev_slac`` has no pause or
error command to enter them from.

Behaviour
=========

Establishing the link
---------------------

**V2G10-028** has the EVCC initiate the data link *after plug-in detection and
before state B* - earlier than the EVSE side, and matching IEC 61851-23-3
Table CC.110 t0. **V2G10-030** then requires D-LINK_READY on state B/B_AUX *and*
a successful link, in either order - the exact mirror of the EVSE's V2G10-023.

This module sees two of those inputs:

* the **carrier** of the configured device, and
* ``trigger_matching``, which is the EV stack saying "I want the data link now".

``trigger_matching`` is therefore the basic-signalling half of V2G10-030. On MCS
the EV stack is what verifies the three t1 preconditions (link established, EVSE
closed S S3, CE state B/B_AUX) - the CE reading is not visible to this module -
so it calls ``trigger_matching`` when its own conditions are met, and this module
supplies the link half.

* ``trigger_matching`` while the carrier is already up matches immediately. This
  is the common case, not an edge case: SPE is point to point and V2G10-028 has
  the link established before state B, so the PHY is usually operational by the
  time the stack asks. Waiting for a carrier edge here would wait forever.
* ``trigger_matching`` without carrier goes to ``MATCHING`` and starts the
  **communication setup deadline** (``link_detect_timeout_ms``). Carrier-up
  within that window means ``MATCHED`` and D-LINK_READY.
* Expiry means communication initialization **FAILED** (V2G10-054) and the state
  returns to ``UNMATCHED``.

ISO 15118-10 bounds that deadline twice with the same 4 s: ``TT_EV_link_detect``
(Table 8) for detecting a valid link, and ``T_conn_resume`` (V2G10-035) for
finishing communication setup after B/B_AUX was detected - which also covers the
wake-up case of V2G10-043. One timer serves both.

No retry loop
-------------

This is the sharpest difference from the EVSE side. **V2G10-039**: on a
D-LINK_ERROR the EV's comm node goes to state B within ``TP_sync_leave`` (400 ms)
and **waits for a new trigger**. The EV does not run the SECC's ``C_conn_retry``
loop - clause 7.5.3 makes the *EVSE* the side that relaunches, via EC→B0→B, and
the EV retries communication initialization only "after indication from the
EVSE".

So there is no retry counter, no restart-guard timer and no
``request_error_routine`` anywhere in this module. After a failure it sits in
``UNMATCHED`` until the EV stack calls ``trigger_matching`` again, which is how
the EVSE's restart indication reaches it. Anything else would be the EV
second-guessing the side that owns the restart.

Losing the link
---------------

A carrier loss or a detected liveness loss while matched is reported upward as
D-LINK_READY(no link) (**V2G10-036**) and the state returns to ``UNMATCHED``.
Nothing is retried, for the reason above.

.. warning::

   **TT_sync_repetition is not implemented on this side, and that is an open
   conformance call awaiting a ruling before HIL.** The argument, so that whoever
   rules on it does not have to reconstruct it:

   * Clause 9.2 is worded symmetrically. V2G10-056 says the communication
     initialization "shall be restarted as long as the timer TT_sync_repetition is
     not expired and EVSE **and EV** are in state B or B_AUX", and V2G10-057 says
     "the **EV and EVSE** shall wait". Read literally, the repetition applies here
     too, and McsDataLink implements it on the EVSE side.
   * Against that: the extraction's list of EV-side D-LINK requirements
     (V2G10-024…-032, -035, -036, -039) does not include -055…-058, and the one
     EV-side timing it does name for this window is V2G10-035's T_conn_resume,
     which this module implements as its single deadline.
   * And the mechanism does not port cleanly. On the EVSE side the repetition is
     bounded twice - by the window *and* by ``C_conn_retry`` - so a fast-failing
     attempt cannot spin. The EV has no retry budget to bound it with, because
     V2G10-039 is what removes it: the EV waits for the EVSE's restart indication
     rather than retrying. Implementing the repetition here would mean either an
     unbounded restart loop inside the window or inventing an EV-side budget the
     standard does not give.

   So the module does the conservative thing and does not repeat. If the ruling
   goes the other way, McsDataLink's implementation ports directly - the window is
   a timer plus one guard on the deadline-expiry row - and the budget question has
   to be answered with it. Note also that at the standard's default maxima the
   repetition is inert anyway: ``TT_EV_link_detect`` and ``TT_sync_repetition`` are
   both capped at 4 s, so the window has already closed when the first deadline
   expires.

EV sleep
--------

**V2G10-040** lets the EV sleep in state B with its communication module switched
**off entirely** - stronger than the EVSE's B0-with-low-power (V2G10-041). A
comm module that is off has no carrier, so a sleep is indistinguishable from a
link failure at this module's inputs, and ``ev_slac`` has no pause command to
tell it apart.

The module therefore reports what it observes: the link is down, so
D-LINK_READY(no link) and ``UNMATCHED``. That is truthful in both cases, and the
EV stack that decided to sleep is the one that knows which it is. On waking, the
stack calls ``trigger_matching`` again and the setup deadline enforces the
V2G10-043 bound. Contrast the EVSE side, where ``dlink_pause`` exists and
D-LINK_READY must *survive* the pause - there is deliberately no analogue of that
here, because there is no way to ask for one.

The carrier contract
====================

Identical to the EVSE side, and implemented by the same library code
(``everest::lib::io::netlink``): an ``AF_NETLINK``/``NETLINK_ROUTE`` socket
subscribed to ``RTMGRP_LINK``, keyed on **``IFF_LOWER_UP``** in the ``ifi_flags``
of ``RTM_NEWLINK``. Not ``operstate`` (a TAP does not maintain one) and not
``IFF_RUNNING`` (a TAP created carrier-off is announced once with it set, because
linkwatch corrects the operstate about a second later). See
``everest/io/netlink/link_tracker.hpp`` for the full contract and the two timing
consequences it carries, including that carrier-up is **not** the same as "IPv6
usable" - the kernel re-runs duplicate address detection on the edge.

The configured device need not exist at startup; the module waits for the
``RTM_NEWLINK`` that announces it. That stops being harmless once the EV asks for
the link: a ``trigger_matching`` while the device does not exist raises
``generic/CommunicationFault``, cleared when it appears.

EV liveness
-----------

With ``neighbor_liveness`` enabled the module also watches the NUD state of the
device's neighbour entries, exactly as the EVSE side does and for the same
reason: 10BASE-T1S gives no peer signal at the PHY level (autonegotiation is
mandated off, V2G10-026/-027), so a carrier saying "my PHY is operational" can
outlive the peer. Here the peer is the **SECC**.

The judgement is ``everest::lib::io::netlink::peer_liveness``, shared with
McsDataLink: an empty neighbour table is no opinion rather than a loss, a single
``NUD_FAILED`` only counts while nothing else on the device is alive, and even
then only after ``liveness_grace_ms``.

``ev_mac_address`` (``publish_connector_mac``) is published once per link when a
neighbour becomes ``NUD_REACHABLE``. Since the peer is the SECC, this is the
charging connector's MAC address, which is exactly what the interface documents
the variable to carry - the mirror of McsDataLink publishing the EV's.

Interface gaps
==============

``ev_slac`` is deliberately smaller than the EVSE-side ``slac``: commands are
``reset`` (no argument) and ``trigger_matching``, variables are ``state``,
``dlink_ready`` and ``ev_mac_address``. Several things the standard asks of the
EV side cannot be expressed through it. **None of these is worked around by
extending the interface** - that is an upstream conversation - so they are
recorded here instead.

D-LINK_PAUSE and D-LINK_TERMINATE (V2G10-031/-032)
   No commands exist. Per the integration plan these EV-side duties are driven by
   the EV HLC stack rather than through this interface, so the module never learns
   about them. The consequence for sleep is described above.

D-LINK_ERROR (V2G10-039)
   No command exists, so the module cannot be *told* that the HLE declared the
   link dead. It implements the observable half correctly by construction - there
   is no retry machinery, and it waits in ``UNMATCHED`` for the next trigger -
   but the 400 ms ``TP_sync_leave`` reaction to an explicit D-LINK_ERROR is not
   something it can perform.

   Two things are missing for that, and adding the command would only fix the
   first. The module never sees the request; and even if it did, the reaction
   V2G10-039 asks for is "change to state B", which is a basic-signalling
   actuator - the EV's S V3 line - that lives in the BSP and the firmware, not
   here. This module has no way to drive CE state at all. All it could ever do on
   a D-LINK_ERROR is tear down its own published state, which is exactly what
   ``reset`` already does. So the 400 ms duty belongs to whoever owns S V3, and
   the interface gap is not the thing standing in its way.

   If the EV stack needs the module's state torn down at that point, ``reset`` is
   the signal to use.

Withdrawing the basic-signalling condition
   There is no ``leave_bcd`` counterpart. The module learns that the EV is no
   longer requesting the link only from ``reset`` (or from the carrier
   disappearing). ``EvManager`` does call ``reset`` before every matching attempt,
   so this is harmless in practice, but a consumer that never calls ``reset``
   would leave the module believing the request still stands. Consumers should
   call ``reset`` on unplug.

``trigger_matching``'s return value
   The interface documents it as "False if the transition was unexpected and
   cannot be handled by the SLAC state machine". Answering that faithfully would
   mean running the state machine on the caller's thread - which the CCS
   ``EvSlac`` does, and which this module avoids on purpose, since keeping the
   machine single-threaded is what makes it testable and race-free - or blocking
   the framework thread on a round trip through the event loop, which risks
   deadlocking against the fault path that takes the same monitor. So the boolean
   here means **"the command was accepted for processing"**: false only when it
   provably will not be (shutdown in progress, or a command backlog deep enough
   to prove the loop is not running). A trigger the machine then ignores because
   it was already matching still returns true. This is strictly more informative
   than ``EvSlac``, which returns an unconditional true, and the caller's real
   feedback is the ``state`` variable it already subscribes to.

Deltas from McsDataLink
=======================

Both modules supervise the same technology on the same mechanism; the
differences are all consequences of which side of the link they sit on and how
much of the interface exists.

============================  ==========================================  ==========================================
Aspect                        McsDataLink (EVSE, ``slac``)                McsEvDataLink (EV, ``ev_slac``)
============================  ==========================================  ==========================================
Basic-signalling input        ``enter_bcd`` / ``leave_bcd``               ``trigger_matching`` only; withdrawal
                                                                          implied by ``reset``
Link established              after state B is applied                    after plug-in, **before** state B
                              (V2G10-023)                                 (V2G10-028, V2G10-030)
Retry on failure              ``C_conn_retry`` budget, plus the           **none** - waits for the EVSE's restart
                              CC.5.2.3.2 restart request                  indication (V2G10-039)
Restart request upward        ``request_error_routine``                   no such variable, and nothing to request
Setup deadline                ``TT_EV_link_detect`` (4 s) plus            one deadline: ``TT_EV_link_detect`` /
                              ``TT_sync_repetition``                      ``T_conn_resume`` (V2G10-035); no
                                                                          repetition - see the warning above
Pause                         ``dlink_pause``; stays MATCHED, keeps        no command; a sleeping comm module is
                              D-LINK_READY (V2G10-041)                    reported as a link loss (V2G10-040)
Explicit teardown             ``dlink_terminate``                         ``reset`` only
Internal states               5 (adds ``paused``, ``retry_wait``)         3 - the published ones
Transition table              hierarchical, 5 outer + 17 inner rows       flat, 10 rows
``ev_mac_address`` peer       the EV                                      the SECC / charging connector
Liveness                      identical (shared ``peer_liveness``)        identical
Carrier contract              identical (shared ``link_tracker``)         identical
============================  ==========================================  ==========================================

Configuration
=============

===============================  =========  ==========================================
``device``                       cb_plc     Network device carrying the MCS SPE link.
                                            Need not exist at startup.
``link_detect_timeout_ms``       4000       Communication setup deadline:
                                            TT_EV_link_detect / T_conn_resume, both
                                            capped at 4000 by Table 8.
``neighbor_liveness``            true       Also supervise the link through the kernel
                                            neighbour table (NUD).
``liveness_grace_ms``            1000       Debounce for the above. 0 acts at once.
``publish_connector_mac``        true       Publish ``ev_mac_address`` (the SECC's).
===============================  =========  ==========================================

Errors
======

``generic/CommunicationFault`` is raised when the rtnetlink socket cannot be
opened or fails fatally, and when ``trigger_matching`` arrives while the
configured device does not exist. It is cleared when the device appears. A device
that is merely absent while the EV is not asking for a link is **not** an error.

Implementation notes
====================

* The netlink machinery is shared with McsDataLink and lives in
  ``lib/everest/io`` under ``everest::lib::io::netlink`` - ``route_parser``,
  ``link_tracker``, ``neighbor_table``, ``peer_liveness`` and ``device_watcher``.
  The shutdown handshake is ``everest::lib::util::LifecycleStateT``. This module
  adds no supervision code of its own; it is the second consumer of all of it.
* The link lifecycle is a ``boost::msm`` state machine
  (``main/link_state_machine.cpp``) whose actions perform no I/O: they append
  effect requests that the controller executes afterwards, which is what makes
  every transition testable without a socket or a timer.
* **The table is flat, unlike McsDataLink's.** The EVSE side is hierarchical
  because 23 of its 39 rows were four session-wide events repeated once per
  state, which is what a composite state exists to collapse. Here the only
  repeated event is ``reset``, at three rows out of ten, so a composite plus an
  explicit entry point would trade two rows for a second table and a shared-data
  indirection. If ``ev_slac`` ever gains the pause/error/terminate commands its
  EVSE counterpart has, this table grows the same repeats and McsDataLink is then
  the pattern to follow.
* There is **no worker thread**. The event loop runs inside ``ready()``, on the
  thread the framework spawns for the global-ready message and joins last during
  teardown. ``shutdown()`` runs on a different framework thread, stops the loop
  and waits for it to return before the module is destroyed.
* Interface commands arrive on framework threads and only ever append to a
  monitor-guarded FIFO and notify an ``event_fd``. The state machine, the liveness
  judgement and the timers are touched by exactly one thread. The FIFO preserves
  command order, which matters here: ``EvManager`` calls ``reset`` and then
  ``trigger_matching``, and swapping them would leave the module unmatched with
  nothing pending.

If not run as root, this module needs no capabilities: subscribing to rtnetlink
link and neighbour notifications and dumping them is unprivileged.

Debugging notes
===============

* ``ip monitor link`` on the configured device shows what this module keys on.
  Look for ``LOWER_UP``, not ``state UP`` or ``RUNNING``.
* Carrier-down can be reported up to about a second late, because the kernel's
  linkwatch work is rate-limited to roughly one run per second. The seconds-scale
  ISO 15118-10 timers tolerate this.
* ``ip neigh show dev <device>`` shows the NUD states the liveness judgement reads.
* The module logs every state change, every D-LINK primitive and every timer
  verdict at info or warning level.

References
==========

ISO 15118-10:2025 V2G10-024 to V2G10-032, V2G10-035, V2G10-036, V2G10-039,
V2G10-040, V2G10-043, V2G10-048, V2G10-054, Table 8; IEC 61851-23-3 (CDV)
Table CC.105, Table CC.107, Table CC.110, CC.5.2.4.
