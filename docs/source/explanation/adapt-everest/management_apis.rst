.. _exp_management_apis:

###############
Management APIs
###############

While technically part of the :ref:`EVerest APIs <everest_apis>`, the management APIs
are a special case, since they are not intended for hardware integration or custom
extensions, but rather for remote management of EVerest itself. They are used for
example to manage the configuration of EVerest, e.g. to update configuration parameters
or to manage configuration sets, e.g. create/activate configurations.

There are two distinct management APIs available: The configuration_API and the
lifecycle_API. As use-cases demand both of them to be available when modules are not
running, they are implemented as part of the central manager process. They are
disabled by default and need to be enabled explicitly by adding the respective
command line argument to the EVerest startup command.

The APIs
========

The management APIs can be used independently of each other, i.e. it is possible to activate
only one of them. However, they can also be used together, e.g. to load an entirely new
configuration from a yaml string while EVerest is running and to restart the modules to
apply the new configuration.

configuration_API
-----------------

The configuration_API provides functionality to manage the configuration of EVerest.
This includes

- the management of configuration slots, i.e. creating, deleting or activating slots
- loading raw yaml data via the API, e.g. to load a new configuration slot with the
  content of a yaml file
- the management of configuration parameters, e.g. to update the value of a module
  configuration parameter for during runtime.
- querying the current configuration slots, e.g. to fetch the list of available
  configuration slots or the details of a specific slot.

The full specification can be found in the `AsyncAPI HTML documentation <../../reference/api/configuration_API/index.html>`__ .

To activate the configuration_API, use the ``--configuration-api`` flag when starting
EVerest, e.g. with the following command in your *build* folder:

.. code-block:: bash

   ./run-scripts/run-sil.sh --configuration-api

By default, the configuration_API is started in read-only mode, which means that only the
querying functionality is available. To enable the full functionality, start the API in
read-write mode by appending ``=rw`` to the flag, e.g. with the following command:

.. code-block:: bash

   ./run-scripts/run-sil.sh --configuration-api=rw

lifecycle_API
-------------

The lifecycle_API provides functionality to manage the lifecycle of EVerest modules.
This includes

- starting and stopping modules
- fetching the status of modules, e.g. whether they are running or not.

The full specification can be found in the `AsyncAPI HTML documentation <../../reference/api/lifecycle_API/index.html>`_ .

To activate the lifecycle_API, use the ``--lifecycle-api`` flag when starting
EVerest, e.g. with the following command in your *build* folder:

.. code-block:: bash

   ./run-scripts/run-sil.sh --lifecycle-api

By default, the lifecycle_API is started in read-only mode, which means that only the
querying functionality is available. To enable the full functionality, start the API in
read-write mode by appending ``=rw`` to the flag, e.g. with the following command:

.. code-block:: bash

   ./run-scripts/run-sil.sh --lifecycle-api=rw

Clients
=======

EVerest ships a command line client for both APIs,
:ref:`everest-management-api-cli <exp_dev_tools_management_api_cli>`, which
is installed into the Python venv of the build directory. It covers every
request of both APIs, prints the replies as JSON and can follow the status
and notification topics. The :ref:`management APIs tutorial <tutorial_management_apis>`
uses it throughout.

The integration tests in ``tests/management_api_tests`` drive the APIs through
the client modules of that package and are a good reference when writing your
own client: all that is needed is an MQTT client that publishes a request
envelope and subscribes to the reply topic named in it (see
:ref:`MQTT Topics <exp_management_apis_topics>`).

AsyncAPI specification
======================

*Messages* are named to match the client's perspective. Variables which a client
subscribes to are named ``receive_{message_name}``. Commands which a client
can request to be executed are named ``send_request_{message_name}``. The
response to a command is named ``receive_reply_{message_name}``.

An example for the *active_slot* variable:

- message for the variable: *receive_active_slot*

Here is an example for the *list_all_slots* command of the configuration_API:

- message for the command: *send_request_list_all_slots*
- message for the response: *receive_reply_list_all_slots*


.. _exp_management_apis_topics:

MQTT Topics
===========

The MQTT topics of the management APIs follow not quite the same but
similar pattern as the other EVerest APIs. All topics are prefixed with
*everest_api/1/{api_type}/* - with *1* being the version and *{api_type}*
the type of the API. Opposed to the other EVerest APIs, the management APIs
do not have a module_id in the topic, since they are not implemented as
separate modules.

The prefix is followed by the direction of the message. There are two
options:

-  *m2e*: "module to EVerest" for messages from the client to EVerest
-  *e2m*: "EVerest to module" for messages from EVerest to the client

This is finally followed by the name of the message. Here is a complete
example:

.. code-block:: text

   everest_api/1/configuration/e2m/active_slot
   everest_api/1/configuration/m2e/list_all_slots

The reply topic for a command is not fixed but is specified in the ``replyTo``
field of the command message. This allows clients to specify their own topic
for receiving the response, e.g. to distinguish responses from different
commands as well as from different invocations of the same command.

Request and reply messages
==========================

A command is sent as a *RequestReply* envelope: the ``headers`` object names
the topic the reply is expected on, the ``payload`` object carries the
arguments of the command (an empty object for commands without arguments).
The manager publishes the bare result object on that ``replyTo`` topic.

.. code-block:: json

   {"headers": {"replyTo": "manual/reply"}, "payload": {"slot_id": 1}}

Two kinds of invalid requests are treated differently:

-  A message whose envelope cannot be parsed (invalid JSON, missing
   ``headers`` or ``replyTo``) gets no reply at all. A client only notices
   this as a timeout.
-  A message with a valid envelope but an invalid or incomplete ``payload``
   is answered with the command's failure result, for example ``Rejected``.

The notices on ``e2m`` topics differ in retention: the lifecycle ``status`` is
published retained, so a subscriber always receives the last known status,
while ``active_slot`` and ``config_updates`` are published without retention
and are only seen by clients subscribed at that moment.

The exchange can be reproduced without any client library using the mosquitto
command line tools:

.. code-block:: bash

   mosquitto_rr -q 2 -e manual/reply \
     -t everest_api/1/configuration/m2e/list_all_slots \
     -m '{"headers":{"replyTo":"manual/reply"},"payload":{}}'
   mosquitto_sub -v -t 'everest_api/1/#'     # everything the APIs publish
