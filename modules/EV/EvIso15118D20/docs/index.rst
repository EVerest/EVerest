.. _everest_modules_handwritten_EvIso15118D20:

EvIso15118D20
=============

EV-side ISO 15118-20 EVCC built on the ``libiso15118`` ``ev::Controller``. It
provides the ``ISO15118_ev`` interface (the same interface as ``PyEvJosev``), so
it is a drop-in software-in-the-loop (SIL) replacement for the Python EVCC when
driving a DC or AC ISO 15118-20 session against a SECC such as ``Evse15118D20``.

AC support covers the Dynamic control mode over plain TCP with external
identification means (EIM) authorization.

Configuration
-------------

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Option
     - Default
     - Description
   * - ``device``
     - ``eth0``
     - Ethernet device used for high-level communication. ``auto`` lets the
       library pick a usable IPv6 interface.
   * - ``evcc_id``
     - ``02:00:00:00:00:01``
     - EVCC identifier sent to the SECC (MAC-address format).
   * - ``response_timeout_ms``
     - ``20000``
     - Response watchdog timeout in milliseconds.
   * - ``ac_max_charge_power_w``
     - ``11040``
     - Advertised AC maximum charge power in watts.
   * - ``ac_min_charge_power_w``
     - ``1380``
     - Advertised AC minimum charge power in watts.

Limitations
-----------

The implementation has a deliberately narrow scope:

- **DC and AC only.** ``start_charging`` accepts DC and AC single/three-phase
  energy-transfer modes; WPT and MCS sessions are not supported.
- **No TLS.** The session advertises ``NO_TRANSPORT_SECURITY``; Plug & Charge and
  TLS are out of scope.
- **No pause/resume.** ``pause_charging`` is a no-op.
- **No BPT / SAE.** Bidirectional power transfer (``set_bpt_dc_params``) and
  SAE J2847/2 (``enable_sae_j2847_v2g_v2h``) are not implemented.

Threading
---------

A single worker thread, started from ``ready()``, runs one V2G session at a time.
``start_charging`` hands a request to the worker; the worker constructs the
``ev::Controller`` and runs its event loop until the session ends, then waits for
the next request. Session state is guarded by a ``monitor`` so command handlers
and the worker coordinate safely, including during module teardown.
