.. quick_start:

.. _quickstartguide_main:

################################################
A Real Quick Guide To EVerest
################################################

************************************
Prepare Your Development Environment
************************************

Needed Packages
===============
You will need Python, Jinja2, PyYAML, a compiler and some more system libraries
set up. See the detailed page for
:ref:`setting up your development environment <preparedevenv_main>` to see some
examples for operating systems.

After having created your environment, return back here, where we will go on
with downloading and installing EVerest.

.. _quickstartguide_download_install:

********************
Download And Install
********************

Get The Needed EVerest Repositories
===================================

EVerest Dependency Manager - edm
--------------------------------

As EVerest is highly modular, you will need multiple repos, which can be found on GitHub.

To get the right repos as needed, the EVerest Dependency Manager - short edm - will help you.

To start with that, let's get edm ready to work.

You will first of all need to pull *everest-dev-environment* to your development environment.

Python and its tools pip, setuptools and wheel have already been installed in the Prerequisites section above. So, you can pull the said repository and install the dependency manager, which will reside inside of .local/bin/ in your HOME directory:

.. code-block:: bash

  git clone git@github.com:EVerest/everest-dev-environment.git
  cd everest-dev-environment/dependency_manager
  python3 -m pip install .

To let edm prepare the most common repos for a simple start with EVerest,
let us use a default config file and set a workspace directory for the repos.
Set your preferred directory instead of `{EVerest Workspace Directory}`, e.g.
use `~/checkout/everest-workspace`.

.. code-block:: bash

  edm --config ../everest-complete.yaml --workspace {EVerest Workspace Directory}

(In future, as your system has edm properly setup, you can always initialise a
new workspace by calling *edm init*.)

edm will now prepare the most common repos to start with. It will also create a
YAML file which describes your newly created workspace. You can change that
YAML file later if you want to adopt the workspace to another scenario.

The YAML file can be found in the directory which you have chosen as workspace
directory. In the above example, it is located at

`{EVerest Workspace Directory}`.

EVerest Command Line Interface: ev-cli
--------------------------------------

In its current version, ev-cli supports you by generating module templates. It
is also necessary to build EVerest.

To install ev-cli, change into the everest-utils/ev-dev-tools/ directory and
install ev-cli:

.. code-block:: bash

  python3 -m pip install .

That is all to install ev-cli. You can find the binary file of ev-cli in your
HOME directory in .local/bin/

In a later step, we will use ev-cli to create module stubs.

Packages for ISO 15118 communication
------------------------------------

To be able to build EVerest with ISO 15118 capability, you will have to
install the requirements for Josev:

.. code-block:: bash

  cd {EVerest Workspace Directory}/Josev
  python3 -m pip install -r requirements.txt


Build EVerest
=============

Now it is time to build EVerest:

.. code-block:: bash

  cd {EVerest Workspace Directory}/everest-core
  mkdir build
  cd build
  cmake ..
  make install

edm helped you to keep it that simple. Let's now dive into simulating our
current workspace.

If you get an error during the build process stating that ev-cli is installed
in an old version, go to your everest workspace directory and call *edm
--git-pull*. This will update the EVerest repositories. After that, repeat
building ev-cli and you should be good to go again.

******************
Simulating EVerest
******************

.. _quickstartguide_helpers:

Prepare The Helpers
===================
EVerest comes with prepared Docker containers. The one that starts Mosquitto
(an MQTT broker) is required to run EVerest. This documentation section shows
the necessary steps to start the simulation and get the user interface running.

Further tools are not required to run EVerest (e.g. SteVe for OCPP). Further
information about EVerest Docker containers can be found on the
`EVerest Docker Setup page <../tutorials/docker_setup.html>`_.

.. hint::
  To get all this working, make sure you have docker and docker-compose installed during the previous install phase. (If not, see install instructions for `docker <https://docs.docker.com/engine/install/#server>`_ and `docker-compose <https://docs.docker.com/compose/install/#install-compose)>`_!)

