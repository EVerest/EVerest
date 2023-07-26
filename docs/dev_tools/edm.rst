.. doc_tutorial_EDM

.. _edm_main:

***
edm
***

edm stands for EVerest dependency manager. It helps you orchestrating the
dependencies between the different EVerest repositories.

.. contents::
	:local:
	:backlinks: none

Dependency Manager for EVerest
##############################

Install and Quick Start
***********************

To install the **edm** dependency manager for EVerest you have to perform the
following steps.

Please make sure you are running a sufficiently recent version of **Python3 (>=3.6)** and that you are able to install Python packages from source.
See the *python3* command below for upgrading the required packages. Refer to
the
`Python Installing Packages <https://packaging.python.org/tutorials/installing-packages/#requirements-for-installing-packages>`_
documentation for indepth guidance if any problems arise.

.. code-block:: bash

  python3 -m pip install --upgrade pip setuptools wheel jstyleson jsonschema

Installing edm
**************

Now you can clone this repository and install **edm**:
(make sure you have `set up your ssh key in github <https://www.atlassian.com/git/tutorials/git-ssh>`_ first!)

A script for the steps below can be found `here <https://github.com/EVerest/everest-utils/tree/main/everest-cpp>`_.

.. code-block:: bash

  git clone git@github.com:EVerest/everest-dev-environment.git
  cd everest-dev-environment/dependency_manager
  python3 -m pip install .
  edm --config ../everest-complete.yaml --workspace ~/checkout/everest-workspace

The last command creates a workspace in the *~/checkout/everest-workspace* directory
from a `config that is shipped with this repository
<https://github.com/EVerest/everest-dev-environment/blob/main/everest-complete.yaml>`_.
The workspace will have the following structure containing all current dependencies
for EVerest:

.. code-block:: bash

	everest-workspace/
	├── everest-core
	├── everest-deploy-devkit
	├── everest-dev-environment
	├── everest-framework
	├── everest-utils
	├── liblog
	├── libmodbus
	├── libocpp
	├── libsunspec
	├── libtimer
	├── open-plc-utils
	├── RISE-V2G
	└── workspace-config.yamleverest-core

The workspace-config.yaml contains a copy of the config that was used to create
this workspace.

Enabling CPM_SOURCE_CACHE and setting PATH
******************************************
The EVerest dependency manager uses
`CPM <https://github.com/cpm-cmake/CPM.cmake>`_
for its CMake integration. This means you *can* and **should** set the
*CPM_SOURCE_CACHE* environment variable. This makes sure that dependencies
that you do not manage in the workspace are not re-downloaded multiple times.
For detailed information and other useful environment variables please
refer to the `CPM Documentation <https://github.com/cpm-cmake/CPM.cmake/blob/master/README.md#CPM_SOURCE_CACHE>`_.

Also set the PATH variable:

.. code-block:: bash

	export CPM_SOURCE_CACHE=$HOME/.cache/CPM
	export PATH=$PATH:/home/$(whoami)/.local/bin

Building EVerest
****************
Make sure you have installed :ref:`ev_cli <evcli_main>` first.
You can now use the following commands to build the repository everest-core:

.. code-block:: bash

  cd ~/checkout/everest-workspace/everest-core
  mkdir build
  cd build
  cmake ..
  make install

Python packages needed to run edm
*********************************
The following Python3 packages are needed to run **edm**. If you installed edm
using the guide above they were already installed automatically.

+ Python >= 3.6
+ Jinja2 >= 3.0
+ PyYAML >= 5.4

.. _cmake_integration_setup:

Setting up a workspace
######################
A sample workspace config, everest-complete.yaml, for the EVerest project is
provided in the root directory of this repository. You can set up this
workspace with the following command.

.. code-block:: bash

  edm --config ../everest-complete.yaml --workspace ~/checkout/everest-workspace

Updating a workspace
####################
To update a workspace you can edit the workspace-config.yaml file in the root
of the workspace. You can then use the following command to apply these
changes:

.. code-block:: bash

  edm init --workspace ~/checkout/everest-workspace

If you are currently in the everest-workspace directory the following command
has the same effect.

.. code-block:: bash

  edm init

Using the edm CMake module and dependencies.yaml
################################################

To use edm from CMake you have to add the following line to the top-level
CMakeLists.txt file in the respective source repository:

.. code-block:: bash

  find_package(EDM REQUIRED)

To define dependencies you can now add a dependencies.yaml file to your source
repository. It should look like this:

.. code-block:: bash

	---
	liblog:
	  git: git@github.com:EVerest/liblog.git
	  git_tag: main
	  options: ["BUILD_EXAMPLES OFF"]
	libtimer:
	  git: git@github.com:EVerest/libtimer.git
	  git_tag: main
	  options: ["BUILD_EXAMPLES OFF"]

Create a workspace config from an existing directory tree
#########################################################
Suppose you already have a directory tree that you want to save into a config
file. You can do this with the following command:

.. code-block:: bash

  edm --create-config custom-config.yaml

This is a short form of:

.. code-block:: bash

  edm --create-config custom-config.yaml --include-remotes git@github.com:EVerest/*

and only includes repositories from the EVerest namespace. You can add as many
remotes to this list as you want.

For example, if you only want to include certain repositories you can use the
following command.

.. code-block:: bash

  edm --create-config custom-config.yaml --include-remotes git@github.com:EVerest/everest* git@github.com:EVerest/liblog.git

If you want to include all repositories, including external dependencies, in
the config you can use the following command:

.. code-block:: bash

  edm --create-config custom-config.yaml --external-in-config

.. _git_information_at_a_glance:

Git information at a glance
###########################
You can get a list of all git repositories in the current directory and their
state using the following command:

.. code-block:: bash

  edm --git-info --git-fetch

If you want to know the state of all repositories in a workspace you can use
the following command:

.. code-block:: bash

  edm --workspace ~/checkout/everest-workspace --git-info --git-fetch

This creates output that is similar to the following example:

.. code-block:: bash

  [edm]: Git info for "~/checkout/everest-workspace":
  [edm]: Using git-fetch to update remote information. This might take a few seconds.
  [edm]: "everest-dev-environment" @ branch: main [remote: origin/main] [behind 6] [clean]
  [edm]: "everest-framework" @ branch: main [remote: origin/main] [dirty]
  [edm]: "everest-deploy-devkit" @ branch: main [remote: origin/main] [clean]
  [edm]: "libtimer" @ branch: main [remote: origin/main] [dirty]
  [edm]: 2/4 repositories are dirty.

Further information can be seen as shell output by calling edm with parameter
**-h** or **--help**. 
