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
at the `Documenting EVerest page <documenting_everest>`.

There are quite a few other resources that you might want to check out:

.. _index_contact:

Communication channels
======================

.. _index_zulip:

Zulip chat
----------

On the Zulip instance of Linux Foundation Energy, there are chat channels for
EVerest.

This is for thought exchange, for questions and talks about dedicated work on
EVerest topics. Feel free to to drop your own questions and dive into
communication with other EVerest enthusiasts.

We have created working groups (see below) to focus on
special topics in EVerest and get the development communication structured.
You will see those working groups as streams in Zulip.

So, the best place to be near development thought exchange is Zulip.

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

We have set up two separate mailing lists:

1. The most important one is the announcement list. Important news or events
   all around EVerest are shared there. Subscribe here: |link_announcementlist|.
2. Optionally, you can subscribe to the detailed mailing list with all
   developer's exchange: |link_mailinglist|.

If you do not want to ask your questions in the Zulip chat, you can use the
detailed mailinglist to get your questions answered.

Weekly tech meetup and working groups
-------------------------------------

Since the beginning of 2024 we organize our community and development
exchange in the format of working groups.
The formerly called **weekly tech meetup** is now one of those working
groups. Now, we simply call it "General Working Group".

Those are all EVerest working groups:

- General (for general topics and to get a common ground for updates from the
  other working groups)
- Car Communication (car/charger communication with topics ISO 15118, EXI,
  SLAC, CHAdeMO etc)
- Cloud Communication (communication between charger and backends in cloud
  with topics OCPP 1.6, 2.0.1, 2.1 etc)
- EVerest Framework and Tools (framework topics and tools like edm, ev-cli
  etc)
- Testing and CI

You might wanna join us in the general working group to meet the community
without having focused on a special area of EVerest (yet).

For the General Working Group, meet us here each Tuesday at 4pm in CE(S)T
time-zone:

https://zoom-lfx.platform.linuxfoundation.org/meeting/92086494169?password=b2df6774-bc19-46f9-8c71-bbd13a0266a1

To see links to the meetings of all other working groups, see the publicly
available LFE EVerest calendar:

https://zoom-lfx.platform.linuxfoundation.org/meetings/everest

Additionally, you might want to join the working group chat channels in Zulip.
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

1:1 meetup
----------

The first step into the EVerest community should be
:ref:`checking out our Zulip chat <index_zulip>`.

Should you have problems entering the community, we are happy to help you.
Just book a meeting via the link below. Of course, this is optionally. You
are allowed to join the Zulip sessions and also take part in the working group
meetings without registration.

.. hint::
    In this 1:1 meetup, we will not talk about technical topics. We will rather
    see where you should start in the community if you have problems entering
    the meetups or the discussions.

Just book a 30-minute meetup here:
`EVerest Community Onboarding via Google calendar <https://calendar.app.google/cWtKd6ysfWMKHJtY9>`_

You will be talking with a community manager of the EVerest project.

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