In order for custom or local containers being able to talk to the services,
provided by the docker-compose containers, we need to create a common docker
network. It is called `infranet_network` and needs to be created by the
following command (IPv6 is enabled for containers which might need it):

.. code-block:: bash

  docker network create --driver bridge --ipv6  --subnet fd00::/80 infranet_network --attachable

Now, change into your workspace directory and enter the directory with the
prepared docker containers. Start the the mosquitto broker which will be
used by EVerest for the communication between the EVerest modules:

.. code-block:: bash

  cd {EVerest Workspace Directory}/everest-utils/docker
  sudo docker-compose up -d mqtt-server

That makes us ready for entering the simulation phase described in the next
chapter.

Software in a loop
==================

In the following, we will start EVerest as a simple AC charging station with
software-in-the-loop configuration. This means that all hardware related
parts like Powermeter, RFID-Reader or even actual Electric Vehicles are
loaded as simulated modules.

.. hint::

  Make sure you have prepared the helpers necessary for simulating EVerest as
  shown in the :ref:`previous section <quickstartguide_helpers>`.

Change to the directory `everest-core/build`, which has been created during
EVerest install.

Start EVerest with a software-in-the-loop configuration via script:

.. code-block:: bash

  {EVerest Workspace Directory}/everest-core/build/run-scripts/run-sil.sh

In a new terminal window, run the Node-RED script:

.. code-block:: bash

  {EVerest Workspace Directory}/everest-core/build/run-scripts/nodered-sil.sh

For a user interface, just direct your browser to `http://localhost:1880/ui` -
the required web-server has already been started via the shell scripts.

This will let us control the simulation in a very simple GUI.

You can analyse the output of the two shell scripts in the terminal windows to
get a little bit of insights about what is going on and which ports are used
etc.

In the GUI, you can simulate car charging sessions using the available buttons,
e.g. `CAR PLUGIN`, `PAUSE`, `RESUME` and so on:

.. image:: img/quick-start-sil-gui.png
  :width: 200px

Your own simulations
====================

To use simulation with your own custom flows, visit `Tuturial For Simulating EVerest <../tutorials/run_sil/index.html>`_.

Admin Panel
===========

The Admin Panel gives you a nice overview of the modules and the connections
between them.

As it resides in an own repository, which is not delivered automatically by edm in default, you will have to get the repo manually here: `EVerest Admin Panel <https://github.com/EVerest/everest-admin-panel>`_

You will have to install and run it via npm. After that, you can reach the
Admin Panel locally via your standard web port 80.

A detailed walk-through to assist you with that is in preparation.

.. _quickstartguide_modulesetup:

************
Module Setup
************

What parts does a module in EVerest consist of?

- Interface definition
- Types definition
- Module implementation

Get a more detailed insight into the module config and implementation files on
the :ref:`EVerest Module Concept page <moduleconcept_main>`.

Here, we want to go on with setting up a module template to use that as a base
for our own implementation.

*************************
Implementing a New Module
*************************

To create a new module in EVerest, we need to do some small steps shown in the
following. No worries: We will go through them in more detail afterwards.

- Create a new subdirectory in the modules directory.
- Create a CMakeLists.txt (or borrow it from another module) with all
  required libraries to build the module
- Create a manifest with information about which interface implementations are
  provided and which interfaces are required from connected modules.

Now, let's make ev-cli do its job of generating a module stub from a template:

.. code-block:: bash

  ev-cli mod create MyModuleName

The name of the module is the one given as directory name.

You will see that you get cpp and hpp files for your main module class and also
for the interfaces to be implemented.

Your main cpp file will have two special functions:

.. code-block:: c++

  void MyModuleName::init() {}
  void MyModuleName::ready() {}

When initialising, the EVerest framework will call all init() functions of all
modules one after the other. After having initialised all modules in that way,
the framework calls the ready() functions.

This allows you to do setup things that relate only to your current module in
the init() function and all stuff requiring other modules being initialised in
your ready() function.

.. hint::

  We will add additional documentation here soon to get you an idea about how vars can be published and how to interact with required modules from the outside. We will show callback functions and events and how all this works together in your module.
