.. _everest_modules_handwritten_EvseSlacNeo :

.. ===================
.. EvseSlacNeo
.. ===================

EvseSlacNeo is the charger-side (EVSE) implementation of the SLAC protocol
defined in ISO 15118-3. It is the part of EVerest that pairs the powerline
modem in your charger with the modem in the car, so that the two can talk
high level charging protocols (ISO 15118 / DIN 70121) afterwards.

This page explains what SLAC does, what features the module offers, which
modem hardware is supported and how to configure the most common setups.

What is SLAC?
=============

When an electric car and a DC or smart AC charger communicate, they do so
via powerline communication (PLC): high-frequency signals injected onto the
Control Pilot wire of the charging cable. Both sides contain a small
HomePlug Green PHY modem for this.

Out of the box these modems do not trust each other. Before any charging
related communication can happen, the charger has to verify that the car it
hears is really the one plugged into *this* cable (and not a car at the
neighbouring charging point, whose signal crosses over), and both modems
have to join a private, encrypted network. The procedure that does this is
SLAC — *Signal Level Attenuation Characterization*: the car sends a series
of test signals ("sounds"), the charger measures how strongly they arrive,
and if the signal is strong enough the two pair up using a fresh network
key.

::

   +------------------+                            +----------------------+
   |     EV (car)     |      charging cable        |    EVSE (charger)    |
   |                  |   ======================   |                      |
   |  +-----------+   |     Control Pilot wire     |   +-----------+      |
   |  | PLC modem |<--+--> ~~ HF signals ~~ <------+-->| PLC modem |      |
   |  +-----------+   |   ======================   |   +-----+-----+      |
   |                  |                            |         |            |
   +------------------+                            |   EvseSlacNeo        |
                                                   |   EvseManager        |
                                                   |   ISO 15118 stack    |
                                                   +----------------------+

How a session unfolds
=====================

EvseSlacNeo is driven by the EvseManager module: when the car is plugged in
(Control Pilot state changes from A/E/F to B/C/D), EvseManager calls
``enter_bcd`` and the module starts listening. From the user's perspective
the whole procedure takes well under a second on a good connection.

::

   Car plugged in (CP state A -> B)
          |
          v
   EvseManager: enter_bcd  ------------>  EvseSlacNeo armed
          |
          v
   Car asks "is a charger listening?"     (CM_SLAC_PARM.REQ)
          |
          v
   Sounding: car sends test signals,      (CM_MNBC_SOUND.IND)
   charger measures the attenuation
          |
          v
   Charger reports the measurement        (CM_ATTEN_CHAR.IND)
          |
          v
   Car picks this charger, asks to pair   (CM_SLAC_MATCH.REQ)
          |
          v
   Charger hands out a fresh network      (CM_SLAC_MATCH.CNF)
   key (NMK) and programs its own modem
          |
          v
   Both modems join the private network
   (AVLN), the PLC link comes up
          |
          v
   dlink_ready(true) is published:
   high level communication starts

When the car is unplugged, EvseManager calls ``leave_bcd`` and the module
leaves the logical network, generates a new key and re-arms itself — from
*any* state, so a failed or aborted session can never get the charger stuck.

Features
========

Robust startup and recovery
---------------------------

* The module starts even if the PLC network interface is not (yet) present,
  and recovers automatically when the interface disappears and comes back —
  for example when the modem is reset or powered late during boot. Module
  readiness is held back until the PLC I/O is actually usable.
* I/O and the SLAC state machine run event driven in a single thread; there
  are no polling threads burning CPU in idle.
* If a PLC error interrupts an established session, ``dlink_ready(false)``
  is published so the rest of the system reacts immediately instead of
  talking into a dead link.

Reliable modem key setup
------------------------

Programming the network key into the local modem (CM_SET_KEY) is the step
where real-world modems differ the most. Two behaviors are configurable:

* ``set_key_handling_mode``: one single attempt (legacy behavior), or
  retries with confirmation (``retry_confirmed``, the default) where the new
  key is only trusted after the modem has confirmed it.
* ``set_key_cnf_success_mode``: which confirmation result counts as
  success. Qualcomm chips answer ``0x01`` (the default expectation), the
  HomePlug Green PHY standard says ``0x00``, and a compatibility mode
  accepts both.

Fresh network keys are generated for every session. By default only
printable characters are used (compatible with some picky EVs in the
field); ``nmk_generation_mode: full_byte_range`` opts into the full byte
range for a lower collision probability.

Link supervision
----------------

With ``link_status_detection`` enabled the module does not simply hope that
the PLC link comes up after pairing — it asks the modem:

* After a successful match it polls the modem until the link is really up,
  and only then reports ``dlink_ready(true)``.
* While matched it keeps polling (``link_status_poll_in_matched_state_ms``,
  default 200 ms) so a lost connection — cable damage, modem reset — is
  noticed quickly and the network is left within the time ISO 15118-3
  demands.
