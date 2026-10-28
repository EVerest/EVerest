.. _exp_linux_yocto_building_yocto:

#######################################
Building Yocto for your custom hardware
#######################################

A good starting point is to use a yocto distribution supplied by the SoM/Board/CPU manufacturer.
This ensures you have a working Linux distribution before starting to add EVerest.

Many SoM manufacturers provide quite well-maintained Yocto
distributions, e.g. PHYTEC provides an *ampliPHY* distribution for all
of their SoMs. That already solves a lot of things. We will look at that
in the next chapters.

.. tip::

   The SoM manufacturer may also already provide suitable build containers or
   other tools to help with integration into your CI/CD.

After the basic setup is done, you most likely will need to adapt the
device tree to your custom board to mux all pins correctly and map Linux
kernel drivers to the correct peripherals.

Once it boots correctly and all hardware is initialized in the kernel,
you can start with adding the *meta-everest* Yocto layer to your
bblayers.conf. You may also want to create your own layer for your image
files, EVerest config files, recipes for your own software etc. Look at
*meta-everest* as an example and refer to the Yocto documentation:

https://docs.yoctoproject.org/dev/dev-manual/layers.html

You can find *meta-everest* in a subdirectory of EVerest:
*EVerest/yocto/<yocto-release>/meta-everest*

Currently this includes support for the yocto *scarthgap* and *kirkstone* releases, and
experimental support for *wrynose*.
The recipes shared by all releases live in the layer
*EVerest/yocto/meta-everest-common*, which *meta-everest* adds by itself, so
*bblayers.conf* only needs *meta-everest*.

Then - in case you have not done it yet - create a custom image file
for your board that installs EVerest as well as all other tools you may
want to have on your base system. This image can be based e.g. on
*core-image-minimal*. Here is an an example from the BelayBox:

.. code-block:: bash

   require recipes-core/images/core-image-minimal.bb

   SUMMARY = "EVerest image for PIONIX BelayBox development kit"

   LICENSE = "MIT"

   CORE_IMAGE_EXTRA_INSTALL += "\
           everest-core \
           openssh \
           mosquitto \
           tzdata \
           pionixbox \
           flutter-pi \
           flutter-engine \
           fontconfig \
           ttf-roboto \
           htop \
           tmux \
       "

.. note::

   Since ``2026.10.0``, *meta-everest* has no individual recipes for the
   EVerest libraries inside the EVerest monorepo.
   ``everest-core`` includes all of these. If you have images or recipes
   written for older EVerest releases, please ensure that they do not rely on
   any of the following anymore: ``everest-framework``, ``everest-sqlite``,
   ``libcbv2g``, ``libevse-security``, ``libfsm``, ``libiso15118``, ``liblog``,
   ``libocpp``, ``libslac`` and ``libtimer``. You must remove these entries
   from your ``IMAGE_INSTALL``, ``CORE_IMAGE_EXTRA_INSTALL``, ``DEPENDS`` or
   ``RDEPENDS``, otherwise parsing will fail with ``Nothing PROVIDES`` or
   ``Nothing RPROVIDES``.

The minimal required packages are ``everest-core`` and ``mosquitto``. The
package ``tmux`` is only needed for the BringUp & Qualification tools.
Other debugging tools that may be useful during development phase are:

.. code-block:: bash

   tcpdump
   canutils
   tpm-tools
   open-plc-utils
   ethtool

.. note::

   The locale should be set to UTF-8 (otherwise BringUp & Qualification tools
   will look weird).

systemd service
===============

With ``systemd`` in ``DISTRO_FEATURES``, ``everest-core`` installs
``everest.service``. It starts only if ``/etc/everest/config.yaml`` exists and
is not empty, which your own layer provides, and runs:

.. code-block:: bash

   manager --config /etc/everest/config.yaml --db /var/lib/everest/everest.db \
           --settings /etc/everest/settings-mqtt-uds.yaml

As long as the boot slot of the configuration database holds no modules,
normally on the first start, it is seeded from the YAML, including
``/etc/everest/user-config/config.yaml`` if present. From then on the module
configuration comes from the database and edits of the YAML have no effect.
The YAML is still parsed on every start, so it has to stay valid: if it cannot
be parsed, the manager does not start even though the database holds a
complete configuration. See :ref:`exp-configuration-service` for the details.

The database holds no manager settings (MQTT broker, paths, telemetry). They
come from the installed example settings file ``settings-mqtt-uds.yaml``, see
:ref:`exp_config_service_settings`. A ``settings:`` block in ``config.yaml`` is
ignored.

Only the user running EVerest can access ``/var/lib/everest``, because module
configurations can hold credentials. The directory has to be persistent and
survive updates of the root file system, see
:ref:`partitioning-schemes-for-rauc-ota`.

