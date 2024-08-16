.. doc_pionix_belay-box

Pionix BelayBox
###############

.. warning::
  This page about the BelayBox is outdated as we are currently moving things
  from the Debian-based to a newer Yocto-based image. The documentation will
  be updated soon. Until that, we have created temporary
  :ref:`quick-and-dirty instructions at the end of this docs <belaybox_new_yocto_based>`
  .

Introduction
************

The BelayBox is a reference platform specifically designed for development and
testing of the open source software EVerest.

Inside the box, a Raspberry Pi is built in and we are officially part of the
"Powered by Raspberry Pi" scheme:

.. image:: img/powered-by-pi.png
  :width: 300
  :alt: Logo Powered by Raspberry Pi for Charging Development Kit BelayBox
  :align: center

BelayBox can be utilized by individuals, research facilities and companies
alike to

* parallelize HW and SW developments for new charger projects,
* explore new charging algorithms without the need do all the groundwork,
* rapid integration of EV charging with other applications

and anything else you want to quickly do without building your own EVerest
compatible charger first.

The BelayBox is not meant to be used for private usage or outdoor charging.

The BelayBox hardware
=====================
Inside, the BelayBox consists mainly of the Yeti board - an AC charger for
electric vehicles (EV) supporting IEC-61851-1 and SAE J1772 - and the Yak
board, which is a high-level control board for EV charging stations supporting
ISO 15118-2 (with ISO 15118-20 on its way) and DIN SPEC70121.

As both Yeti and Yak Board are also released as Open Hardware under CERN Open
Hardware Licence Version 2 (Permissive), we are very happy to point you to the
schematics and design files and also the firmware:

* `Yeti and Yak Hardware Reference Design <https://github.com/PionixPublic/reference-hardware>`_
* `Yeti Firmware <https://github.com/PionixPublic/yeti-firmware>`_

The 3D files of the BelayBox case components can be downloaded here:
`BelayBox 3D files <https://a360.co/45erK90>`_.

For more information about vendors working with EVerest,
contact us via
the `EVerest mailing list <https://lists.lfenergy.org/g/everest>`_.

Getting support
===============

If you already have purchased a BelayBox and you have hardware related
questions, you can get support by creating an issue via our
`support page <http://support.pionix.com>`_.

.. important::

  This is only for hardware-related support. For all software-related
  questions, you can find help in the wonderful EVerest community via
  `Zulip <https://lfenergy.zulipchat.com/>`_ or the
  `EVerest mailinglist <https://lists.lfenergy.org/g/everest>`_.

If you need additional parts for your BelayBox, see the
`Pionix Online Shop <https://shop.pionix.com>`_.

Setting up Hardware and Software
********************************

Assembling the Yak Board
========================

Starting assembling the Yak Board, you should have the following parts
available:

.. image:: img/yak-assembly-1-overview.jpg

And you will need the following tools:

* ESD safe environment, e.g. ESD wrist band
* ESD underlay mat
* Linux host system, Ubuntu >18 recommended
* 1x Micro USB cable
* 12V DC power supply with minimum 30W to connect to “12V IN” pins on
  Yak board. A lab power supply is sufficient.

Needed software:

* `Raspberry PI USB Boot <https://github.com/raspberrypi/usbboot/blob/master/Readme.md#building>`_
* `balenaEtcher <https://www.balena.io/etcher>`_
  (“dd” also works but is dangerous to use and much slower)
* Internet access from host system

.. warning::
  Before working with any open PCB make sure to work in an ESD safe
  environment using ESD safe equipment only.

Glue on the heatsinks as shown in the following image using the double
sided tape that comes with the heatsinks. Plug in the small clips into
the mounting holes of the CM4 board as shown.

.. image:: img/yak-assembly-2.jpg

Turn around the CM4 and put on the gray spacers as shown here:

.. image:: img/yak-assembly-3.jpg

Plug the CM4 board in both connectors and make sure the clips go all the way
through the Yak board and hold the CM4 securely without any gaps between the
spacers and both boards. Make sure to remove the metal part (if there is one)
out of the board-to-board connector as shown in the upper left corner in the
following image:

.. image:: img/yak-assembly-4.jpg

This is how it looks from the top side:

.. image:: img/yak-assembly-5.jpg

Now place the small black jumper onto the "BOOT" pins as shown above. This
is needed to be able to mount the emmC flash to the host system.

Flashing the Yak Board
======================

.. image:: img/yak-assembly-6.jpg

Plug in a micro usb cable to the "J1" USB socket and plug the other end in the
linux host system.

.. warning::
  As we are currently moving things from the Debian-based to a newer
  Yocto-based image, please refer to our temporary
  :ref:`quick-and-dirty instructions at the end of this docs <belaybox_new_yocto_based>`
  .

Assembling the Yeti Board
=========================

Here's what you should have:

.. image:: img/yeti-assembly-1-overview.jpg

Tools needed:

* ESD safe environment, e.g. ESD wrist band
* ESD underlay mat

Clip on the touch protection cage and make sure all clips are correctly seated
as shown here:

.. image:: img/yeti-assembly-2.jpg

Clip in the smaller part of the touch protection and make sure all clips are
correctly seated as shown here:

.. image:: img/yeti-assembly-3.jpg

Clip in the bigger part of the touch protection and make sure all clips are
correctly seated as shown in the following image:

.. image:: img/yeti-assembly-4.jpg

Your mission can be seen as accomplished if your Yeti looks like that:

.. image:: img/yeti-assembly-5.jpg

Preparing the cable set
=======================

That's how we start:

.. image:: img/cable-set-1-overview.jpg

The **10-position cable between Yeti and Yak** is mandatory to connect Yak to
Yeti and to power the Yak board from the Yeti power supply.

.. image:: img/cable-set-2.jpg

Plug in one of the crimped cables with one end into the 10-position plug. Make
sure to plug in the crimp in the exact same orientation as shown in the
picture above. Be aware that the crimps cannot be unplugged again from the
10-position plug. Make sure you plug in the crimps in the correct positions
before actually plugging them in.

Plug in the other crimped end of the cable into the second plug. It is very
important to plug in the crimps in the shown “1:1” fashion. Doing otherwise
will permanently damage the Yak and/or Yeti board.

.. image:: img/cable-set-3.jpg

.. warning::
  Be aware that the crimps cannot be unplugged again from the 10 position
  plug. Make sure you plug in the crimps in the correct positions before
  actually plugging them in.

Continue with plugging in all ten cables one after the other as there is less
chance of getting it wrong this way.

This is how the cable looks when assembly is done:

.. image:: img/cable-set-4.jpg

Let's continue with the **6-position CAN + RS485 cable**.

.. image:: img/cable-set-5.jpg

Plug in a crimped cable with one end into the 6-position plug.
Make sure to plug in the crimp in the exact same orientation as shown in the
picture above. Continue with plugging in all needed cables.

Be aware that these cables have unisolated, open ends. In case you use the
6-position cable for e.g. using the CAN bus, make sure all other not used
cables are isolated to prevent damage to the Yak board.

This is how the assembled cable looks like:

.. image:: img/cable-set-6.jpg

This is the pin description of the Yak board's 4-, 6- and 10-position sockets:

.. image:: img/cable-set-7.png

Final Yak-Yeti-Cable-Setup
==========================

Tools needed:

* ESD safe environment, e.g. ESD wrist band
* ESD underlay mat
* Preassembled Yak, Yeti kits and cable-set as shown in sections above

.. image:: img/final-assembly.jpg

Plug in the 10-pin cable into the corresponding sockets on both ends.
Plug in the 4-pin RFID/NFC reader cable.
The assembly of Yak, Yet kit and cable set is completed.

When using the assembly in a "desk" environment, it is recommended to apply
power through the 12V DC barrel connector shown in the upper right corner of
the Yeti board in the image above. Make sure the WiFi antenna does not touch
any other open PCB parts to prevent damage to the boards.

.. _belaybox_furtherinfo:

BelayBox Further Information
****************************

Reference Cheat Sheet
=====================

Make root partition read/writable
---------------------------------

Use the following command:

.. code-block:: bash

  rw

Make it read only again
-----------------------

Use the following command:

.. code-block:: bash

  ro

File containing wifi settings
-----------------------------
.. code-block:: bash

  /mnt/user_data/etc/wpa_supplicant/wpa_supplicant.conf