* Short link dropouts, as they can occur under full DC load, can be ridden
  out with ``link_status_debounce_count``: only that many *consecutive*
  negative answers tear the session down.

EV validation (BCB toggle)
--------------------------

If a car wants extra certainty that it is paired with the right charger, it
can start the optional CM_VALIDATE procedure from ISO 15118-3: the car
briefly toggles its Control Pilot state (B → C → B) a number of times and
asks the charger how many toggles it saw. EvseSlacNeo implements the full
two-step exchange, including the timed observation window. The toggle count
itself comes from EvseManager via the ``count_bc`` command of the slac
interface, so this works out of the box in a standard EVerest setup.

Transmit power limitation (CM_AMP_MAP)
--------------------------------------

Some installations need to reduce the PLC transmit power on selected OFDM
carriers, for example to meet EMC requirements. With
``initiate_amp_map: true`` the module sends an amplitude map to its modem
once the network is established. The map is described in a small YAML file
(``amp_map_file``) — see ``config/slac_amp_map_example.yaml``:

.. code-block:: yaml

   carriers: 1155          # number of OFDM carriers
   default_amplitude: 15   # 15 = full power, 0 = maximum reduction
   overrides:              # optional per-carrier values
     100: 4
     101: 4

Incoming CM_AMP_MAP requests are always answered, regardless of this
option.

Living next to other charging points
------------------------------------

On sites with several charging points the modems hear each other
("cross talk"). The module keeps up to ``max_matching_sessions`` candidate
sessions apart and answers each car individually — the attenuation
measurement then makes sure only the physically connected car pairs up.
On multi-connector hardware, ``startup_delay_ms`` staggers the modem
queries of multiple EvseSlacNeo instances at boot.

Autocharge support
------------------

With ``publish_mac_on_match_cnf`` (default on) the MAC address of the
successfully matched car is published on the ``token_provider`` interface.
This can be used for autocharge — identifying a returning car by its modem
MAC — including on AC with cars that do not do high level communication.

Compatibility switches for real-world EVs
-----------------------------------------

* ``ac_mode_five_percent`` (default on): restart failed SLAC sessions as
  ISO 15118-3 specifies for AC. The standard does not allow the retries for
  DC, but leaving this on is strongly recommended — some EVs in the field
  frequently need a second attempt.
* ``reset_instead_of_fail`` (default on): after an aborted session, go back
  to a ready state instead of the standard's terminal failed state, because
  some cars immediately try again with a new CM_SLAC_PARM.REQ. Turn this
  off only for conformance testing.
* ``sounding_attenuation_adjustment``: add a dB offset to the measured
  attenuation to compensate for the coupling characteristics of your
  hardware.

Diagnostics
-----------

* ``print_state_transitions`` logs every internal state change.
* With EVerest telemetry enabled, the module publishes detailed information
  about every SLAC session.
* Successful sessions are narrated at INFO level in the log, so a session
  can be followed without debug logging.

Supported hardware
==================

The module talks standard HomePlug Green PHY on the wire and works with any
compliant EVSE modem. The vendor of the connected modem is detected
automatically at startup; for the following chips vendor-specific
extensions are used:

+---------------------+------------------------------------------------------+
| Modem               | Specialities                                         |
+=====================+======================================================+
| Qualcomm            | Link status detection and supervision. Optional      |
| QCA7000 / QCA7005   | chip reset after key setup (``do_chip_reset``, via   |
| (QCA700x family)    | the RS_DEV vendor extension — Qualcomm only). Note   |
|                     | that these chips confirm CM_SET_KEY with result      |
|                     | ``0x01``; the default configuration expects this.    |
+---------------------+------------------------------------------------------+
| Lumissil CG5317     | Link status detection and supervision (via the NSCM  |
|                     | vendor extension). Default set-key timing works.     |
+---------------------+------------------------------------------------------+
| Other HPGP modems   | Full SLAC functionality; link status detection and   |
|                     | chip reset are not available and must stay disabled. |
+---------------------+------------------------------------------------------+

Typical configuration
=====================

For most chargers the defaults are sensible and only the network device
needs to be set:

.. code-block:: yaml

   evse_slac:
     module: EvseSlacNeo
     config_implementation:
       main:
         device: eth1

Recommended additions for hardware with a Qualcomm or Lumissil modem:

.. code-block:: yaml

         link_status_detection: true
         link_status_debounce_count: 3   # ride out short link flaps

Requirements and notes
======================

* The module opens a raw ethernet socket on ``device``. If EVerest does not
  run as root, the process needs the ``CAP_NET_RAW`` capability.
* The EV-side counterpart of this module is **EvSlacNeo**, built on the
  same library — useful for simulators and test setups.
* ``debug_simulate_failed_matching`` and
  ``hack_disable_regenerate_key_on_reset`` are debugging aids and must not
  be enabled in production.
