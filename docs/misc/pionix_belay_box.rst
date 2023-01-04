.. doc_pionix_belay-box

Pionix BelayBox
################

Introduction
************

Detailed description of the hardware
------------------------------------
While we will add some more information regarding the hardware here in future, we are very happy to point you to the open hardware design and open source firmware repositories of the BelayBox:

* `Yeti and Yak Hardware Reference Design <https://github.com/PionixPublic/reference-hardware>`_
* `Yeti Firmware <https://github.com/PionixPublic/yeti-firmware>`_

For more information about vendors working with EVerest, contact us via the `EVerest mailing list <https://lists.lfenergy.org/g/everest>`_. 

Installation/mounting
---------------------

Raspbian
********

BelayBox uses Raspian (a debian flavour for the Raspberry Pi) as a main 
operating system for development purposes.
For deployment on real products you should consider using Yocto or similar 
instead.

Partitioning scheme
-------------------

BelayBox uses a different partitioning scheme then vanilla raspian. The reason 
for this is it supports A/B root 
partitions for updates. This way an update can be downloaded and installed 
while the Box is in operation, even 
while charging.
When rootfs A is booted, new updates will be installed to partition B and vice 
versa. After succesfull installation
an atomic flag is set in the Raspberry Pi bootloader to try one boot of the
newly installed system.
If it boots succesfully, the changes are made permanent. If not, it 
automatically falls back to the previous version
on the next boot. 

The SD card has the following partitions:

.. code-block::

    Device         Boot    Start      End  Sectors  Size Id Type
    /dev/mmcblk0p1          8192  1056767  1048576  512M  c W95 FAT32 (LBA)
    /dev/mmcblk0p2       1056768 14688255 13631488  6.5G 83 Linux
    /dev/mmcblk0p3      14688256 28319743 13631488  6.5G 83 Linux
    /dev/mmcblk0p4      28319744 30564351  2244608  1.1G  f W95 Ext'd (LBA)
    /dev/mmcblk0p5      28327936 28459007   131072   64M 83 Linux
    /dev/mmcblk0p6      28467200 30564351  2097152    1G 83 Linux

``/dev/mmcblk0p1``: Boot partition.
This is used for both root partitions due to limitations
in the Raspberry Pi bootloader. It contains two subdirectories 
(system0 and system1) with the boot files of the two installed root partitions.

``/dev/mmcblk0p2``: Root partition A. Read only.

``/dev/mmcblk0p3``: Root partition B. Read only.

``/dev/mmcblk0p4``: Extented (container for 5-6)

``/dev/mmcblk0p5``: Factory data.

The contents will be written once during production and should not be changed.
Mounted under ``/mnt/factory_data``

``/dev/mmcblk0p6``: User data.
Only writable partition. All data generated during the use of the box will be
stored here. Also various configuration overrides can be set here, see Cheat
sheet.
Mounted under ``/mnt/user_data``. Format this partition to reset the BelayBox
to factory defaults.


Using Online updates
--------------------

BelayBox comes with a very simple online update tool that is controlled by 
two systemd services:

``ota-update.service``: This service starts a shell script that checks for 
online updates on Pionix update servers. It is triggered by the second systemd 
service:

``ota-update.timer``: This is the systemd timer unit that starts 
``ota-update.service`` on regular intervals.

To disable online updates use ``sudo systemctl disable ota-update.timer``.
The online update updates always the full root partition, partial updates
are not implemented yet. All data that needs to survive the update needs
to be stored in ``/mnt/user_data``.

The root partition should normally never be modified, it is read only. All 
changes will also be lost on the next online update.

If you still want to modify something, use the ``rw`` and ``ro`` commands
to re-mount root read-write/read-only.

In rw mode you can e.g. use ``sudo apt install ...`` to install new software.
Disable online update if you need the changes to stay.

EVerest
*******

EVerest is the charging software on the BelayBox that controls charging, 
cloud access, autorization, energy management, the display app etc. 

Integration into Raspbian
-------------------------

EVerest is installed under ``/opt/everest``. Since this folder is in the 
root partition it is also updated with the regular online update.

The systemd service ``everest.service`` starts EVerest at boot if no custom
everest installation is found under ``/mnt/user_data/opt/everest``.

