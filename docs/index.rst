.. _index:

.. admonition:: Current Version |version|

    You are looking at the documentation for the |version| release of EVerest.
    See `snapshot file <./appendix/snapshot.html>`_ for further version mapping.
    See `verions index <../versions_index.html>`_ for other versions.

.. image:: img/everest_horizontal-color.svg
    :align: right

***************
What Is EVerest
***************
EVerest is an open source modular framework for setting up a full stack environment for EV charging.

The modular software architecture fosters customizability and lets you configure your dedicated charging scenarios based on interchangeable modules. All this is glued together by MQTT.

EVerest will help to speed the adoption to e-mobility by utilizing all the open source advantages for the EV charging world. It will also enable new features for local energy management, PV-integration and many more.

The EVerest project was initiated by PIONIX GmbH to help with the electrification of the mobility sector and is an official project of the Linux Foundation Energy.

***************
EVerest Compass
***************

You are currently on the main documentation page of EVerest.

There are quite a few other resources that you might want to check out:

Direct contact
==============

.. |link_mailinglist| raw:: html

    <a href="https://lists.lfenergy.org/g/everest" target="_blank">
        <img src="_static/icons/mail.svg" style="height: 0.8em;"> <b>EVerest mailing list</b>
    </a>

The easiest and fastest way to connect to the steadily growing EVerest
community is the |link_mailinglist|.

You will get your questions answered and will also get regular information
about our public meetups to which you are invited to join.

A **weekly tech meetup** for
all contributors is currently held once a week.

Meet us here each Tuesday at 3pm in CET time-zone:

Join via Google Meet:
https://meet.google.com/wnv-aadq-irt

Or dial:
(DE) +49 40 8081616135
PIN: 964783404#

More phone numbers:
https://tel.meet/wnv-aadq-irt?pin=4305012153510&hs=7

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

Social Media
============

.. |link_twitter| raw:: html

    <a href="https://twitter.com/everestincharge" target="_blank">
        <img src="_static/icons/twitter.svg" style="height: 0.8em"> Twitter
    </a>

.. |link_mastodon| raw:: html

    <a href="https://fosstodon.org/@EVerest" target="_blank">
        Mastodon
    </a>

Don't forget to follow us on |link_twitter| or |link_mastodon|.

**************************************
Find Your Way Into EVerest Development
**************************************

If you want to choose video rather than text and code for a quick dive-in, **have a look at our webinar first**:

.. image:: /img/webinar-how-everest-ecosystem-simplifies-charging-use-cases-screenshot.png
  :alt: Screenshot of the webinar video How EVerest Ecosystem simplifies Charging Use Cases
  :target: https://www.youtube.com/watch?v=OJ6kjHRPkyY

Click for watching on YouTube: `Webinar: How the EVerest Ecosystem will simplify Charging Use Cases <https://www.youtube.com/watch?v=OJ6kjHRPkyY>`_

We prepared a path to get step by step into the EVerest world. It will lead you from a high level overview right into understanding how to implement modules for your dedicated hardware scenarios or developer use cases.

To walk this path, simple go on reading and follow the table of contents below.

******************************************
Table Of Contents
******************************************

But now let's dive in the EVerest journey to get you on board. Start on top level with the first chapter and walk the path down to your first module implementation.

.. toctree::
    :numbered:
    :maxdepth: 2
    :glob:

    general/01_framework
    general/02_detail_pre_setup
    general/03_quick_start_guide
    general/04_detail_module_concept
    general/05_existing_modules
    dev_tools/index
    tutorials/index
    hardware/*
    appendix/*
    appendix/*/index

Indices and tables
==================

* :ref:`genindex`
* :ref:`search`
