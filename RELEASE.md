# Release Process
## General Infos About Versioning

EVerest will create releases on a monthly base. This frequency is related to the monthly Technical Steering Committee meetings.

For the everest-core repository, the releases are tagged using the following name pattern: _YEAR.MONTH.INDEX_, e.g. 2022.12.1

For libraries, semantic versioning is used.

Find details about the releases on the corresponding release pages of the repos. E.g. for everest-core: [Release page of everest-core repository](https://github.com/EVerest/everest-core/releases)

First of all, there will be source code releases for EVerest. Binary releases for different hardware platforms will follow soon.

## How to Get a Dedicated Version
If you follow the
`Quick Start Guide of EVerest<https://everest.github.io/nightly/general/02_quick_start_guide.html>`_,
you will setup EVerest with the help of the EVerest Dependency Manager (edm).
edm takes a config file as parameter, which can be the default one that you
get with the everest-dev-environment repository called everest-complete.yaml.

To get a dedicated release of EVerest, you can edit that yaml file and change
the git_tag values to the release name and run edm with the changed yaml as
configuration file.

## Create a New Release

