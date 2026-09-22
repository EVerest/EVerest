##################################
Security Best Practices
##################################

This how-to-guide describes security best practices for deploying
EVerest in a production environment. It provides checklists with 
items to consider for securing the system.

EVerest-related security aspects
================================

- Do not expose the main MQTT broker on any interface (e.g. PLC,
  Ethernet, ...). We recommend using a unix domain socket between
  EVerest and Mosquitto.

In Mosquitto, config file add the line:

.. code-block::

   listener 0 /tmp/mosquitto.socket

In the EVerest config file, add the following to the settings section:

.. code-block:: yaml

   settings:
    mqtt_broker_socket_path: /tmp/mosquitto.socket

- If a part of an MQTT API should be exposed, e.g. to a mobile app on
  the same wireless LAN, use mutually authenticated TLS for the MQTT
  connection and run a separate Mosquitto instance. Bridge only those
  API topics strictly needed (and nothing from the “everest” topic)
  between the two Mosquitto instances. Shutdown the public Mosquitto
  whenever it is not needed.
- Use OCPP security level 3 for the CSMS connection.
- Use a hardware security module, e.g. TPM2 for generating and
  securing private keys.
- Don't use the *admin-panel* on the product and ensure EVerest does
  not listen on port 8849. In the Yocto recipes, this is disabled by
  default. Take special care if you use another build system. Set
  "EVEREST_ENABLE_ADMIN_PANEL_BACKEND=OFF" in cmake.
- Do not run EVerest modules as root user. Create a low privilege
  user, that has access only to what is needed (via filesystem
  permissions, group memberships, ...). In Yocto, you can do this in your
  image file similar to this:

.. code-block::

   inherit useradd
   USERADD_PACKAGES = "${PN}"
   USERADD_PARAM:${PN} = "-u 1500 -d /home/everest -g everest -G systemd-journal,dialout,tty,gpio,tss,mosquitto -r -s /bin/false everest"
   GROUPADD_PARAM:${PN} = "-g 1500 everest; -g 1501 gpio; -r systemd-journal; -r tss"
   GROUPMEMS_PARAM:${PN} = ""

.. note::

   Make sure to use the correct groups and settings for your system.

Specify this user in the EVerest config file in the settings section:

.. code-block:: yaml

   settings:
    run_as_user: everest

The systemd service should start the manager process as root. It will
then change the user for the child processes it forks (the modules).
Alternatively the manager process can be started with the appropriate
Linux capability to allow user changes.

Some modules need elevated privileges, e.g. SLAC module. Those modules
should be given individual Linux capabilities via file based capabilities.
A module declares the capabilities it needs in the ``capabilities`` list
of its ``manifest.yaml``:

.. code-block:: yaml

   capabilities:
     - CAP_NET_RAW

Here is a list of capabilities required by the EVerest modules:

================== ==============================================================
Module name        Capabilities
================== ==============================================================
EvseSlac / EvSlac  CAP_NET_RAW
Setup              CAP_NET_ADMIN, CAP_NET_RAW, CAP_DAC_OVERRIDE
PacketSniffer      CAP_NET_RAW
Huawei_V100R023C10 CAP_NET_RAW
EvseManager        CAP_NET_RAW, only with ``debug_emit_cp_state_hpav_frames``
================== ==============================================================

The script ``applications/utils/scripts/set_module_capabilities.py`` reads
the manifests of all installed modules and grants these capabilities to the
module binaries with ``setcap``. It needs PyYAML, and ``setcap`` requires
root privileges (``CAP_SETFCAP``):

.. code-block:: bash

   sudo python3 applications/utils/scripts/set_module_capabilities.py \
       --modules-dir /usr/libexec/everest/modules

Add ``--dry-run`` to only print the ``setcap`` commands. Only compiled
modules (C++, Rust) can be given file capabilities. Python and JavaScript
modules are skipped with a warning, since capabilities on the interpreter
would apply to every script it runs.

