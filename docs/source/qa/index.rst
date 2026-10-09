#####
Q & A
#####

This page will grow with questions from the mailing list and topics that
come up regularly in our EVerest development life. It is always a good idea
to have a look here when running into problems before asking for help via
the :ref:`mailing list <exp_community_mailinglist>`.

Errors, warnings and Troubleshooting
====================================

Compiling with GNU compilers
----------------------------

Building EVerest, you might want to use a GNU compiler. Handing over the flag
`CMAKE_CXX_COMPILER` to `cmake` lets you do that.

However, when using `gcc`, you might get errors about some
`unreferenced symbols` or linking issues.

Solution is simple: Use `g++` instead::

  cmake -D CMAKE_CXX_COMPILER=g++

`g++` will link std C++ files automatically
(`besides others <https://stackoverflow.com/a/173007/1168315>`_) which `gcc` won't do.

EVerest OCPP 2.0.1 setup
------------------------
After successfully setting up EVerest and configuring the
:ref:`OCPP201 module <everest_modules_OCPP201>`, I get errors about
a failed websocket connection.

The :ref:`OCPP module <everest_modules_OCPP201>` of EVerest operates - for now - as an OCPP client.
You will need to choose a backend system capable of OCPP 2.0.1 (like SteVe
for OCPP 1.6).

You may want to have a look at `<https://github.com/mobilityhouse/ocpp>`_ and
implement message handlers to get the communication working. Or you can have
a look at `<https://github.com/thoughtworks/maeve-csms>`_. Note: This has not been
officially tested by us.
