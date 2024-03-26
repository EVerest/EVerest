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

    Create the infranet_network Docker network:

    .. code-block::

        docker network create --driver bridge --ipv6  --subnet fd00::/80 infranet_network --attachable || true

    Stand up Everest MQTT and Node-RED Docker containers:

    .. code-block::

        docker compose -f "./everest-utils/docker/docker-compose-dev.yml" up -d mqtt-server
        docker compose -f "./everest-utils/docker/docker-compose-dev.yml" up -d nodered

    There is also a script version of this inside the EVerest Utilities repository.

    .. code-block::

        bin/devup

#. Open the dev container inside VSCode

    * Press `CMD + Shift + P`
    * type `Dev Containers: Open Folder in Container...`

    .. image:: docs/tutorials/how_to_mac/img/shot1_cmd_p.png
      :width: 400
      :alt: Use the Command Pallet to open the Dev Container

#.
