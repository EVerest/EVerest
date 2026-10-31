.. _htg_pionix_chargebridge:

###################
Pionix ChargeBridge
###################

The Pionix ChargeBridge is a development board that exposes the hardware
interfaces of a charger to a host PC over Ethernet. The
``pionix_chargebridge`` application runs on the host and maps each interface
of the board to a local device, so that EVerest modules use them as if the
hardware were attached directly:

.. list-table::
   :header-rows: 1

   * - ChargeBridge interface
     - Host side
     - Used by
   * - ``can_0``
     - virtual CAN interface ``cb_can``
     - e.g. DC power supply drivers
   * - ``serial_1``, ``serial_2``
     - ``/dev/cb_uart``, ``/dev/cb_rs485``
     - e.g. Modbus power meters
   * - ``plc``
     - TAP network interface ``cb_plc``
     - ``EvseSlac`` and the ISO 15118 stack
   * - ``evse_bsp``
     - EVerest board support API
     - ``EvseManager``

This guide shows how to connect a ChargeBridge to a host PC and run EVerest
with the AC configuration ``config-CB-SAT-AC.yaml``.

*************
Prerequisites
*************

* EVerest built and installed as described in the
  :doc:`Getting Started Guides <getting-started/index>`. The commands below
  assume the default install prefix ``build/dist``.
* The ``pionix_chargebridge`` application, built along with EVerest.
* A ChargeBridge, its power supply and an Ethernet cable.

*******
Cabling
*******

1. Connect the ChargeBridge to the host PC with the Ethernet cable **before**
   powering it up.
2. On the host, set the IPv4 method of that Ethernet connection to
   **Shared to other computers** (NetworkManager), so that the host assigns an
   IP address to the ChargeBridge. You can also use a router or a DHCP server.

The ChargeBridge configuration uses ``ip: ANY_EVSE`` by default, so
``pionix_chargebridge`` discovers the board through mDNS and no address needs
to be configured.

***********
Permissions
***********

Two binaries need Linux capabilities to run without root:

* ``pionix_chargebridge`` creates the ``cb_can`` and ``cb_plc`` interfaces,
  which requires ``CAP_NET_ADMIN``.
* ``EvseSlac`` opens a raw packet socket on ``cb_plc``, which requires
  ``CAP_NET_RAW``.

From the root of the EVerest repository:

.. code-block:: bash

   sudo setcap cap_net_admin+ep ./build/applications/pionix_chargebridge/pionix_chargebridge
   sudo setcap cap_net_raw+ep ./build/dist/libexec/everest/modules/EvseSlac/EvseSlac

.. note::

   Capabilities are lost whenever a binary is rewritten. Repeat these commands
   after every rebuild and every ``cmake --build build --target install``.

******************************
Connecting to the ChargeBridge
******************************

From the root of the EVerest repository:

.. code-block:: bash

   ./build/applications/pionix_chargebridge/pionix_chargebridge \
       ./applications/pionix_chargebridge/config/config-CB-SAT-AC.yaml

The argument is the ChargeBridge configuration file loaded by the application.

With ``fw_update_on_start: true``, the application updates the board firmware
on start using the file given by ``fw_file``. A relative ``fw_file`` path is
resolved against the directory of the configuration file.

Leave the application running.

***************
Running EVerest
***************

In a second terminal, from the ``build`` directory:

.. code-block:: bash

   cd build
   LD_LIBRARY_PATH=./dist/lib/ ./dist/bin/manager --prefix ../build/dist \
       --config ../config/config-CB-SAT-AC.yaml

Variants of this configuration are available in ``config/``.

*************
Checking Logs
*************

The connector is ready when the manager logs:

.. code-block:: text

   Communication check: SUCCESS
   Cleared error of type evse_manager/Inoperative
   🌀🌀🌀 Ready to start charging 🌀🌀🌀

The PLC link is up when ``EvseSlac`` reports the modem firmware version:

.. code-block:: text

   Starting the SLAC state machine
   Lumissil PLC Device Firmware version: <version>
