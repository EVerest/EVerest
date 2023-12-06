.. _tutorial_act:

##########################################
How To: Act
##########################################

This is a tutorial about how to setup and use
the local github actions runner ``act``.

******************************************
Setup
******************************************

#. Install act

    Follow the instructions on the 
    `original act repository <https://github.com/nektos/act#installation>`_.

#. Configure ``~/.actrc``

    Add the following lines to the file ``~/.actrc``:

    .. code-block::
        :linenos:

        -P ubuntu-latest=ghcr.io/catthehacker/ubuntu:act-latest
        -P ubuntu-20.04=ghcr.io/catthehacker/ubuntu:act-20.04
        -P ubuntu-18.04=ghcr.io/catthehacker/ubuntu:act-18.04
        -P self-hosted=ghcr.io/catthehacker/ubuntu:act-20.04
        --actor github-username
        --secret-file /path/to/your/github.secrets
        --artifact-server-path /path/to/your/.artifact-server
        -W ../.github/workflows/
    
    Each line presents an argument passed when calling ``act``.
    The first four lines are needed to use the correct docker images.
    The ``--actor`` argument specifies ``github.actor`` in the workflow.
    The ``--secret-file`` argument specifies the path to the file containing the secrets.
    The ``--artifact-server-path`` argument specifies the path to the directory where the artifacts are stored.
    The ``-W`` argument specifies the path to the 
    directory where the workflows are stored. 
    It's default value is ``.github/workflows/``.

#. Set up the secrets

    Create a file ``github.secrets`` in the directory you specified
    in your ``~/.actrc`` file.
    The file should contain the secrets in the following format:

    .. code-block::

        SECRET_NAME_1="secret_value_1"
        SECRET_NAME_2="secret_value_2"
        MULTILINE_SECRET="line_1\nline2\nline3"
        ...

    Probably you want to configure the following secrets:

    .. code-block::

        GITHUB_TOKEN="your_github_token"
        SA_GITHUB_TOKEN="github_token_of_the_service_account"
        SA_GITHUB_SSH_KEY="ssh_key_of_the_service_account"
        GITHUB_ACTOR="username_of_the_service_account"

    If you don't have access to the service account, don't
    want to use it or just testing something out locally, 
    you can use your own github token and
    your own ssh key:

    .. code-block::

        GITHUB_TOKEN="your_github_token"
        SA_GITHUB_TOKEN="your_github_token"
        SA_GITHUB_SSH_KEY="your_ssh_key"
        GITHUB_ACTOR="your_username"

******************************************
Usage
******************************************

List available jobs:
==========================================

To see all jobs in your workflows for an specific event use:

.. code-block::

    act <event> -W <path/to/workflows> -l

When the ``<event>`` argument is omitted the default event is ``push``.
It is not possible to list all jobs for all events at the same time.

Run Jobs / Trigger events:
==========================================

To trigger an event use:

.. code-block::

    act <event> -W <path/to/workflows>

When the ``<event>`` argument is omitted the default event is ``push``.
The event is triggered for all your workflows in the directory specified
by the ``-W`` argument.

If you want to trigger an event for a specific workflow use:

.. code-block::

    act <event> -W <path/to/specific/workflow-file.yml>

If you want to trigger an event for a specific job use:

.. code-block::

    act <event> -W <path/to/workflows> -j <job-name>

Bind working directory:
==========================================

Since the actions are executed in a docker container, 
the working directory isn't direct accessible by default.

If you want to bind the working directory use the ``-b`` flag:

.. code-block::

    act <event> -W <path/to/workflows> -j <job-name> -b

With this you bind the working directory to the current directory.
If you want to use a clean directory try:

.. code-block:: 

    mkdir work0/
    cd work0/
    act <event> -W <../.github/workflows> -j <job-name> -b

This is why adding the line ``-W ../.github/workflows/``
to your ``~/.actrc`` file is a good idea.