To apply a changed YAML, delete the database, including its SQLite journal
files, while the service is stopped. This also deletes all other configuration
slots, e.g. those written through the configuration API:

.. code-block:: bash

   systemctl stop everest
   rm -f /var/lib/everest/everest.db*
   systemctl start everest

The experimental manager option ``--reset-from-yaml`` replaces only the boot
slot instead, and leaves the database untouched if the YAML is invalid, see
:doc:`/reference/manager-cli`.

To use other settings, override the command line in a drop-in of your layer.
Base your settings file on ``settings-mqtt-uds.yaml``: the TCP listener does not
give access to the internal topics.

.. code-block:: ini

   # /etc/systemd/system/everest.service.d/10-settings.conf
   [Service]
   ExecStart=
   ExecStart=/usr/bin/manager --config /etc/everest/config.yaml --db /var/lib/everest/everest.db --settings /etc/everest/settings.yaml

MQTT
----

``everest-core`` recommends the package ``everest-mosquitto-config``. It runs
Mosquitto with ``/etc/mosquitto/everest.conf`` instead of the ``mosquitto.conf``
of the ``mosquitto`` package:

- A Unix domain socket ``/run/mosquitto/mosquitto.sock`` for the manager and
  the modules, as recommended in :doc:`/how-to-guides/security-best-practices`.
  It is the only way to reach the internal ``everest/`` topics.
  ``/run/mosquitto`` has mode ``0750``, so only ``root`` and members of the
  group ``mosquitto`` can connect. Add the user running EVerest to that group.
- A TCP listener on ``localhost:1883`` without a password for local clients,
  e.g. clients of the EVerest APIs, or ChargeBridge, which only connects over
  TCP. ``/etc/mosquitto/everest-external.acl`` gives them every topic except
  the internal ``everest/`` topics; it assumes the default
  ``mqtt_everest_prefix``.

To configure Mosquitto yourself, leave the package out of your image with
``BAD_RECOMMENDATIONS += "everest-mosquitto-config"`` and pass EVerest a
matching settings file. To reach topics from the network, add a listener with
authentication and TLS.

.. note::

   Local clients that used the internal ``everest/`` topics over
   ``localhost:1883``, e.g. custom tools or Node-RED flows, no longer receive
   them: Mosquitto drops subscriptions and publishes the ACL denies without an
   error. Connect them to the Unix domain socket as a member of the group
   ``mosquitto``, or move them to the EVerest APIs.

.. note::

   The topics reachable over TCP include the management APIs of the manager,
   ``everest_api/1/configuration/`` and ``everest_api/1/lifecycle/``. They are
   only served when the manager runs with ``--configuration-api`` or
   ``--lifecycle-api``, see :ref:`exp_management_apis`. In ``rw`` mode, any
   local process can then change the configuration or stop and restart
   modules. To keep them from local clients, add
   ``topic deny everest_api/1/configuration/#`` and
   ``topic deny everest_api/1/lifecycle/#`` to the ACL, so they are only
   reachable over the Unix domain socket.

Rust modules
============

``everest-core`` builds its Rust modules (``RsPaymentTerminal``, ``RsIskraMeter``
and the Rust examples) when its ``PACKAGECONFIG`` contains ``rust``:

.. code-block:: bash

   PACKAGECONFIG:append:pn-everest-core = " rust"

The crates need Rust 1.86 or newer, while kirkstone ships 1.59 and scarthgap
1.75. The Yocto Project's
`meta-lts-mixins <https://git.yoctoproject.org/meta-lts-mixins/>`_ repository
has ``kirkstone/rust`` and ``scarthgap/rust`` branches that backport a current
toolchain under the standard recipe names, so it replaces poky's Rust for the
whole build. Clone the branch for your release and add the layer to your
``bblayers.conf``:

.. code-block:: bash

   git clone -b scarthgap/rust https://git.yoctoproject.org/meta-lts-mixins

If the Rust in the build is too old, ``everest-core`` and ``cxxbridge-cmd-native``
skip themselves with a message saying so.

The build runs without network access. The recipe fetches every crate of
``modules/Cargo.lock`` up front (listed in ``everest-core-crates.inc``) and the
git dependency of ``RsPaymentTerminal`` at the revision its ``Cargo.toml``
names. After changing a Rust module's dependencies, update ``modules/Cargo.lock``,
regenerate the crate list with ``bitbake -c update_crates everest-core`` and,
for a new zvt revision, ``SRCREV`` in ``everest-core-rust.inc``.

----

**Authors**: Cornelius Claussen, Manuel Ziegler
