.. _everest_modules_handwritten_IsoMux:

.. *******************************************
.. IsoMux
.. *******************************************

This module multiplexes one PLC interface, SDP server and TLS endpoint between an
EvseV2G instance (DIN SPEC 70121, ISO 15118-2) and an Evse15118D20 instance
(ISO 15118-20), each running without its own SDP server on the loopback interface.

.. warning::

   **Deprecated** in 2026.10.0, earliest removal in 2027.04.0. Use
   :ref:`Evse15118D20 <everest_modules_Evse15118D20>` alone, which negotiates DIN
   SPEC 70121, ISO 15118-2 and ISO 15118-20 itself, so no multiplexer is needed.
   See the how-to guide :ref:`Migrate from EvseV2G and IsoMux to Evse15118D20
   <howto-iso15118-stack-migration>`. The module logs a deprecation warning at
   startup.
