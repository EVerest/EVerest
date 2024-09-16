
![Alt text](https://raw.githubusercontent.com/EVerest/EVerest/main/docs/img/everest_horizontal-color.svg)

[![OpenSSF Best Practices](https://bestpractices.coreinfrastructure.org/projects/6739/badge)](https://bestpractices.coreinfrastructure.org/projects/6739)

# What is it?

EVerest is a Linux Foundation backed open-source modular framework for setting
up a full stack environment for EV charging. The modular software architecture
fosters customizablility and lets you configure your dedicated charging
scenarios based on interchangeable modules. All communication is performed by
the lightweight and flexible MQTT message queueing service. EVerest will help
to speed the adoption to e-mobility by utilizing all the open-source advantages
for the EV charging world. It will also enable new features for local energy
management, PV-integration and many more!

# Table of Contents

- [Main Features](#main-features)
- [Build and Install](#build-and-install)
- [Dependencies](#dependencies)
- [Demonstrations](#demonstrations)
- [License](#license)
- [Documentation](#documentation)
- [Background](#background)
- [Governance](#governance)
- [Discussion and Development](#discussion-and-development)
- [Contributing to EVerest](#contributing-to-everest)

# Main Features

- IEC 6185
- DIN SPEC 70121
- ISO 15118: -2 and -20
- SAE J1772
- SAE J2847/2
- CHAdeMO (planned)
- GB/T (planned)
- MCS (planned)
- OCPP: 1.6, 2.0.1 and 2.1 (planned)
- Modbus
- Sunspec

For a more detailed view of the current, and planned features, please review the
EVerest [roadmap.](https://github.com/EVerest/everest/blob/main/tsc/ROADMAP.md)

# Build and Install

The source code and installation instructions are currently hosted within [everest-core.](https://github.com/EVerest/everest-core#readme)

# Dependencies

everest-core relies on EVerest Dependency Manager (EDM) to help orchestrate the
dependencies between the different repositories. Detailed EDM installation
instructions are found [here.](https://everest.github.io/nightly/dev_tools/edm.html#dependency-manager-for-everest)

## Full Stack Hardware Requirements

It is recommended to have at least 4GB of RAM available to build EVerest. More
CPU cores will optionally boost the build process, while requiring more RAM accordingly.

# Demonstrations

The current demos showcase the foundational layers of a charging solution that
could address interoperability and reliability issues in the industry. Check-out
the available demonstrations in the [US-JOET Repo](https://github.com/US-JOET/everest-demo).

# License

EVerest and its subprojects are licensed under the Apache License, Version 2.0.
See [LICENSE](https://github.com/EVerest/EVerest#:~:text=Version%202.0.%20See-,LICENSE,-for%20the%20full)
for the full license text.

# Documentation

The official EVerest documentation is hosted [here.](https://everest.github.io/nightly/)

# Background

The EVerest project was initiated by PIONIX GmbH to help with the
electrification of the mobility sector.

# Governance

EVerest is a project hosted by the [LF Energy Foundation.](https://lfenergy.org/)
This project's technical charter is located in [CHARTER.md](https://github.com/EVerest/EVerest/blob/main/tsc/CHARTER.md)
and has established its own processes for managing day-to-day processes in the
project at [GOVERNANCE.md.](https://github.com/EVerest/EVerest/blob/main/GOVERNANCE.md)

# Discussion and Development

Regular discussions take place on [Zulip Chat.](https://lfenergy.zulipchat.com/)
Another way to connect to the steadily growing EVerest community is the mailing
lists:

- [EVerest Announcements List](https://lists.lfenergy.org/g/everest-announce)
- [EVerest Developers Exchange List](https://lists.lfenergy.org/g/everest)

Everest working group meetings occur on a weekly basis. A full calendar with
invitations to all meetings can be found [here.](https://zoom-lfx.platform.linuxfoundation.org/meetings/everest?view=month)

Additionally, if you and/or your organization would like to set up a 30 minute
1:1 meeting please follow the link [here.](https://calendly.com/manuel-ziegler-pionix/30min?month=2024-08)

Check out our [YouTube Page](https://www.youtube.com/@lfe_everest) for
instructional videos and meeting archives.

To report a problem, you can open an [issue](https://github.com/EVerest/EVerest/issues)
in repository against a specific workflow. If the issue is sensitive in nature
or a security related issue, please do not report in the issue tracker but
instead email <everest-tsc@lists.lfenergy.org>.

# Contributing to EVerest

Anyone can contribute to EVerest! Learn more about getting involved
[here.](https://github.com/EVerest/EVerest/blob/main/CONTRIBUTING.md)
Each contribution must meet the [Java Script](https://github.com/EVerest/EVerest/blob/main/CONTRIBUTING.md#:~:text=must%20meet%20the-,Java%20Script,-or%20C%2B%2B)
or [C++](https://github.com/EVerest/EVerest/blob/main/.clang-format) coding
style (part of every repository). If you just need
help or have a question, refer to [COMMUNITY.md](https://github.com/EVerest/EVerest/blob/main/CONTRIBUTING.md#:~:text=question%2C%20refer%20to-,COMMUNITY.md,-.)
