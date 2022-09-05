.. _how_to_work_on_issues:

How To: Work on issues
#######################

Issue states
************

* Unconfirmed
    Newly created public issues are unconfirmed. The issue can be moved by team members to
    `Untriangled`_ by validating, reproducing or otherwise that this is a bug or a feature request.

* Untriaged
    Confirmed issues that haven't been reviewed for priority or assignment. This is the default 
    for new created issues by team members. The issue can be moved to `Available`_ after an
    importancy and urgency has been assigned.

* Available
    Confirmed and triaged but not assigned issues that are available to work on.
    The issue can be moved to `Assigned / Planned`_ after it has been assigned to a team member.

* Assigned / Planned
    Confirmed and triaged issues that are assigned to someone who planned to work on them.
    The issue can be moved to `In Progress`_ after this the person has started working on it.

* In Progress / Started
    Assigned issues that are currently being worked on.
    The issue can be moved to `In Approving / Fixed`_ after it has been finished and needs
    to be approved by a team member.

* In Approving / Fixed
    Issues that are being reviewed by the team.
    The issue can be moved to `Closed`_ after a team member has approved it and merged all linked pull requests.

* Closed
    Issues that are done or closed for other reasons.

Start working on an issue
*************************

#. Pick an issue

    In best case you can pick an issue from the list of `Assigned / Planned`_ issues.
    If you don't have any assigned issues, you can pick an issue from the list of `Available`_ issues.

#. Assign yourself to the issue if not already assigned

#. Move the issue to `In Progress`_

#. :ref:`Create a WIP pull request <how_to_pull_request>` for each branch related to the issue and link it to the issue

#. After you have finished working on the issue, move it to `In Approving / Fixed`_

    Make sure you are still assigned to the issue. Assign a team member as reviewer and wait for them 
    to review your code. If no code changes are requested and the issue is approved by a team member, the 
    pull request will be merged and the issue will be moved to `Closed`_.

#. After a team member has approved the issue, it should be moved to `Closed`_ automatically by merging all linked pull requests.