The systemd service ``everest-dev.service`` starts EVerest at boot from
 ``/mnt/user_data/opt/everest`` if that exists.

The systemd service ``display-app.service`` starts the flutter based
display application.

Update Yeti's microcontroller firmware
--------------------------------------

The Yeti Power Board is controlled by an STM32 microcontroller that is 
responsible for the lowest level state machine and all electrical safety.
In the future updates will be installed automatically, for now they can be 
installed manually. The firmware has been open sourced, see `Yeti Firmware <https://github.com/PionixPublic/yeti-firmware>`_

In your normal workflow updating this firmware is not needed.

The microcontroller is not protected (remember this is a dev kit and not
a real product), you can use the update tool that comes with the yeti
Everest driver module:

``/opt/everest/bin/yeti_fwupdate /dev/serial0 new-firmware.bin``

This will reboot the microcontroller in firmware update ROM bootloader and
uses stm32flash tool to upload the new firmware.

Developing with EVerest and BelayBox
************************************

You can use make or ninja with cmake. The examples here are given with make.

Setup cross compile environment
-------------------------------

First, make sure you have successfully built EVerest natively on your laptop as 
described here: https://github.com/EVerest/everest-core#everest-core

Download and untar the bullseye-toolchain:

.. code-block:: bash

  wget http://build.pionix.de:8888/release/toolchains/bullseye-toolchain.tgz
  tar xfz bullseye-toolchain.tgz

Change directory to everest-core in your workspace e.g.:

.. code-block:: bash

  cd ~/checkout/everest-workspace/everest-core

Cross-compile by changing the given paths accordingly:

.. code-block:: bash

  cmake \
   -DCMAKE_TOOLCHAIN_FILE=/full-path-to/bullseye-toolchain/toolchain.cmake \
   -DCMAKE_INSTALL_PREFIX=/mnt/user_data/opt/everest \
   -S . -B build-cross


Now build EVerest with the following commands:

.. code-block:: bash

  make -j$(nproc) -C build-cross
  make -j$(nproc) DESTDIR=./dist -C build-cross install

Deploy a custom EVerest on BelayBox
-----------------------------------

The binaries are now installed under ``build-cross/dist``.
You can use ``rsync`` within the ``build-cross`` folder to copy the files to 
BelayBox:

``rsync -a build-cross/dist/mnt/user_data/opt/everest/* everest@the.ip.add.res:/mnt/user_data/opt/everest``

The first time you need to create the folder ``/mnt/user_data/opt/everest`` 
on the BelayBox before syncing
(``ssh everest@the.ip.add.res mkdir -p /mnt/user_data/opt/everest``)

You can also copy to another folder on the BelayBox, but using 
``/mnt/user_data/opt/everest`` will make your new custom everest installation
auto start at boot (see ``everest-dev.service``). This way you can have a
custom installation and still use the online updates for the base system.

If you do it for the first time, reboot BelayBox so that 
``everest-dev.service`` is used from now-on instead of ``everest.service``.

Reference Cheat sheet
---------------------

* rw: make root partition read/writable
* ro: make it read only again
* /mnt/user_data/etc/wpa_supplicant.conf: file containing wifi settings
* /mnt/user_data/opt/everest/<everest binaries TBD> force the use of custom 
    everest build or config by automated start of ``everest-dev.service``
    instead of ``everest.service``
* /mnt/user_data/etc/update_channel contains either stable or unstable to 
    define release channels
* /mnt/user_data/etc/wireguard/<wireguard interface name>.conf for a wireguard 
    VPN configuration
* /mnt/user_data/user-config/config-deploy-devboard.json for a persistent user 
    config containing only the diffs to the default config.
* to stop automatic updates: rw; sudo systemctl disable ota-update.timer
* /mnt/user_data/etc/mosquitto/conf.d: 
    here you can add additional config files
    for the mqtt broker. For example a “public_mqtt.conf” file with the 
    following contents:
    ``listener 1883``
    ``allow_anonymous true`` to allow anonymous external connections to the 
    mqtt broker for debugging purposes
* ``sudo journalctl -fu everest.service``: watch the output of everest.service 
* ``sudo journalctl -fu everest-dev.service``: 
    watch the output of ``everest-dev.service`` 
* ``sudo /opt/everest/bin/run.sh``
    run EVerst in the terminal (Make sure the 
    systemd service is not running)
