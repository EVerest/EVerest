.. _docker_setup:

############
Docker setup
############

****************
Prerequisites
****************

You need to install docker_ and docker-compose_. Furthermore, `visual
studio code`_ might be handy as a common integrated development
environment.

In order for custom or local containers being able to talk to the
services, provided by the *docker-compose* containers, we need to create
a common docker network.  It is called ``infranet_network`` and needs to
be created by the following command (IPv6 is enabled for containers
which might it)::

  docker network create --driver bridge --ipv6  --subnet fd00::/80 infranet_network --attachable

*****************************************
Start services without devcontainer setup
*****************************************

The following section describes how to start the services without using
the devcontainer setup. This is useful if you want to use one of the
services in a different environment or if you want to use the services
without the devcontainer setup.

.. note::

  Have a look at the section :ref:`How to Devcontainer <how_to_devcontainer>` if you want to use
  the more convenient devcontainer setup.

Control the services with docker-compose
========================================

The services can be controlled with docker-compose. The following
section describes how to start the services with docker-compose.

Checkout the everest-dev-environment repository and make use of the
docker-compose file in the ``docker/`` directory.

The following command will start all services defined in the docker-compose.yml
file connected to the `infranet_network` network.

.. code-block:: bash

  cd {EVerest Workspace Directory}
  git clone https://github.com/EVerest/everest-dev-environment.git
  cd everest-dev-environment/docker
  docker compose up -d

You can also start a single service by specifying the service name:

.. code-block:: bash

  docker compose up -d <service_name>

The following sections describe how to start the services individually without
the need of the docker-compose file.


Mosquitto MQTT broker
=====================

Use the deployed docker image `ghcr.io/everest/everest-dev-environment/mosquitto:docker-images-v0.1.0`
to start the mosquitto MQTT broker. The following command will start
the mosquitto MQTT broker connected to the `infranet_network` network.

.. code-block:: bash

    docker run -d --name mqtt-server --network infranet_network -p 1883:1883 -p 9001:9001 ghcr.io/everest/everest-dev-environment/mosquitto:docker-images-v0.1.0

``-d``: run the container in the background

``--name mqtt-server``: name the container `mqtt-server`

``--network infranet_network``: connect the container to the `infranet_network` network

``-p 1883:1883``: map the port, used for mqtt tcp connection, of the container to the host

``-p 9001:9001``: map the port, used for mqtt websocket connection, of the container to the host

``ghcr.io/everest/everest-dev-environment/mosquitto:docker-images-v0.1.0``: the docker image to use

MariaDB
=======

Use the image `mariadb:10.4.30` to start the MariaDB database. It is needed by the SteVe service.
The following command will start the MariaDB database connected to the `infranet_network` network.

.. code-block:: bash

    docker run \
      -d \
      --name ocpp-db \
      --network infranet_network \
      -p 13306:3306 \
      -e MYSQL_RANDOM_ROOT_PASSWORD="yes" \
      -e MYSQL_DATABASE=ocpp-db \
      -e MYSQL_USER=ocpp \
      -e MYSQL_PASSWORD=ocpp \
      mariadb:10.4.30

``-d``: run the container in the background

``--name ocpp-db``: name the container `ocpp-db`

``--network infranet_network``: connect the container to the `infranet_network` network

``-p 13306:3306``: map the port, used for sql tcp connection, of the container to the host

``-e MYSQL_RANDOM_ROOT_PASSWORD="yes"``: set a random root password

``-e MYSQL_DATABASE=ocpp-db``: create a database called `ocpp-db`

``-e MYSQL_USER=ocpp``: create a user called `ocpp`

``-e MYSQL_PASSWORD=ocpp``: set the password for the user `ocpp`

``mariadb:10.4.30``: the docker image to use

SteVe
=====

Use the image `ghcr.io/everest/everest-dev-environment/steve:docker-images-v0.1.0` to start the SteVe service.
The following command will start the SteVe service connected to the `infranet_network` network.

.. code-block:: bash

    docker run \
      -d \
      --name steve \
      --network infranet_network \
      -p 8180:8180 \
      -p 8443:8443 \
      ghcr.io/everest/everest-dev-environment/steve:docker-images-v0.1.0

``-d``: run the container in the background

``--name steve``: name the container `steve`

``--network infranet_network``: connect the container to the `infranet_network` network

``-p 8180:8180``: map the port, used for the web interface, of the container to the host

``ghcr.io/everest/everest-dev-environment/steve:docker-images-v0.1.0``: the docker image to use

How to use SteVe
----------------

  - ``:8180/steve/manager/home``: web interface (login = admin:1234)
  - ``:8180/steve/services/CentralSystemService``: SOAP endpoint for
    OCPP
  - ``:8180/steve/websocket/CentralSystemService/(chargeBoxId)``:
    WebSocket/JSON endpoint for OCPP

Local CI environment
====================

depricated.

If you want to generate the sphinx documenation locally,  you can use
the `ci-env` docker image.  In order to build the image locally::

    cd docker/ci-env
    docker build -t ci-env .

To generate the documentation, change to the project root and run::

    docker run -it --rm -v `pwd`:/work ci-env

The documentation will be found in ``docs/_build/html``.

Generating languange specific protobuf files
============================================

In order to create the protobuf implementation files for nanopb and
python, you can use the Dockerfile and scripts in
``docker/protobuf_generate``.  Change into that directory and then run::
    
    ./run.sh path_to_where_protobuf_files_reside

This will

#. Build a docker image (including python and protoc)

#. Run the created image with the specified folder mounted into the container

   #. Generate the language specific implementation files

   #. Zip these files into ``nanopb_pb_gen.zip`` and``python_pb_gen.zip``

#. Copy the zip files back to the host from the temporary container

#. Delete the container


.. _docker: https://docs.docker.com/engine/install/#server
.. _docker-compose: https://docs.docker.com/compose/install/#install-compose)
.. _visual studio code: https://code.visualstudio.com/docs/setup/linux
