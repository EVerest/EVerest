.. detail_pre_setup:

.. _preparedevenv_main:

####################################
Prepare Your Development Environment
####################################

A Word on System Environments
=============================

For a native build, EVerest requires a Linux based system. One reason for that
is for example Linux socket header files we use for networking.

To get building done on a Windows or Mac system, you can use WSL2 (Windows) or
Docker / Podman (Mac).

Required Packages
=================

To get EVerest running properly, you will mainly need the following packages:

* Python (greater than 3.6)
* Jinja2
* PyYAML
* Compiler:

  * GCC 9 (lower versions could work with some tweaking but are not
    recommended)
  * Clang (starting with version 12) has been used with EVerest but is not
    officially supported.

Make sure you have the Python packages pip, setuptools and wheel available,
which are needed for the EVerest dependency manager:

.. code-block:: bash

  python3 -m pip install --upgrade pip setuptools wheel

A complete list of libraries to be installed is given by the following best
practices which setup a development environment on a number of operating
systems.

Tested Environments
===================

Ubuntu
------

Use `apt` to get your needed libraries installed:

.. code-block:: bash

  sudo apt update
  sudo apt install -y python3-pip git rsync wget cmake doxygen graphviz build-essential clang-tidy cppcheck maven openjdk-11-jdk npm docker docker-compose libboost-all-dev nodejs libssl-dev libsqlite3-dev clang-format curl rfkill libpcap-dev libevent-dev

Please make sure to have `nodejs` installed with minimum version 10.20 for `node_api` version 6+. For updating to a supported version, please follow the install procedure here: `<https://github.com/nodesource/distributions/blob/master/README.md#installation-instructions>`_.

OpenSUSE
--------
Use `zypper` to get your needed libraries installed:

.. code-block:: bash

  zypper update && zypper install -y sudo shadow
  zypper install -y --type pattern devel_basis
  zypper install -y git rsync wget cmake doxygen graphviz clang-tools cppcheck boost-devel libboost_filesystem-devel libboost_log-devel libboost_program_options-devel libboost_system-devel libboost_thread-devel maven java-11-openjdk java-11-openjdk-devel nodejs nodejs-devel npm python3-pip gcc-c++ libopenssl-devel sqlite3-devel libpcap-dev libevent-devel

Fedora
------
Tested with Fedora 36, 37 and 38. Here is how to get your needed libraries with
`dnf`.

.. code-block:: bash

  sudo dnf update
  sudo dnf install make automake gcc gcc-c++ kernel-devel python3-pip python3-devel git rsync wget cmake doxygen graphviz clang-tools-extra cppcheck maven java-17-openjdk java-17-openjdk-devel boost-devel nodejs nodejs-devel npm openssl openssl-devel libsqlite3x-devel curl rfkill libpcap-devel libevent-devel

Now, it's time to continue with the
:ref:`Quick Start Guide to install EVerest <quickstartguide_main>`.