Use of custom everest build or config
-------------------------------------
Force the use of custom everest build or config by automated start of
``everest-dev.service`` instead of ``everest.service``

.. code-block:: bash

  /mnt/user_data/opt/everest/<crosscompiled everest binaries>

Define release channels
-----------------------
Contains either stable or unstable to define release channels:

.. code-block:: bash

  /mnt/user_data/etc/update_channel

Wireguard VPN configuration
---------------------------
.. code-block:: bash

  /mnt/user_data/etc/wireguard/wg0.config

Persistent user config
----------------------
Via a complete config:

.. code-block:: bash

  /mnt/user_data/etc/everest/custom.yaml

Via a config file containing only the diffs to the default config:

.. code-block:: bash

  /mnt/user_data/user-config/config-deploy-devboard.yaml

Stop automatic updates
----------------------
.. code-block:: bash

  rw; sudo systemctl disable ota-update.service

Additional config files for the mqtt broker
-------------------------------------------
.. code-block:: bash

  /mnt/user_data/etc/mosquitto/conf.d

This is the place where you can add for example a “public_mqtt.conf” file with the following contents:

.. code-block:: bash

  listener 1883
  allow_anonymous true

With this, you allow anonymous external connections to the mqtt broker for
debugging purposes.

Watch the output of everest.service
-----------------------------------

.. code-block:: bash

  sudo journalctl -fu everest.service

For watching the output of everest-dev.service, set service name to
*everest-dev.service*.

Run EVerest in terminal
-----------------------

.. code-block:: bash

  sudo /opt/everest/bin/manager --conf /opt/everest/conf/config-deploy-devboard.yaml

or for using the custom user config:

.. code-block:: bash

  sudo /opt/everest/bin/manager --conf /mnt/user_data/etc/everest/custom.yaml

Make sure the systemd service is not running.

Using online updates
====================

.. warning::
  This section about BelayBox updating is outdated as we are currently moving
  things from the Debian-based to a newer Yocto-based image. Find setup
  instructions in the temporary
  :ref:`quick-and-dirty instructions at the end of this docs <belaybox_new_yocto_based>`
  . Information about doing updates will follow.

BelayBox comes with a very simple online update tool that is controlled by
two systemd services:

``ota-update.service``: This service starts a shell script that checks for
online updates on Pionix update servers. It is triggered by the second systemd
service:

``ota-update.timer``: This is the systemd timer unit that starts
``ota-update.service`` on regular intervals.

To disable online updates use ``sudo systemctl disable ota-update.service``.
The online update updates always the full root partition. All data that needs
to survive the update needs to be stored in ``/mnt/user_data``.

The root partition should normally never be modified, it is read only. All
changes will also be lost on the next online update.

If you still want to modify something, use the ``rw`` and ``ro`` commands
to re-mount root read-write/read-only.

In rw mode you can e.g. use ``sudo apt install ...`` to install new software.

Disable online update if you need the changes to stay.

Factory reset
=============

For a factory reset of the BelayBox, the following partition has to be
formatted:

.. code-block:: bash

  /mnt/user_data/

Before that, all services accessing that partition have to be stopped:

.. code-block:: bash

  sudo systemctl stop everest
  sudo systemctl stop nodered

.. hint::
  Depending of your setup, the EVerest service could also be called
  *everest-dev* or *everest-rpi* instead of just *everest*.

After this, unmount the partition:

.. code-block:: bash

  sudo umount /dev/mmcblk0p6

Finally, formatting can start:

.. code-block:: bash

  sudo mkfs -t ext4 /dev/mmcblk0p6

Confirm with "y" as soon as you are happy with losing all previous
configuation settings (e.g. WiFi credentials).

After formatting, reboot the BelayBox to let it setup the factory default
configuration:

.. code-block:: bash

  sudo reboot

Troubleshooting
***************

Yeti errors or EVerest not starting
===================================

Should your log output tell you something about "Yeti reset not successful"
or the EVerest modules get terminated right after EVerest started, it could
be due to the Yeti interface not being connected properly.

In this case, check the connections and the cable harness.

Should everything look fine, check if the Yeti firmware is running properly
by looking at the Yeti LED. It should flash in a fast frequency. If it is on
or off without flashing, the firmware could not be started or is not
installed.

