**************************
How To: Plug&Charge in EVerest
**************************

EVerest provides support for Plug&Charge within ISO15118-2 and OCPP1.6J. This tutorial explains how you can
set up and configure EVerest to make Plug&Charge work.

This tutorial includes:

- How to set up the PKI for Plug&Charge
- How to simulate Plug&Charge with EVerest
- 

.. _prequesites:

Prerequesites
=============

If you're new to EVerest start with our `Quick Start Guide <02_quick_start_guide.html>`_ 
to get a simulation in EVerest running for the first time. It is important that you have set up the required docker containers for Mosquitto and SteVe,
which we will use as an example CSMS.
If you have done that successfully, you can move on with this tutorial.

Set up your Plug&Charge PKI
===========================

Custom Plug&Charge CSMS
=======================

NodeRED UI for PnC
==================

Topic:

everest_external/nodered/1/carsim/cmd/execute_charging_session

Simuation String for PnC:

sleep 1;iso_wait_slac_matched;iso_start_v2g_session contract,AC_three_phase_core;iso_wait_pwr_ready;iso_draw_power_regulated 16,3;sleep 20;iso_stop_charging;iso_wait_v2g_session_stopped;unplug
