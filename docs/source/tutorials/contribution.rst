.. _tutorial_contribute_to_everest:

Contributing to EVerest
***********************

Assuming you want to contribute to the source code of EVerest, this tutorial
will help finding your way through the standard process.

The following steps are involved, which we will walk through in this tutorial
to get the details:

* Have a new idea for EVerest? Creating an according issue.
* Promoting a first high-level explanation of your idea and describing a
  possible way to implement a solution.
* Discussing with responsible core developers and the community.
* Preparing your development environment and start developing.
* Giving intermediate information in the EVerest live calls.
* Presenting the final work and update all relevant EVerest documents.

Let's get started!


Step 0: Before you start
========================

This tutorial assumes that you already got some experience with EVerest.
For example, you did some simulation tests based on our beginner guides
or you even used EVerest for a real-world hardware project already.

An additional great practice is to already have created an account for the
main EVerest community platform, Zulip.

Also consider to attend some of the live talks like the working groups.

See our :ref:`community channels page <exp_communicity_channels>` to get
all channels where to meet the core developers and the community.

Step 1: A new idea means a new issue
====================================

For this tutorial, let's assume that you have worked with EVerest's OCPP
features but you noticed that the local cost calculation for OCPP 2.1 is
still missing.

What a great idea to implement.

One important rule though:
For every idea / feature, we will have to create a GitHub issue first as the
core developers and the EVerest community could have some additional thoughts
or suggestions for the implementation of your idea.
Let them get the chance to express their views on a dedicated GitHub issue
page.


Step 1.1: Create a new issue
----------------------------

Look for the repository that fits your feature idea.
In our case, the `libocpp <https://github.com/EVerest/libocpp>`_ repository
would be a great idea.

On the GitHub page, click on the "Issues" tab.

Before creating a new one, please look for an existing issue that would fit
your topic.
Also use the search function of GitHub.
Eventually somebody else already had the same idea and there already was some
discussion about it.

.. note:: Also check Zulip
    As most of the discussion about the technical specifics of EVerest is
    happening on the Zulip platform, it is a great idea to also check there
    for information and thought exchange all around your implementation idea.

If there is no existing issue, create a new one.
Choose "New issue" on the issues page, which will show a dialog with some
options.
In our case, "Feature request" will be the right option to choose.

Give your new issue a nice title and fill out the form.
The more information you can give about your feature idea and the possible
solution, the better for core developers to understand and give input to your
contribution.

For this tutorial you could create an issue like this:

.. image:: images/tutorial-contributing-create-issue-1.png
    :width: 240px
    :alt: New issue for implementing Local Cost Calculation in OCPP 2.1

.. image:: images/tutorial-contributing-create-issue-2.png
    :width: 240px
    :alt: Description of the issue and a non-detailed suggestion for solving the issue


Step 1.2: Let the discussion happen
-----------------------------------

With an existing GitHub issue, we have a place where discussion about the
implementation can take place.

After you have created your issue, some core developer might give their opinion
or other contributors could add valuable information.

Additionally, you can bring your topic into an EVerest working group that fits
your topic.

.. note:: Best Practice Tip
    Before starting to implement your idea, there should at least have been
    some thought exchange inside the community about your idea.
    If there has not been a reaction in the GitHub issue and also not via
    Zulip, you might at least tell in one of the live calls that you will start
    to implement now.


Step 2: Let there be code!
==========================

With the community knowing about your upcoming development activity, it is now
time to shine and spread some code.

Be sure to create your working environment first - see next step.


Step 2.1: Prepare development environment
-----------------------------------------

For development and testing, a Linux environment is required.
For more requirements and the minimum setup for EVerest development, have a
look at the
:ref:`system requirements section on the getting started page <exp_getting_started_sw>`.

Also setup the software development packages described on that page.


Step 2.2: Get the sources
-------------------------

For the new OCPP implementation, we will require the libocpp repository to add
our still to be written code there.
Create a fork of the libocpp repository and download the source code to your
development environment.

Should you already have received dedicated access rights from the core team to
push directly to the original upstream repository, you can of course directly
git-clone the directory.

.. note:: About testing EVerest
    For the mere implementation of our new function, it will be sufficient to
    only get the libocpp repository.
    To test the new OCPP functionality within an EVerest instance, you will
    have to setup EVerest.
    That requires more respositories and configuration, which will not be part
    of this tutorial.

Familiarize yourself with the sources and get an idea where your new logic
might fit.


Step 2.3: Implement and test
----------------------------

TODO: How is the testing implemented in EVerest? Any best practices to add in
this tutorial?


Step 2.4: Optional: Create a Draft PR
-------------------------------------

Should you see a reason for showing an intermediate version of your
implementation - e.g. for discussing some things before continuing with
coding -, you can create a Draft Pull Request (PR).
This lets others give you feedback on your non-finished implementation.

Sometimes, this is a good way to show your current path of solution and
get some answers on open questions or check whether other see you on the fight
path.

.. note:: Draft PR to real PR
    A Draft PR can be switched to a real PR as soon as you finished your
    implementation. For further information, see the GitHub docs.

Step 2.5: Documentation
-----------------------

Yes, please do document your implementation. Have a dedicated page in the
EVerest Docs or make changes on existing pages - depending on your
implementation.

See our :ref:`documentation how-to <documenting_everest>` for more
information on that.

If unsure where to do the changes, you can always ask the community.


Step 3: Ready to publish your implementation?
=============================================

As soon as you have finished implementing and tested your creation, it it time
to create a Pull Request (PR) on GitHub.

See GitHub for
`a how-to about creating a PR from a fork <https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/creating-a-pull-request-from-a-fork>`_.

Please add a summarization about your implementation.
The community could have some feedback before adding your implementation
to the main branch.

Should you have to do some additional changes - e.g. based on community
feedback -, you can still commit and push your changes to your fork.
The changes will automatically be reflected in the PR.


Step 4: Your mission is done.
=============================

Congratulations!
Yet another optimization of EVerest has been merged.

The code owners of the changed repository (in your case the libocpp code
owners) will merge the PR as soon as they have reviewed your implementation
and think that it is good to shine.
