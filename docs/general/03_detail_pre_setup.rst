.. detail_pre_setup:

####################################
Prepare Your Development Environment
####################################

To get EVerest running properly, you will mainly need the following packages:

* Python (greater than 3.6)
* Jinja2
* PyYAML
* Compiler:

  * GCC 9 (lower versions could work with some tweaking but are not recommended)
  * Clang (starting with version 12) has been used with EVerest but is not officially supported.

A complete list of libraries to be installed is given by the following best practices which setup a development environment on a number of operating systems.

Ubuntu
======

Use `apt` to get your needed libraries installed:

.. code-block:: bash

  sudo apt update
  sudo apt install -y python3-pip git rsync wget cmake doxygen graphviz build-essential clang-tidy cppcheck maven openjdk-11-jdk npm docker docker-compose libboost-all-dev nodejs libssl-dev libsqlite3-dev clang-format curl rfkill libpcap-dev

Please make sure to have `nodejs` installed with minimum version 10.20 for `node_api` version 6+. For updating to a supported version, please follow the install procedure here: `<https://github.com/nodesource/distributions/blob/master/README.md#installation-instructions>`_.

OpenSUSE
========
Use `zypper` to get your needed libraries installed:

.. code-block:: bash

  zypper update && zypper install -y sudo shadow
  zypper install -y --type pattern devel_basis
  zypper install -y git rsync wget cmake doxygen graphviz clang-tools cppcheck boost-devel libboost_filesystem-devel libboost_log-devel libboost_program_options-devel libboost_system-devel libboost_thread-devel maven java-11-openjdk java-11-openjdk-devel nodejs nodejs-devel npm python3-pip gcc-c++ libopenssl-devel sqlite3-devel libpcap-dev

Fedora
======
Tested with Fedora 36. Here is how to get your needed libraries with `dnf`.

.. code-block:: bash

  sudo dnf update
  sudo dnf install make automake gcc gcc-c++ kernel-devel python3-pip git rsync wget cmake doxygen graphviz clang-tools-extra cppcheck maven java-11-openjdk java-11-openjdk-devel boost-devel nodejs nodejs-devel npm openssl-devel libsqlite3x-devel curl rfkill libpcap-devel

Now, it's time to continue with the `Quick Start Guide to install EVerest <02_quick_start_guide.html#download-and-install>`_.