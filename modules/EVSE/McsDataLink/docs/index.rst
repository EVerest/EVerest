.. _everest_modules_handwritten_McsDataLink:

..  This file is included in the auto-generated HTML page for the module.

*******************************************
McsDataLink
*******************************************

Data link supervision for the Megawatt Charging System (MCS) according to
ISO 15118-10:2025.

What it is
==========

For MCS, single-pair ethernet (SPE, 10BASE-T1S) replaces HomePlug Green PHY
power line communication. **There is no SLAC.** The matching procedure of
ISO 15118-3 does not exist: no CM_SET_KEY, no sounding, no CM_SLAC_MATCH, no
logical network to join. Two peers on a point-to-point PLCA segment either have
a working ethernet link or they do not.

What ISO 15118-10 does keep, deliberately unchanged, are the **D-LINK service
primitives** of ISO 15118-3 (Tables 2 to 5): D-LINK_READY.indication,
D-LINK_TERMINATE.request, D-LINK_ERROR.request, D-LINK_PAUSE.request. Those are
exactly what the EVerest ``slac`` interface carries, which is why this module
provides ``slac`` and ``EvseManager`` needs no modification at all - its
``slac`` requirement is already optional and every command and variable maps
one to one.

The interface name is therefore a misnomer here and the states read a little
differently:

=================  ==========================================================
``UNMATCHED``      No data link, and none being established.
``MATCHING``       An EV is present per basic signalling and the module is
                   waiting for the SPE link to come up. Nothing is being
                   negotiated; TT_EV_link_detect is running.
``MATCHED``        The link is up and D-LINK_READY(link) has been issued.
=================  ==========================================================

``dlink_ready`` is the load-bearing output. The ISO 15118-20 stack in
``lib/everest/iso15118`` starts its communication-setup timeout on it and its
SDP server ignores every request until it is true, so without this module (or
another provider of ``slac``) an MCS session never starts.

The carrier contract
====================

The module supervises the link through the **carrier of a network device**,
watched over an ``AF_NETLINK``/``NETLINK_ROUTE`` socket subscribed to
``RTMGRP_LINK``. Anything that is a netdev works:

* a Chargebridge TAP device configured with ``plc.carrier: firmware``, where the
  firmware-reported SPE PHY status is mirrored onto the TAP carrier via
  ``TUNSETCARRIER``; or
* a real SPE netdev whose driver reports carrier itself.

The signal is **``IFF_LOWER_UP`` in the ``ifi_flags`` of ``RTM_NEWLINK``**. This
is not interchangeable with the alternatives, and picking either of them is a
real bug:

* Not ``operstate``. A TAP device does not maintain a meaningful operstate
  string; it starts out ``UNKNOWN``.
* Not ``IFF_RUNNING``. A TAP created carrier-off is still announced **once with
  ``IFF_RUNNING`` set**, because the operstate that flag reflects is only
  corrected by the kernel's linkwatch work about a second later. Keying on
  ``IFF_RUNNING`` produces a spurious ~1 s carrier-up on every TAP creation and
  every bridge reset.

``RTM_DELLINK`` is treated as a link-down: a device that is gone has no carrier,
whatever flags its removal message happened to carry.

Startup and a device that is not there yet
------------------------------------------

The configured device does **not** have to exist when EVerest starts. On a
Chargebridge the TAP is created by the ``pionix_chargebridge`` application at
runtime, possibly much later. This is a normal condition, logged once at info
level, not an error - the module simply waits for the ``RTM_NEWLINK`` that
announces the device.

It stops being harmless the moment an EV is connected: if ``enter_bcd`` arrives
while the device does not exist, ``generic/CommunicationFault`` is raised (and
cleared again when the device appears), so ``EvseManager`` can make the
connector inoperative instead of letting the session die on a bare 4 s timeout.
A failure to open the rtnetlink socket raises the same error.

On startup, and again after an ``ENOBUFS`` socket overrun, the module requests
an ``RTM_GETLINK`` dump so the current state is known rather than waited for -
on a stable link the next carrier edge might otherwise never come.

