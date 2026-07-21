.. _project-deprecation-index:

########################
Active Deprecation Index
########################

This page lists all public API components that are currently deprecated in
EVerest. It is at least updated with every stable release.

Each entry records the deprecated component, the release in which it was
deprecated, the earliest release in which it may be removed (following the
:ref:`minimum deprecation period <project-deprecation-policy>`), and a link
to the corresponding migration guide.

.. list-table::
   :header-rows: 1
   :widths: 30 20 20 30

   * - Component
     - Deprecated in
     - Earliest removal
     - Migration guide
   * - Evse15118D20 `logging_path` config option
     - 2026.10.0
     - 2027.03.0
     - Remove every `logging_path` entry from the EVerest config(s). An alternative to save EXI messages from the actual ISO15118-20 session the `PacketSniffer` module, `tcpdump` or `Wireshark` can be used.  
   * - 
     - 
     - 
     - 
