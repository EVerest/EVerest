.. _tutorial_mac

##########################################
How To: Develop on a Mac
##########################################

This is a tutorial on how to setup your Mac
to develop on EVerest.

******************************************
Application Setup
******************************************

#. Install Homebrew

    Follow the instructions on the `brew.sh <https://brew.sh/>`_.

    It may ask you for your password, but don't worry, that is expected.

#. Install Docker Desktop

    Follow the instructions from
    `Install Docker Desktop on Mac <https://docs.docker.com/desktop/install/mac-install/>`_.

#. Install VSCode

    You can either download it from the
    `VSCode website <https://code.visualstudio.com/>`_, or from Homebrew:

    .. code-block::

        brew install --cask visual-studio-code

#. Install VSCode's Dev Containers Extension

    Once you have VSCode up and running, follow the instructions at the
    `Dev Container extension's page <https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers>`_.

******************************************
GitHub Setup
******************************************

#. Create an ssh keypair

    .. code-block::

        ssh-keygen -t rsa -a 100 -Z aes128-gcm@openssh.com

#. Create a file named `config` in the `~/.ssh` folder

    .. code-block::

        touch ~/.ssh/config

    Add the following to the file:

    .. code-block::

        AddKeysToAgent yes
        User git
        PubkeyAcceptedAlgorithms +ssh-rsa

#. Checkout the EVerest Utils repository

    .. code-block::

        > git@github.com:EVerest/everest-utils.git
        > cd everest-utils

    The `EVerest utilities <https://github.com/EVerest/everest-utils>`_
    GitHub repository contains the dev container that you will need to develop on a Mac.

******************************************
Standup the Dev Docker Environment
******************************************

#. Stand up the background Docker network and containers

    From the project root of everest-utils, create the infranet_network Docker network:

    .. code-block::

        docker network create --driver bridge --ipv6  --subnet fd00::/80 infranet_network --attachable || true

    Stand up Everest MQTT and Node-RED Docker containers:

    .. code-block::

        docker compose -f "./docker/docker-compose.yml" up -d mqtt-server
        docker compose -f "./docker/docker-compose.yml" up -d nodered

    There is also a script version of this inside the EVerest Utilities repository.

    .. code-block::

        bin/devup

#. Open the dev container inside VSCode

    * Press `CMD + Shift + P`
    * type `Dev Containers: Open Folder in Container...`

    .. image:: img/shot1_cmd_p.png
      :alt: Use the Command Pallet to open the Dev Container

#. Navigate and open the docker/everest-playground folder.

    .. image:: img/shot2_open_folder.png

    This will open the EVerest Playground as a VSCode dev container.

#. Initialize your environment

    Now we run commands inside the Playground based on the instructions from the `/everest-cpp/README.md` file.

    Initialize the EVerest workspace sourcing the `./init.sh` file:

    .. image:: img/shot3_init.png
      :alt: ./init.sh

    The working directory will be changed to `everest-core/build`. Here you can build the project:

    .. code-block::

        cmake .. && make install


    You can also use `make's -j flag <https://www.gnu.org/software/make/manual/html_node/Parallel.html>`_ to speed up
    the build:

    .. code-block::

        cmake .. && make install -j12

    .. image:: img/shot4_install.png
      :alt: ./make install

    Now you're ready to go.

******************************************
EVerest SIL Simulation
******************************************

    .. code-block::

        ./dist/bin/manager --config ../config/config-sil-dc.yaml

    This starts the EVerest Manager with the modules loaded determined by the `config-sil-dc.yaml` config file.

    You can interact with it using the Node-RED UI by opening your browser to `http://localhost:8849/`.

    .. image:: img/shot5_admin_panel.png
      :alt: EVerest Admin Panel

Stopping things
==========================================

    .. code-block::

        Code > File > Close Remost Connection

Resources
==========================================

* `Node-RED <https://nodered.org/>`_
