.. _index:

.. admonition:: Current Version |version|

    You are looking at the documentation for the |version| release of EVerest.
    See `snapshot file <./appendix/snapshot.html>`_ for further version mapping.
    See `versions index <../versions_index.html>`_ for other versions.

.. image:: img/everest_horizontal-color.svg
    :align: right

***************
What Is EVerest
***************
EVerest is an open source modular framework for setting up a full stack
environment for EV charging.

The modular software architecture fosters customizability and lets you
configure your dedicated charging scenarios based on interchangeable modules.
All this is glued together by MQTT.

EVerest will help to speed the adoption to e-mobility by utilizing all the open
source advantages for the EV charging world. It will also enable new features
for local energy management, PV-integration and many more.

The EVerest project was initiated by `PIONIX GmbH <https://pionix.com>`_
(`Pionix at LinkedIn <https://www.linkedin.com/company/pionix-gmbh>`_) to help
with the electrification of the mobility sector and is an official project of
the Linux Foundation Energy.

If you are into LinkedIn, make sure to follow the EVerest project there:
`EVerest project on LinkedIn <https://www.linkedin.com/showcase/everest-project/>`_

**************************************
Where to Go From Here: EVerest Compass
**************************************

Testing and setting up EVerest
==============================

You are currently on the main documentation page of EVerest.
This is the right place to learn about understanding how things work together
and how to test EVerest.

Just read on through all the sections and you will find a
:ref:`Quick Start Guide <quickstartguide_main>`, more detailed explanations
about the EVerest modules concept and tutorials.

Contributing to EVerest
=======================

EVerest is an open source software and has also a very open community around
it.

"Open community" means that you can join discussions, thought exchanges and
prioritization meetings without dedicated registration or allowance.
See below for all the communication channels that can be joined by you right
now.

To learn about **contributing to the source code** of EVerest, read our
`contribution file in the main EVerest repository <https://github.com/EVerest/EVerest/blob/main/CONTRIBUTING.md>`_
.

If you rather want to contribute to the documentation, you should have a look
at the :ref:`Documenting EVerest page <documenting_everest>`.

There are quite a few other resources that you might want to check out:

.. _index_contact:

Communication channels
======================

.. _index_zulip:

Zulip chat
----------

The most important place for thought exchange, questions and discussions is
the Zulip chat of the Linux Foundation Energy.

You can find chat channels there for different topics all around EVerest.

Feel free to drop your own questions and dive into the communication with other
EVerest enthusiasts.

Read the README channel in Zulip for more information about the different
channels.

You can find us here: https://lfenergy.zulipchat.com/

.. _index_mailinglist:

Mailing list
------------

.. |link_mailinglist| raw:: html

    <a href="https://lists.lfenergy.org/g/everest" target="_blank">
        <img src="_static/icons/mail.svg" style="height: 0.8em;"> <b>EVerest mailing list</b>
    </a>

.. |link_announcementlist| raw:: html

    <a href="https://lists.lfenergy.org/g/everest-announce" target="_blank">
        <img src="_static/icons/mail.svg" style="height: 0.8em;"> <b>EVerest announcement list</b>
    </a>

Another way to connect to the steadily growing EVerest community is the mailing
list.
As Zulip is the main place for all the information and news around EVerest,
the mailing list is an optional way if you prefer contacting the core team via
email.

We have set up two separate mailing lists:

1. The most important one is the announcement list. Important news or events
   all around EVerest are shared there. Subscribe here: |link_announcementlist|.
2. Optionally, you can subscribe to the detailed mailing list with all
   developer's exchange: |link_mailinglist|.


Weekly tech meetup and working groups
-------------------------------------

We also have online video meetings for a more personal way of exchange.

The General EVerest Welcome Call is a format since the beginning of 2025.
If this compass page at hand leaves some questions open and you want to clarify
that in person, you can join us in a weekly (non-technical) meeting.
We can answer question all around the EVerest community and show you the way
to go with your specific scenario.

The General EVerest Welcome Call is each Tuesday at 11am CE(S)T.

Find us here:
https://zoom-lfx.platform.linuxfoundation.org/meeting/94033706607?password=20dfbaaa-37d5-4b77-8c59-9935c9037c7a