.. note::

   **Carrier-up is not the same as "IPv6 usable".** A carrier off-to-on edge
   makes the kernel re-run duplicate address detection, and the device's
   link-local address stays unusable for roughly one second afterwards (with the
   default ``dad_transmits=1``). Everything downstream of this module runs on
   seconds-scale ISO 15118-10 timers (TT_EV_link_detect is 4 s, T_conn_resume is
   4 s), which absorbs the delay - but nothing may assume it can send the
   instant the carrier appears.

Behaviour
=========

Establishing the link
---------------------

Per **V2G10-023** D-LINK_READY requires *both* an operational ethernet link
*and* charge-enable state B/B_AUX, in either order. SPE is point-to-point and
auto-negotiation is mandated off (V2G10-019/-021), so the PHY can perfectly well
be operational before the EV is plugged in:

* ``enter_bcd`` while the carrier is already up goes straight to ``MATCHED`` and
  issues D-LINK_READY(link). Waiting for a carrier edge here would wait forever.
* ``enter_bcd`` without carrier goes to ``MATCHING`` and starts
  **TT_EV_link_detect** (``link_detect_timeout_ms``, ISO 15118-10 Table 8, max
  4 s). Carrier-up within that window means ``MATCHED``.
* Expiry means communication initialization **FAILED** (**V2G10-054**). It is then
  **repeated** while ``TT_sync_repetition`` (``sync_repetition_ms``) has not
  expired and the EV is still present (**V2G10-056**), each repetition costing one
  attempt from the retry budget so it cannot loop; once the window closes or the
  budget is gone the initialization stops (**V2G10-058**) and the state returns to
  ``UNMATCHED``. EvseManager owns what happens to the session from there.

  .. note::

     Mind the standard's own arithmetic. ``TT_EV_link_detect`` and
     ``TT_sync_repetition`` are both capped at 4 s (Table 8), so at the default
     maxima the window has already closed when the *first* ``TT_EV_link_detect``
     expires and **no repetition ever happens**. The mechanism only does anything
     for an integrator who shortens ``link_detect_timeout_ms``, which is exactly
     what V2G10-055 to -058 imply rather than a quirk of this module.

  The repetition window opens at the communication-initialization *trigger*
  (V2G10-055), i.e. on the ``enter_bcd`` that starts ``MATCHING``. A re-match
  after a link loss is a *reconnect* governed by ``C_conn_retry``, not a
  repetition of the initial setup, and does not reopen the window.

Losing the link
---------------

A carrier loss while matched is reported upward as D-LINK_READY(no link)
(**V2G10-036**) and then, if the retry budget allows, matching is restarted
(**V2G10-037/-038**). ``UNMATCHED`` is published explicitly before ``MATCHING``
so that a consumer sees the link genuinely went down, even though both happen in
the same event-loop iteration.

A restarted ``MATCHING`` waits for a **fresh** carrier-up edge; the module never
re-derives "the carrier is still up" to shortcut it. That is what keeps a
liveness-detected loss - where the carrier never actually dropped - from
re-matching instantly and looping. Such a loss instead runs out
TT_EV_link_detect and ends as communication-initialization-FAILED.

Retry behaviour
---------------

``conn_retry_max`` implements **C_conn_retry** (V2G10-037/-038) and the restart
loop of **IEC 61851-23-3 CC.5.2.3.2** (``N <= 3``, manufacturer may differ). One
budget covers both kinds of failure - link losses and ``dlink_error`` requests.

The budget is **per EV connection and is not refunded by a successful match.**
Refunding it would mean a link that flaps between up and lost retries forever
and ``conn_retry_max`` bounds nothing. It is refilled by ``leave_bcd``,
``reset`` and ``dlink_terminate`` - the events that end or explicitly restart
the connection. When it is exhausted the module reports the link down and stays
``UNMATCHED``: state B0 territory, where EvseManager decides.

