.. _how_to_devcontainer:

###########################
How to Devcontainer
###########################

The devcontainer setup is a convenient way to develop the EVerest project. It uses
Docker to create a development environment that is consistent across different
machines. This section describes how to set up the devcontainer for the EVerest
project and how to use it.

****************
Prerequisites
****************

You need to install docker_, docker-compose_ and `visual
studio code`_.

*******************************************
Create devcontainer based EVerest workspace
*******************************************

Enter the workspace directory
=============================

Create a new directory and navigate into it.
This directory will be your new workspace.
(You could also use an existing one.)

.. code-block:: bash

    mkdir my_workspace
    cd my_workspace

Run the setup script
====================

Run the following command to setup the devcontainer.

.. code-block:: bash

    export BRANCH="main" && bash -c "$(curl -s --variable %BRANCH=main --expand-url https://raw.githubusercontent.com/EVerest/everest-dev-environment/{{BRANCH}}/devcontainer/setup-devcontainer.sh)"

The script will ask you for the following information:

#. Workspace directory: Default is the current directory. You can keep the default by pressing enter.
#. everest-dev-environment version: Default is 'main'. You can keep the default by pressing enter.

Open the workspace in Visual Studio Code
========================================

After the script has finished, open the workspace in Visual Studio Code.

.. code-block:: bash

    code .

VS Code will ask you to reopen the workspace in a container. Click on the button "Reopen in Container).

Getting started
===============

As your set up dev environment suggests when you open a terminal, you can setup your EVerest workspace by running the following command:

.. code-block:: bash

    everest clone everest-core

******************************
The Everest's Development Tool
******************************

.. note::

    A more detailed documentation of the tool is coming soon.

The Everest's development tool is a command-line tool that helps you to manage your EVerest workspace.
You can use it to clone repositories and start/stop services.

.. _docker: https://docs.docker.com/engine/install/#server
.. _docker-compose: https://docs.docker.com/compose/install/#install-compose)
.. _visual studio code: https://code.visualstudio.com/docs/setup/linux
