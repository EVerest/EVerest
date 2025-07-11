# Release Process
## General Infos About Versioning
### Types of releases

EVerest releasing happens in the following way:

- source code releases once a month (around the TSC meeting every 4th Thursday
  in a month) and
- consolidated releases every three months (January, April, July, October)
  at the same point in time.

A feature freeze phase of two weeks will precede the consolidated releases.
In that timeframe, testing with focus on stability will be conducted.
No new features will be merged into the release candidate branch during that
time.

### Tagnames and features of a release

For the everest-core repository, the releases are tagged using the following name pattern: _YEAR.MONTH.INDEX_, e.g. 2022.12.1

For libraries, semantic versioning is used.

Find details about the releases on the corresponding release pages of the repos. E.g. for everest-core: [Release page of everest-core repository](https://github.com/EVerest/everest-core/releases)

First of all, there will be source code releases for EVerest.

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

### Define the features and repo versions
Make sure that you are clear with the EVerest core developer team that all
new release features have been finished, tested and documented in the
changelog.

Development in several EVerest repositories other than everest-core will still
go on. Make sure that stable versions of repositories have been tagged and
configured in the everest-core repository in file dependencies.yaml. This way,
those versions will be part of the new EVerest release.

If unsure about all that, ask the core developers
[via Zulip](https://lfenergy.zulipchat.com/).

### Create a new release on GitHub

General information about release management with GitHub can be found directly
at GitHub on the page about
[Managing releases in a repository](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository).

The EVerest-specific process is as follows:

To create a new official EVerest release, you will have to create a new release
of the main EVerest repository in GitHub - which is everest-core. On the repo
page, click on __Releases__ in the right side-column. The button __Draft a new release__ leads you to the release creation form.

You can create a new Git tag right on that page. That is the tag on which the
release will be based. Remember the correct versioning name pattern:
_YEAR.MONTH.INDEX_, e.g. 2025.12.1

When using edm to get a new release version, edm knows from the
dependencies.yaml which tags of all repositories to checkout for serving
a stable release workspace.