.. note::

   **V2G10-052** ("a successful communication setup shall reset all the timeout
   timers and reset the retry_counters") read literally asks for the refund this
   deliberately withholds. The counter is kept unrefunded because the alternative
   is an unbounded restart loop on flapping hardware, and because Table 8 scopes
   ``C_conn_retry`` to "communication setup retries by wakeup trigger by basic
   signalling" - the initialization phase, which ``TT_sync_repetition`` already
   bounds in time. Flagged for review rather than settled.

``dlink_error``: the host-side restart
--------------------------------------

On PLC, ``dlink_error`` is essentially a SLAC reset. On MCS it is the
**IEC 61851-23-3 CC.5.2.3.2 restart method 1**, driven from the host because the
trigger ("the EVSE wants to restart a communication session") is protocol
context the firmware does not have:

#. D-LINK_READY(no link), state ``UNMATCHED``.
#. If the retry budget allows, wait ``retry_wait_ms`` - the standard requires at
   least 3 s with S S3 open between attempts.
#. Publish ``request_error_routine``. ``EvseManager`` turns that into
   ``Charger::request_error_sequence()``, whose CP/CE toggle is the B0-to-B
   transition the restart method asks for - that toggle is the wake signal
   **for the EV**.
#. In the same step the module re-arms matching itself: straight back to
   ``MATCHED`` when the carrier still stands (both V2G10-023 conditions hold
   again), or ``MATCHING`` with TT_EV_link_detect when it does not (the
   T_conn_resume analog for the EV to bring the link back after the toggle).
   The re-arm deliberately does not reopen the TT_sync_repetition window -
   this is a ``C_conn_retry`` reconnect, not a new communication
   initialization.

   .. note::

      An earlier revision instead fell back to ``UNMATCHED`` and expected the
      error sequence to "come back as a fresh ``enter_bcd``". That is
      CCS-thinking: there the sequence drives the real CP line through state F
      and the BSP edges re-issue ``enter_bcd``. On MCS the synthesized CP
      state never leaves B while the vehicle is mated (B0 and B both map to
      CP ``B``), so the fresh ``enter_bcd`` structurally cannot occur and the
      module stayed deaf until unplug - found live on the first
      two-ChargeBridge ISO 15118-20 session attempt. Re-arming is
      honest because the basic-signalling condition demonstrably still stands:
      no ``leave_bcd`` arrived.
#. Budget exhausted: no restart is requested, the module stays ``UNMATCHED``.

During the wait a bare carrier edge is ignored on purpose: the guard time is
mandatory and the link is meant to be re-established through the B0-to-B
transition, not by the carrier flickering. ``enter_bcd`` and ``leave_bcd`` do
cancel the wait.

``reset``
---------

``reset`` tears the data link down, publishes ``UNMATCHED`` and refills the retry
budget. Its ``enable`` argument is accepted and logged but deliberately **does
not** latch matching off, even though the interface documents ``enable = false``
as "stop SLAC".

The reason is how the argument is actually used: ``EvseManager`` only ever calls
``reset(false)``, as its session-end teardown - the matching ``reset(true)`` call
is commented out (``EvseManager.cpp:409``, ``:1088``, ``:1097``). A module that
treated ``reset(false)`` as a latch would serve exactly one EV after startup and
then ignore every ``enter_bcd`` forever. ``SlacSimulator`` ignores the flag
outright and ``EvseSlacNeo`` treats ``reset(false)`` as "reset the state machine"
for the same reason; only the ``BUSlac`` bring-up tool uses the flag as
start/stop.

Pause and resume semantics
--------------------------

``dlink_pause`` (**V2G10-041**, IEC 61851-23-3 CC.5.2) is *not* a teardown. The
EVSE goes to B0 with X S2 still energized and the PHY **may power down**:

* The published state stays ``MATCHED`` and ``dlink_ready`` is **not**
  withdrawn - the link remains logically up. Nothing at all is published on
  ``dlink_pause``.
* A carrier loss while paused is **expected** and is ignored: no
  D-LINK_READY(no link), no retry spent, no state change. This is the one place
  where a carrier loss is not a failure, and it is why "paused" is a state of
  its own rather than a flag on "matched".
* A liveness loss while paused is ignored for the same reason, and the neighbour
  table is dropped when the pause starts so the powering-down PHY cannot be
  mistaken for a dead EV.
* The wake-up **re-issues D-LINK_READY(link)** per **V2G10-042/-043**, within
  T_conn_resume. Since the state never left ``MATCHED``, only ``dlink_ready`` is
  published again - it has to be, even though its value did not change.
* ``leave_bcd``, ``reset`` and ``dlink_terminate`` still tear the link down from
  the paused state.

**Leaving the paused state matters, and not only on a carrier edge.** Because
carrier and liveness supervision are suspended while paused, a module that stayed
paused after the session resumed would silently supervise nothing for the rest of
that session: a genuine link loss would never produce ``dlink_ready(false)``
(a V2G10-036 miss) and the V2G10-042 re-issue would never happen. So the pause
ends on the first evidence that the session came back:

* the carrier returning, if the PHY did power down; **or**
* a neighbour of the device becoming reachable - the EV answering.

The second path is the one that carries the realistic case today: the LAN8650
low-power mode is **not implemented** (see ``MCS_STATE_HANDLING_PLAN`` §3.9 and
follow-up #6), so a real pause keeps the carrier up and produces no wake-up edge
at all.

.. warning::

   With ``neighbor_liveness: false`` the neighbour path does not exist, so a
   carrier edge is the only way out of the paused state - and with the PHY
   low-power mode unimplemented that edge may never come. Link supervision then
   stays suspended for the remainder of the session. The module logs a warning
   when it pauses in that configuration. Keep ``neighbor_liveness`` enabled on
   any setup that uses ISO 15118-20 pause/resume until the PHY low-power mode
   lands.

EV liveness (optional)
----------------------

On the SECC side 10BASE-T1S offers **no peer signal at the PHY level**. There is
no auto-negotiation or link training (mandated off, V2G10-019/-021), and the
PLCA status bit asserts on the coordinator as soon as PLCA is enabled - it means
"my PLCA cycle is running", not "a node answered". A carrier that says "the PHY
is operational" can therefore outlive the EV.

The kernel's neighbour discovery does probe the peer, so with
``neighbor_liveness`` enabled the module additionally subscribes ``RTMGRP_NEIGH``
and watches the NUD state of the configured device's neighbour entries. It is
deliberately conservative, because a false positive kills a charging session:

* An **empty table is no opinion**, never "the EV is gone". That is the state
  before the EV has sent anything, and also after the kernel has
  garbage-collected an idle entry - removal of the last entry is explicitly not
  a loss.
* ``NUD_STALE``, ``NUD_DELAY``, ``NUD_PROBE`` and ``NUD_PERMANENT`` all count as
  **alive**. The kernel only re-probes when something wants to send, so an idle
  but healthy link legitimately sits in STALE indefinitely.
* With one refinement (bench-found on the first live run): entries sharing a link-layer
  address are **one station** - typically the EV's IPv4 and IPv6 link-local
  addresses. Once any entry of a station reaches ``NUD_FAILED``, the station is
  *suspect* and its idle STALE twins stop counting as alive; nothing ever sends
  to them, so the kernel would never re-probe them and they would veto the loss
  verdict forever. Suspicion is a latch - it survives the kernel's
  FAILED/INCOMPLETE churn under an active sender and the garbage collection of
  the failed entry - and only a fresh ``NUD_REACHABLE`` of that station, the one
  state that is new evidence from the peer, clears it.
* A single ``NUD_FAILED`` is **not** enough. It only counts while no other
  neighbour of the device is alive, and even then only if none has recovered
  within ``liveness_grace_ms``. The grace period is not re-armed by further
  failures - that would push the deadline out exactly when it should be
  expiring.
* ``NUD_INCOMPLETE`` neither starts nor cancels a grace period: resolution in
  flight is not yet a verdict.

A confirmed liveness loss is treated exactly like a carrier loss (V2G10-036).

Detection latency, and why it is allowed to be slow
---------------------------------------------------

Neighbour liveness inherits the kernel's NUD timing: a freshly confirmed entry
stays ``REACHABLE`` for ``reachable_time`` - randomized around
``base_reachable_time_ms``, i.e. **15 to 45 seconds** at the kernel default -
before anything re-probes it, plus a few seconds of unicast probes before
``NUD_FAILED``. End to end, a dead peer takes up to ~50 s to be declared lost.
That is intentional, because this mechanism is the *last* line in a hierarchy in
which everything dangerous is caught much faster:

* CE/ID faults, including unplug: the firmware's autonomous emergency machinery,
  **<= 10 ms** (IEC 61851-23-3 Tables CC.114/CC.115).
* Unplug at the link layer: the CE-mated carrier gate
  (``plc.carrier_gate: ce_mated`` in the ChargeBridge daemon), milliseconds.
* A severed SPE pair: the **EV's PHY** sees the coordinator's beacons vanish
  immediately, its state machine reacts, and that lands on the SECC as a CE
  state change within seconds.
* Communication loss during energy transfer: the ISO 15118-20 sequence and
  control-loop timeouts, seconds.

What is left for neighbour liveness is the slow-burn residue: the peer's stack
died while its PHY, the CE line and the session all still look healthy - the
zombie session. Noticing that in tens of seconds is proportionate; no power
decision waits for it. During an active session it is faster in practice
anyway, since session traffic keeps confirming (or, on loss, actively probing)
the neighbour instead of waiting out the idle timer.

Where faster reclaim is wanted, the dial is the kernel's, per device::

   sysctl net.ipv4.neigh.<device>.base_reachable_time_ms=5000
   sysctl net.ipv6.neigh.<device>.base_reachable_time_ms=5000

at the cost of a little probe traffic on an idle link. This is a deployment
knob, not a bench hack - but bench demonstrations definitely want it.

``ev_mac_address`` (``publish_ev_mac``, an Autocharge enabler) is published once
per link when a neighbour of the device becomes ``NUD_REACHABLE`` carrying a
48-bit link-layer address, formatted upper-case and colon-separated as
``interfaces/slac.yaml`` requires. It is forgotten whenever the link goes down,
so the next session publishes again.

Configuration
=============

===============================  =========  ==========================================
``device``                       cb_plc     Network device carrying the MCS SPE link.
                                            Need not exist at startup.
``link_detect_timeout_ms``       4000       TT_EV_link_detect (Table 8, max 4000).
``sync_repetition_ms``           4000       TT_sync_repetition (Table 8, max 4000):
                                            the window in which a FAILED
                                            initialization is repeated. 0 disables
                                            repetition. See the note above about the
                                            default being inert.
``conn_retry_max``               3          C_conn_retry / the CC.5.2.3.2 restart
                                            counter. 0 disables retrying.
``retry_wait_ms``                3000       Wait before requesting a restart;
                                            CC.5.2.3.2 requires at least 3000.
``neighbor_liveness``            true       Also supervise the link through the
                                            kernel neighbour table (NUD).
``liveness_grace_ms``            1000       Debounce for the above. 0 acts at once.
``publish_ev_mac``               true       Publish ``ev_mac_address`` for Autocharge.
===============================  =========  ==========================================

Errors
======

``generic/CommunicationFault`` is raised when the rtnetlink socket cannot be
opened or fails fatally, and when ``enter_bcd`` arrives while the configured
device does not exist. It is cleared when the device appears. A device that is
merely absent while no EV is connected is **not** an error.

Implementation notes
====================

* The generic netlink machinery this module is built on lives in ``lib/everest/io``
  under ``everest::lib::io::netlink``, not here: ``route_parser`` (decoding
  NETLINK_ROUTE link and neighbour messages), ``link_tracker`` (carrier and
  presence edges for a named device, and the documented IFF_LOWER_UP contract),
  ``neighbor_table`` (mirroring the kernel neighbour table), ``peer_liveness``
  (deciding when the peer counts as gone) and ``device_watcher`` (the socket, the
  subscription and the dumps). The monitor-based shutdown
  handshake is ``everest::lib::util::LifecycleStateT`` in
  ``everest/util/async/lifecycle_gate.hpp``. What stays in the module is what is
  actually MCS-specific: the state machine, the controller and ``slacImpl``.
* The EV liveness judgement is ``everest::lib::io::netlink::peer_liveness``,
  separate from the ``neighbor_table`` it reads. The table reports facts - who is
  known, what NUD state they are in - and the judgement decides what that means
  for the link: the grace window, the "only when nothing else is alive" rule and
  the no-opinion-when-empty rule. It moved to libio when McsEvDataLink became its
  second consumer; the algorithm never mentioned MCS.
* The link lifecycle is a ``boost::msm`` state machine
  (``main/link_state_machine.cpp``) with five internal states behind the three
  published ones - ``paused`` and ``retry_wait`` behave differently from the
  states they are published as. Its actions perform no I/O: they append effect
  requests (publish, start/stop timer) that the controller executes afterwards,
  which is what makes every transition testable without a socket or a timer.
* The machine is **hierarchical**, in two tables:

  - An **outer table** for the events that mean the same thing wherever the
    session currently is: ``reset``, ``leave_bcd`` and ``dlink_terminate`` end
    it, and ``dlink_error`` restarts it. Five rows, because a composite state
    lets one row cover every substate. The three teardown rows are
    self-transitions on the session: leaving it runs the exit action of
    whichever substate was active - stopping that state's timer - and
    re-entering it starts the session at ``unmatched``, which is exactly where
    all three want to end up.
  - An **inner table** for the link lifecycle itself - ``unmatched``,
    ``matching``, ``matched``, ``paused``, ``restart_wait`` and the edges
    between them. Seventeen rows.

  ``restart_wait`` is an explicit entry point of the composite, because
  ``dlink_error`` is the one session-wide event that has to land on a specific
  substate rather than on the session's initial one.

  Wherever two rows share a source and an event, their guards are mutually
  exclusive. That is deliberate rather than incidental: ``boost::msm`` does not
  resolve such a conflict in declaration order, so a guarded row paired with an
  unguarded fallback silently picks the fallback.

  One consequence of moving ``dlink_error`` to the outer table: ``restart_wait``
  needs a row that *swallows* a repeated ``dlink_error``, or the outer rows would
  fire and restart the guard. Nothing observable changed - the guard is not
  restarted and no second attempt is spent, as before - but because swallowing it
  is a transition rather than an unhandled event, that case no longer increments
  the machine's "ignored events" counter. The counter means "no rule matched",
  not "nothing happened".
* There is **no worker thread**. The ``lib/everest/io`` event loop runs inside
  ``ready()``, on the thread the framework spawns for the global-ready message
  and joins last during teardown. ``shutdown()`` runs on a different framework
  thread, stops the loop and waits for it to return before the module is
  destroyed.
* Interface commands arrive on framework threads and only ever append to a
  monitor-guarded FIFO and notify an ``event_fd``. The state machine, the
  liveness judgement and the timers are touched by exactly one thread.

If not run as root, this module needs no capabilities: subscribing to rtnetlink
link and neighbour notifications and dumping them is unprivileged.

Debugging notes
===============

* ``ip monitor link`` on the configured device shows exactly what this module
  keys on. Look for ``LOWER_UP`` in the flags, not for ``state UP`` or
  ``RUNNING``.
* **Carrier-down can be reported late.** The kernel's linkwatch work is
  rate-limited to roughly one run per second, so the ``RTM_NEWLINK`` that drops
  ``IFF_LOWER_UP`` may lag the physical event by up to about a second. The
  seconds-scale ISO 15118-10 timers tolerate this, but do not read a ~1 s delay
  between a firmware-side link drop and ``dlink_ready(false)`` as a bug in this
  module. The same rate limiting is why a carrier-off TAP is briefly announced
  with ``IFF_RUNNING`` set.
* ``ip neigh show dev <device>`` shows the NUD states the liveness judgement reads.
* The module logs every state change, every D-LINK primitive and every timer
  verdict at info or warning level; a session's whole link lifecycle is
  reconstructable from the log without a debugger.

References
==========

ISO 15118-10:2025 V2G10-019, V2G10-021, V2G10-023, V2G10-034, V2G10-036,
V2G10-037, V2G10-038, V2G10-041, V2G10-042, V2G10-043, V2G10-052, V2G10-054,
V2G10-055 to V2G10-058, Table 8; IEC 61851-23-3 (CDV) CC.5.2, CC.5.2.3.2,
Table CC.110.
