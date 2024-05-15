.. configure_plug_and_charge_main:

.. _configure_plug_and_charge_main:

#####################
Configure Plug&Charge
#####################

This is a guide for how to configure EVerest to enable its Plug&Charge functionalities. 
For a tutorial on how to do Plug&Charge in the EVerest SIL, please refer to :ref:`How To: Plug&Charge with EVerest Software in the loop <how_to_pnc>`.

*************************
Plug&Charge Authorization
*************************

There are a lot of resources available on Plug&Charge and ISO15118 PKI involved in this process,
so this guide is not going to repeat how Plug&Charge actually works.

It rather explains what EVerest provides with respect to Plug&Charge and how EVerest needs to 
be configured in order to suit your Plug&Charge use case.

************************************
The Authorization process in EVerest
************************************

In essence, the Plug&Charge Authorization runs like any other authorization in EVerest,
like local RFID authorization or remote authorization.  Have a look at how the authorization
process in EVerest in designed within the `Documentation of the Auth module <https://everest.github.io/nightly/_included/modules_doc/EvseSecurity.html#everest-modules-handwritten-auth>`_.

************************
Involved EVerest modules
************************

The E2E Plug&Charge process involves communication from the EV to systems in the cloud. The
main protocols involved are ISO15118 and OCPP. In EVerest, several modules and interfaces 
are involved in the Plug&Charge process. Here is an overview of how everything comes together
in EVerest:

.. image:: img/plug_and_charge_modules.png
    :align: center

.. note::
    
    This visualization only presents the interfaces and connections between them that are
    relevant for Plug&Charge.

Let's have a look step by step:

Step 0
======

Before a Plug&Charge session can start, the following certificates and keys should be installed on 
the charger:

* V2G Root certificate
* SECC Leaf certificate
* SECC Leaf private key
* MO Root certificate (optional)

These certificates and keys can be installed during provisioning of the charger, or they can be 
installed using OCPP1.6 or OCPP2.0.1. The paths to store these files can be configured in the 
EvseSecurity module. Please see `Documentation of the EvseSecurity <https://github.com/EVerest/everest-core/blob/main/modules/EvseSecurity/doc.rst>`_
for further information on how to do the configuration for this module.

In the visualization step 0. shows the process represents the previously described process of 
provisioning the charger with the correct certificates. Step 0. before there is a physical 
connection to the EV. The OCPP/OCPP201 and EvseV2G module require a module that implements 
the evse_security.yaml (link) interface, in order to execute the following commands:

* install_ca_certificate (Used by OCPP to install root certificates. This process is initiated by the OCPP CSMS)
* update_leaf_certificate (Used to install or update SECC leaf certificates)
* generate_certificate_signing_request (Used to generate a CSR that is used in the SignCertificate.req of OCPP)
* verify_certificate (Used by EvseV2G to verify the contract certificate and by OCPP to verify new leaf certificates)
* get_mo_ocsp_request_data (Used by EvseV2G and OCPP to get the OCSP request data of the contract certificate (chain))

There are more commands provided by the evse_security.yaml (link) interface, which are not included in the Plug&Charge
process.

Step 1
======

This step is triggered by a physical connection between the EV and the charger. A TLS connection is required 
between the EV and the charger to allow Plug&Charge, so the EvseV2G module retrieves the SECC leaf certificate 
chain and private key from via the evse_security.yaml interface and sets up a TLS server, to which the EV
can connect as a TLS client.

Step 2
======

When charger and EV have agreed on Contract being the selected payment option, we have something going on
that we can call a Plug&Charge process. The EV sends its contract certificate chain and requests authorization
from the charger. The EvseV2G module generates a ProvidedIdToken (link), which is the EVerest type that 
contains data the authorization request, including the contract certificate and OCSP request data. 

The ProvidedIdToken is transmitted via the evse_manager.yaml interface to the EvseManager module.

Step 3
======

The EvseManager module implements the token_provider.yaml interface and can therefore publish the 
ProvidedIdToken (link) containing the contract certificate and OCSP data within EVerest to the central
authorization module in EVerest: Auth.

Step 4
======

The Auth module sends commands containing the ProvidedIdToken to its registered token_validator(s) (link),
which are OCPP/OCPP201 in the case of Plug&Charge. The OCPP module(s) validate the token based on the requirements
specified in the OCPP protocol (either validating locally or by the CSMS).

Step 5
======

In case the validation was successful, the Auth module notifies the EvseManager using the authorize command,
that authorization is present and the charging session can be started.

Step 6
======

The EvseManager forwards the authorization response to the EvseV2G module, which can then send the 
awaited ISO15118 response to the EV.

.. note::
    
    We have taken some shortcuts and ignored some further communication going on during the full process,
    but these steps cover what's important for Plug&Charge in EVerest.


*********************
EVerest configuration
*********************

Now that we know everything comes together for Plug&Charge in EVerest, we can have a look at how this is 
actually configured.

The following two configuration files are relevant and require a correct setup and activation for Plug&Charge:

* EVerest configuration file (yaml)
* OCPP configuration file (.json)

Let's start with the EVerest configuration file. If you haven't read "Explaining the YAML files", now its the 
right time to do it before you go on!

It's a good idea to start with a base of a configuration file and talk about the changes required to enable
Plug&Charge. The base config we use is the "config-sil-ocpp201.yaml", which already contains the configuration
for OCPP2.0.1.

We need to take a closer look at the configuration of the EvseManager, EvseV2G, Auth and EvseSecurity.

EvseManager
===========

* In case of AC, make sure that `ac_hlc_enabled` is set to `true` in order to allow ISO15118 communication
* Make sure `payment_enable_contract` is set to `true`

EvseV2G
===========

* Make sure `tls_security` is set to `allow` or `force`.
* If `verify_contract_cert_chain` is `true` the EvseV2G module attempts to verify the contract certificate chain
locally. It is recommended to set this to `false`, because this validation is also executed and handled in OCPP.

Auth
====

* Make sure the EvseManager module is listed as a connection of `token_provider`. This is important, because only
in this case the ProvidedIdToken including the contract certificate is actually received by the Auth module.
* Make sure the OCPP module is configured as the single `token_validator`.

EvseSecurity
============

Please refer to `Documentation of the EvseSecurity module <https://github.com/EVerest/everest-core/blob/main/modules/EvseSecurity/doc.rst>`_ 
for information on the ISO15118 configuration. 
