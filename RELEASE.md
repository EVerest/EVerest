# Release Process
## General Infos About Versioning

EVerest will create releases on a monthly base. This frequency is related to the monthly Technical Steering Committee meetings.

For the everest-core repository, the releases are tagged using the following name pattern: _YEAR.MONTH.INDEX_, e.g. 2022.12.1

For libraries, semantic versioning is used.

Find details about the releases on the corresponding release pages of the repos. E.g. for everest-core: [Release page of everest-core repository](https://github.com/EVerest/everest-core/releases)

First of all, there will be source code releases for EVerest. Binary releases for different hardware platforms will follow soon.

## Checkout a Dedicated Version
If you follow the
[Quick Start Guide of EVerest](https://everest.github.io/nightly/general/02_quick_start_guide.html), you will setup EVerest with the help of the
EVerest Dependency Manager (edm).

Starting from edm version 0.5, edm also helps you with getting a dedicated
release of EVerest. First of all, see a list of available releases:
```bash
edm init --list
```
That will result in an output like
```txt
[edm]: Available everest-core releases: 2023.5.0, 2023.3.0, 2023.2.1, 2023.2.0, 2023.1.0, 2022.12.1, 2022.12.0, 2022-11.0
```
with the most current releases shown first.

To checkout your favourite release, like in the following case `2023.5.0`:
```bash
edm init 2023.5.0
```

### Older versions of edm
Versions of edm prior to 0.5 will need a manual adjustment. As seen in the
[Quick Start Guide of EVerest](https://everest.github.io/nightly/general/02_quick_start_guide.html), edm receives a config file as parameter that
tells edm which repositories shall be checked-out for your EVerest workspace.

Open that yaml file and look for the git_tag values. Change its values to the
release name and run edm with that changed yaml as parameter.

__This is not the recommended way__, so whereever possible, update edm
to the current version and enjoy the more convenient way.

## Create a New Release

### Define the features
Make sure that you are clear with the EVerest core developer team that all
new release features have been finished, tested and documented in the
changelog.

If unsure about that, discussion about such topics takes place in the weekly
tech meeting (announced via the
[EVerest Mailinglist](https://lists.lfenergy.org/g/everest).

### Create a new release on GitHub

General information about release management with GitHub can be found directly
at GitHub on the page about
[Managing Releases in a repository](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository).

The EVerest-specific process is as follows:

Assuming you wish to create a new release for the everest-core repository,
which is the main EVerest repo. On the repo page, click on **Releases** in the
right side-column. The button **Draft a new release** leads you to the release
creation form.

You can create a new Git tag right on that page. That is the tag on which the
release will be based. Remember the correct versioning name pattern:
_YEAR.MONTH.INDEX_, e.g. 2022.12.1

> INFO: _Just tell them about some stuff about how edm knows where to
get the compatible versions of the other repos and so on._