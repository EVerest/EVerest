.. doc_framework:

EVerest framework
#################
You can think of EVerest as an operating system for EV chargers with implementations of communication protocols, software modules for representations of hardware devices (chargers, cars, …) and tools for simulating the charging process.

It's a full stack environment for EV charging.

*********************
A Visual Introduction
*********************

A first very high level overview of the framework can be seen here:

.. image:: img/quick-start-high-level-1.png

The EVerest framework helps with building your dedicated development scenario with all needed modules for your specific developer's use case. The modules are connected by using the principle of loose coupling.

Modules in EVerest can be everything like hardware drivers, protocols, authentication logic and more. Build up your development scenario as needed and enhance it by adding your own additional modules.

.. note::

  Modules can be implemented in C++, Javascript, Python or Rust.

  As especially Python and Rust were not part in the very early version of
  EVerest, we appreciate feedback about your experience in using those
  languages.

  Write us via
  :ref:`Zulip <index_zulip>` or drop us a line on the
  :ref:`Mailing list <index_mailinglist>`.

Another way to look at EVerest is its layer architecture:

.. image:: img/quick-start-high-level-2.png

Depending on your project use case, you can define the suitable module stack and configure module connections and module parameters.

What modules EVerest already delivers as ready-to-use implementations and which features they currently ship will be explained in detail in the modules documentation.

*****************
Tools And Helpers
*****************

Additionally, you have some tools and helpers that work with the framework which makes your EVerest developer's life easier:

.. image:: img/quick-start-high-level-3.png

To understand the benefit delivered by those tools, let's have a sneak preview:

- **Admin Panel**: Tool to show all modules connections and dependencies including the parameters set for the modules.
- **EVerest Dependency Manager** (edm): Tool that helps you getting all needed repositories from Git for your specific setup.
- **ev-cli**: Generates module and interface scaffolds based on templates. This way you can start implementing new modules very fast.
- **MQTT Explorer**: Great for debugging the messages sent between your modules during development phase.
- **NodeRed** for simulating your EVerest setup
- **SteVe**: Just in case you want to test your EVerest instance with some OCPP backend functionality: SteVe is an external tool that lets you do exactly that.

How to setup and use those tools will be shown later.

*************************************
System Requirements and Prerequisites
*************************************

What is needed to run EVerest?

Hardware
========
It is recommended to have at least 4GB of RAM available to build EVerest. More CPU cores will optionally boost the build process, while requiring more RAM accordingly.

We have setup EVerest successfully on Raspberry Pi 4.

Operating System
================
EVerest has been tested with Ubuntu, OpenSUSE and Fedora 36. In general, it can
be expected to run on most Linux-based systems.

Libraries And Tools
===================

To create your development environment with all needed tools, libraries and
compilers, the section
:ref:`Prepare Your Environment <preparedevenv_main>` will walk you through the
setup phase.
