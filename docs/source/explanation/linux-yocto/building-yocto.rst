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

Currently this includes support for the yocto *scarthgap* and *kirkstone* releases.

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
           libocpp \
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

   libocpp is part of everest-core as of today (2026), whereas older version
   still rely on libocpp being hosted separately.

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

Rust modules
============

``everest-core`` builds its Rust modules (``RsPaymentTerminal``, ``RsIskraMeter``
and the Rust examples) when its ``PACKAGECONFIG`` contains ``rust``:

.. code-block:: bash

   PACKAGECONFIG:append:pn-everest-core = " rust"

The crates need Rust 1.82 or newer, while scarthgap ships 1.75. The Yocto
Project's `meta-lts-mixins <https://git.yoctoproject.org/meta-lts-mixins/>`_
repository has a ``scarthgap/rust`` branch that backports a current toolchain
under the standard recipe names, so it replaces poky's Rust for the whole
build. Clone that branch and add the layer to your ``bblayers.conf``:

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