Further live meetings we regularly have are the working group meetings.
In the working groups, developers meetup for exchanging the status quo of
dedicated projects and topics.

The working groups are to change over time depending on the current needs of
the community. Currently, we have the following working groups:

- Car Communication (car/charger communication with topics ISO 15118, EXI,
  SLAC, CHAdeMO etc)
- Cloud Communication (communication between charger and backends in cloud
  with topics OCPP 1.6, 2.0.1, 2.1 etc)
- Energy Management
- MCS (Megawatt Charging System)

A calendar with all EVerest events can be found here:
https://zoom-lfx.platform.linuxfoundation.org/meetings/everest

If you work on a topic that does not fit in the current list of working groups,
you can ask in Zulip or in the General Call for finding like-minded people and
suggest creating a new working group.

You might want to join the working group chat channels in Zulip.
See the "Zulip chat" section above to see how to get there!

Special Interest Group "EV Charging"
------------------------------------

Linux Foundation Energy (LFE) is growing. As the EVerest project (and all
other LFE projects) have several connecting points to other projects in the
same field, a Special Interest Group (SIG) has been created for

* exchanging thoughts and experiences,
* share best practices,
* discuss industry trends and
* tackle challenges together.

If you work in the field of EV charging and you are interested in open source
software / projects, join the SIG calls. They are free to access and open to
all interested parties.

Join the mailing list: https://lists.lfenergy.org/g/ev-charging-sig

Directly access via Zoom:
https://zoom-lfx.platform.linuxfoundation.org/meeting/92797425199


Tech resources about EVerest
============================

.. |link_github| raw:: html

  <a href="https://github.com/EVerest" target="_blank">
    <b>EVerest repositories on GitHub</b>
  </a>

.. |link_roadmap| raw:: html

    <a href="https://github.com/EVerest/EVerest/blob/main/tsc/ROADMAP.md" target="_blank">
      <b>roadmap on GitHub</b>
    </a>

.. |link_youtube| raw:: html

        <a href="https://www.youtube.com/@lfe_everest" target="_blank">
            <img src="_static/icons/youtube.svg" style="height: 0.8em"> <b>YouTube Channel</b>
        </a>

Find the source code, current Pull Requests and an issue tracking on our
home at |link_github|.

For getting at least a rough overview of the things we already implemented
and what is planned for the near future, see our |link_roadmap|.

With our |link_youtube| channel, you can stay up-to-date with webinars and get
insights from the Technical Steering Committee recordings.

And last but not least, also have a look at section
:ref:`FAQ And Best Practices <faq_main>` in this documentation
page to find topics that sometimes people get stuck with.

**************************************
Find Your Way Into EVerest Development
**************************************

If you want to choose video rather than text and code for a quick dive-in, **have a look at our webinar first**:

.. image:: /img/webinar-how-everest-ecosystem-simplifies-charging-use-cases-screenshot.png
  :alt: Screenshot of the webinar video How EVerest Ecosystem simplifies Charging Use Cases
  :target: https://www.youtube.com/watch?v=OJ6kjHRPkyY

Click for watching on YouTube: `Webinar: How the EVerest Ecosystem will simplify Charging Use Cases <https://www.youtube.com/watch?v=OJ6kjHRPkyY>`_

We prepared a path to get step by step into the EVerest world. It will lead you
from a high level overview right into understanding how to implement modules
for your dedicated hardware scenarios or developer use cases.

To walk this path, simply read on and follow the table of contents below.

*****************
Table Of Contents
*****************

But now let's dive in the EVerest journey to get you on board. Start on top
level with the first chapter and walk the path down to your first module
implementation.

.. toctree::
    :numbered:
    :maxdepth: 2
    :glob:

    general/01_framework/index
    general/02_detail_pre_setup
    general/03_quick_start_guide
    general/04_detail_module_concept
    general/05_existing_modules
    general/06_handling_bank_cards
    general/07_configure_plug_and_charge
    dev_tools/index
    tutorials/index
    general/faq
    hardware/*
    appendix/*
    appendix/*/index

Indices and tables
==================

* :ref:`genindex`
* :ref:`search`