.. _belaybox_new_yocto_based:
Temporary quick-and-dirty docs: New Yocto-based build
=====================================================

Install latest Yocto version
----------------------------

.. note::

  From June 2024 on, we will start changing the Debian-based to a Yocto-based
  image. As we will need some time to update our documentation accordingly,
  see a quick overview of how you can setup your hardware in the meantime.

For a new board (or previous Debian-based board), download the complete SD
image:

`<http://build.pionix.de:8888/release/yocto/belaybox-image-raspberrypi4-20240613154507.rootfs.wic.bz2>`_

Use balena etcher as described in the manual above, but use the downloaded
image instead.

The Yeti MCU also needs the corresponding firmware for the new Yocto image.
The firmware is included in the new image.

.. note::

  If you have purchased the YETI board after June 2024 the new firmware 2.1 is
  already on the YETI board.

Run these two commands once booted into the new image (the first one is very
important - do not update while EVerest/BaseCamp is running!):

.. code-block:: bash

  systemctl stop basecamp
  yeti_fwupdate /dev/serial0 /usr/share/everest/modules/YetiDriver/firmware/yetiR1_2.1_firmware.bin

After that, reset both Yeti and Yak!

The new ssh login credentials for the Yocto image are:

.. code-block:: bash

  user: root
  pw: basecamp

If you have the new Yocto installed already, you can update to this version
using this command:

.. code-block:: bash

  rauc install http://build.pionix.de:8888/release/yocto/belaybox-bundle-raspberrypi4-20240627101617.raucb

After installation is complete, run this to boot into the newly installed
update:

.. code-block:: bash

  tryboot

Use new toolchain for cross-compiling
-------------------------------------

If you want to cross compile your EVerest version, this is the toolchain to
use:

.. code-block:: bash

  http://build.pionix.de:8888/release/yocto/poky-glibc-x86_64-belaybox-image-cortexa7t2hf-neon-vfpv4-raspberrypi4-toolchain-4.0.16.sh

First of all you need to install it. It is a shell script, so just do a
"chmod +x name_of_toolchain.sh" and then run it with

.. code-block:: bash

  ./name_of_toolchain.sh

You will be asked where to install it. You can e.g. install it in your home
directory - somewhere like /etc/myuser/toolchain-belaybox

Then you need to source the environment variables (it tells you how to do it
at the end of the installation).

Once they are sourced, this terminal will cross compile.

In everest-core, create a folder called "build-cross". Change into it.

There, run cmake as follows:

.. code-block:: bash

  cmake .. -GNinja -DCMAKE_INSTALL_PREFIX=/var/everest -DEVEREST_ENABLE_PY_SUPPORT=OFF -DEVEREST_ENABLE_JS_SUPPORT=OFF -Deverest-core_USE_PYTHON_VENV=OFF

In this case, the PY/JS support flags are set to OFF. You may need to set them
to ON if you are using simulation. The last option
-Deverest-core_USE_PYTHON_VENV is only a temporarily needed directive that
will probably be obsolete in future release candidates.
The -GNinja can also be left out, then it will use make.

After that you can build with 

.. code-block:: bash

  make -j10 

or 

.. code-block:: bash

  ninja

depending on what you configured.

Once the build is complete, you can rsync directly to belaybox like this:

.. code-block:: bash

  DESTDIR=dist ninja install/strip && rsync -av dist/var/everest root@the.ip.add.ress:/var

Replace the IP address placeholder with the correct one.

Then log into the BelayBox and stop the systemd service:

.. code-block:: bash

  systemctl stop basecamp

Then you can run your self-compiled version like this:

.. code-block:: bash

  /var/everest/bin/manager --conf /path/to/my/configfile

Further potential necessary steps
---------------------------------

The new ssh login credentials for the Yocto image are:

.. code-block:: bash

  user: root
  pw: basecamp

The default config yaml file being used by the basecamp.service is the symlink
in /etc/everest/basecamp.yaml. It points to the config to be used. This can be
changed to a config to your liking.

Should you see any "Unknown config entry" errors when starting the manager
process, delete the corresponding config entries from the yaml file you are
using for startup.