The ``everest-core`` recipes of the meta-everest layers inherit the
``everest_file_capabilities`` class, which runs this script in
``pkg_postinst`` when the root filesystem is created, so images built with
them contain the file capabilities without further configuration.
Installing the package on a running target does not apply them, run the
script manually in this case.

Run the script again whenever module binaries are replaced, since
overwriting a file drops its capabilities. File capabilities are ignored
on file systems mounted with ``nosuid``.

At startup, the framework adds these capabilities to the ambient set of
the module process, so programs it starts, e.g. ``ip`` called by the Setup
module, run with the same capabilities.

.. note::

   Setting Linux capabilities for a module with the ``capabilities`` key
   in the EVerest config file is no longer supported. The key is still
   accepted, but the manager ignores it and logs a warning at startup.

- To ensure that internal services cannot be accessed via the powerline connection,
  iptables can be used with the following rules. In this example the powerline module
  is on device seth0.

.. code-block::

   ip6tables -S | grep seth0
   -A INPUT -i seth0 -p ipv6-icmp -j ACCEPT
   -A INPUT -i seth0 -p udp -m udp --dport 15118 -j ACCEPT
   -A INPUT -i seth0 -p tcp -m tcp --dport 50000 -j ACCEPT
   -A INPUT -i seth0 -p tcp -m tcp --dport 61341 -j ACCEPT
   -A INPUT -i seth0 -p tcp -m tcp --dport 64109 -j ACCEPT
   -A OUTPUT -o seth0 -p ipv6-icmp -j ACCEPT
   -A OUTPUT -o seth0 -p udp -m udp --sport 15118 -j ACCEPT
   -A OUTPUT -o seth0 -p tcp -m tcp --sport 50000 -j ACCEPT
   -A OUTPUT -o seth0 -p tcp -m tcp --sport 61341 -j ACCEPT
   -A OUTPUT -o seth0 -p tcp -m tcp --sport 64109 -j ACCEPT

According to the standard, port 15118 is used for SDP messages.
:ref:`EvseV2G <everest_modules_EvseV2G>` uses the configurable ``tcp_port`` and ``tls_port``
values (defaults: TCP ``61341``, TLS ``64109``).
:ref:`Evse15118D20 <everest_modules_Evse15118D20>` integrates libiso15118 and uses the configured
``tcp_port`` value for TCP and TLS1.2/1.3. The default is ``50000``.
When changing these values, update the firewall rules accordingly.

General (non-EVerest-related) security aspects
====================================================================

Reaching complete system-level security is a complex topic, that should
be handled by experts. This manual cannot give real advice here.

Following, we created an unordered and incomplete list of things that
you should consider during the development process:

- Use secure boot and make sure that you do a full lock-down on
  production units that prevents any unsigned code to boot.
- Disable *bootloader* console on production units.
- Disable any UART console login.
- Disable or lock down all programmer interfaces such as SWD or JTAG
  in production units.
- Don't expose USB ports unless you really know what you are doing. A
  quite common attack vector for charging stations is to plug in a USB
  dongle and reconfigure / update the firmware - or attach a keyboard …
- Evaluate if the rootfs or special partitions need encryption.
- Use a secure vault for all private keys on the device
  (e.g. TPM2.0).
- Make sure your PKI for secure boot and OTA is properly maintained.
  If you loose the private root key, the hardware in the field is lost.
- Use signed updates. Evaluate if encryption is needed.
- Use signed updates also for all other components, such as the
  safety MCU.
- Be able to OTA-update all components in the charger through the
  normal OTA package - also third party components if they have a
  firmware update feature.
- Network interfaces: Verify that only services being absolutely
  necessary are accessible on a network interface.
- Use mTLS with TPM if you have a custom cloud connection.
- Lock down all maintenance / service / repair ports.
- Maintain the complete Linux system and provide regular updates.
- Don't use default passwords for anything - or better: Don't use
  passwords at all.
- Use software and hardware watchdogs.

----

**Authors**: Cornelius Claussen
