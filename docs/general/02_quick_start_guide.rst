.. quick_start:

################################################
A Real Quick Guide To EVerest
################################################

************************************
Prepare Your Development Environment
************************************

Needed Packages
===============
You will need Python, Jinja2, PyYAML, a compiler and some more system libraries set up. See the detailed page for `setting up your development environment <03_detail_pre_setup.html>`_ to see some examples for operating systems.

After having created your environment, return back here, where we will go on with downloading and installing EVerest.

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
  edm --config ../everest-complete.yaml --workspace ~/checkout/everest-workspace

(In future, as your system has edm properly setup, you can always initialise a new workspace by calling *edm init*.)

edm will now prepare the most common repos to start with. It will also create a YAML file which describes your newly created workspace. You can change that YAML file later if you want to adopt the workspace to another scenario.

The YAML file can be found in the directory which you have chosen as workspace directory. In the above example, it is located at ~/checkout/everest-workspace.

EVerest Command Line Interface: ev-cli
--------------------------------------

In its current version, ev-cli supports you by generating module templates. It is also necessary to build EVerest.

To install ev-cli, change into the everest-utils/ev-dev-tools/ directory and install ev-cli:

.. code-block:: bash

  python3 -m pip install .

That is all to install ev-cli. You can find the binary file of ev-cli in your HOME directory in .local/bin/

In a later step, we will use ev-cli to create module stubs.

Packages for ISO 15118 communication
------------------------------------

To be able to build EVerest with ISO 15118 capability, you will have to
install the needed requirements for Josev:

.. code-block:: bash

  cd ~/checkout/everest-workspace/Josev
  python3 -m pip install -r requirements.txt


Build EVerest
=============

Now it is time to build EVerest:

.. code-block:: bash

  cd ~/checkout/everest-workspace/everest-core
  mkdir build
  cd build
  cmake ..
  make install

edm helped you to keep it that simple. Let's now dive into simulating our current workspace.

If you get an error during the build process stating that ev-cli is installed in an old version, go to your everest-workspace directory and call *edm --git-pull*. This will update the EVerest repositories. After that, repeat building ev-cli and you should be good to go again.

******************
Simulating EVerest
******************

Prepare The Helpers
===================
EVerest comes with prepared Docker containers, which are needed for simulation and further development. To get this working, make sure you have Docker and Docker-Compose installed during the previous install phase. (If not, see install instructions for `Docker <https://docs.docker.com/engine/install/#server>`_ and `Docker-Compose <https://docs.docker.com/compose/install/#install-compose)>`_!)

In order for custom or local containers being able to talk to the services, provided by the docker-compose containers, we need to create a common docker network. It is called `infranet_network` and needs to be created by the following command (IPv6 is enabled for containers which might it):

.. code-block:: bash

  docker network create --driver bridge --ipv6  --subnet fd00::/80 infranet_network --attachable

Now, change into the directory of the local everest-utils repo, which should have been cloned from Git by EDM before.

Enter directory `docker` and startup some containers:

.. code-block:: bash

  docker-compose up -d

This will give you the following services up and running:

- **Mosquitto MQTT broker** (service name: mqtt-server) with ports

  - ``1883``: mqtt tcp connection
  - ``9001``: mqtt websocket connection

- **mariadb** (service name: ocpp-db), sql database needed by **SteVe**

  - ``3306``: sql tcp connection

- **SteVe** (service name: steve) on port 8180 with endpoints

  - ``:8180/steve/manager/home``: web interface (login = admin:1234)
  - ``:8180/steve/services/CentralSystemService``: SOAP endpoint for
    OCPP
  - ``:8180/steve/websocket/CentralSystemService/(chargeBoxId)``:
    WebSocket/JSON endpoint for OCPP

That makes us ready for entering the simulation phase described in the next chapter.

Software in a loop
==================

Make sure you have prepared the helpers necessary for simulating EVerest as shown in the `previous section <02_quick_start_guide.html#prepare-the-helpers>`_.

After having done that, change to the directory /everest-core/build/, which has been created during EVerest install.

We will startup EVerest now with a software-in-a-loop (SIL) config.

Start the software-in-a-loop simulation via script:

.. code-block:: bash

  ./run-scripts/run-sil.sh

In a new terminal window, run the NodeRed script:

.. code-block:: bash

  ./run-scripts/nodered-sil.sh

This will let us control the simulation with the help of NodeRed.

You can analyse the output of the two scripts in the terminal windows to get a little bit of insights about what is going on and which ports are used etc.

If everything worked well, you will be able to reach a web GUI showing a charging process at *localhost:1880/ui*.

With that GUI, you can simulate charging states of a charging process in an electric vehicle.

You can play around with that a little bit to see some output in your two terminal windows. Try to get a first idea!

Admin Panel
===========

The Admin Panel gives you a nice overview of the modules and the connections between them.

As it resides in an own repository, which is not delivered automatically by edm in default, you will have to get the repo manually here: `EVerest Admin Panel <https://github.com/EVerest/everest-admin-panel>`_

You will have to install and run it via npm. After that, you can reach the Admin Panel locally via your standard web port 80.

A detailed walk-through to assist you with that is in preparation.

************
Module Setup
************

What parts does a module in EVerest consist of?

- Interface definition
- Types definition
- Module implementation

Get a more detailed insight into the module config and implementation files on the `EVerest Module Concept page <04_detail_module_concept.html>`_.

Here, we want to go on with setting up a module template to use that as a base for our own implementation.

*************************
Implementing a New Module
*************************

To create a new module in EVerest, we need to do some small steps shown in the following. No worries: We will go through them in more detail afterwards.

- Create a new subdirectory in the modules directory.
- Create a CMakeLists.txt (or borrow it from another module) with all needed libraries to build the module
- Create a manifest with information about which interface implementations are provided and which interfaces are required from connected modules.

Now, let's make ev-cli do its job of generating a module stub from a template:

.. code-block:: bash

  ev-cli mod create MyModuleName

The name of the module is the one given as directory name.

You will see that you get cpp and hpp files for your main module class and also for the interfaces to be implemented.

You main cpp file will have to special functions:

.. code-block:: c++

  void MyModuleName::init() {}
  void MyModuleName::ready() {}

When initialising, the EVerest framework will call all init() functions of all modules one after the other. After having initialised all modules in that way, the framework calls the ready() functions.

This allows you to do setup things that relate only to your current module in the init() function and all stuff requiring other modules being initialised in your ready() function.

.. attention:: 

  We will add additional documentation here soon to get you an idea about how vars can be published and how to interact with required modules from the outside. We will show callback functions and events and how all this works together in your module.
