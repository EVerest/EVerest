**************************
How To: Plug&Charge with EVerest Software in the loop
**************************

EVerest provides support for Plug&Charge within ISO15118-2 and OCPP1.6 and OCPP2.0.1. This tutorial explains how you can
set up and configure EVerest for Plug&Charge with the software in the loop.

.. _prequesites:

Prerequesites
=============

If you're new to EVerest start with our `Quick Start Guide <02_quick_start_guide.html>`_ to get a simulation in EVerest running for the first time.
If you have done that successfully, you can move on with this tutorial.

.. _plug_and_charge_process:

The Plug&Charge process
=============

The process we are going to simulate covers a complete AC Plug&Charge process including a CertificateInstallation request to install a virtual contract in the simulated EV.

FIXME: Refer to some good documentation about Plug&Charge

The components included in this setup are the following:
1. Charging Station (SECC): The EVerest stack provides the software running on the charger. It provides the ISO15118 and OCPP implementations.
2. Electric Vehicle (EVCC): The EV is siumlated using the software in the loop (SIL). The SIL runs as part of EVerest using separate modules that are started alongside with the EVerest application.
3. Charging Station Management System (CSMS): The CSMS used in this setup is an external service. It's a very simple implementation of an OCPP central system based on the Python ocpp package from TheMobilityHouse (https://github.com/mobilityhouse/ocpp).

Let's get started step by step
==============================

1. Prerequisites must be fullfiled: EVerest must be installed on your system. By default the installation of everest-core includes a complete and automatic installation of a test PKI. 
The certificates and keys are located under `dist/etc/certs`.

2. Lets prepare the central system that we are going to use. Follow the instructions described here to set it up: https://github.com/EVerest/ocpp-csms

3. Run everest-core with either 

```bash
./run_scripts/run-sil-ocpp201-pnc.sh 
```

or 

```bash
./run_scripts/run-sil-ocpp-pnc.sh 
```

Make sure NodeRED is running.

In NodeRED select: Car Simulation: AC ISO15118-2 and click Car Plugin.

Check the OCPP logs. By default located in /tmp/everest_ocpp_logs
