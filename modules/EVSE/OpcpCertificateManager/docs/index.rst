.. _everest_modules_handwritten_OpcpCertificateManager:

*******************************************
OpcpCertificateManager
*******************************************

Keeps the ISO 15118 SECC leaf certificates (ISO 15118-2, ``V2G``, prime256v1 / TLS 1.2 and ISO 15118-20,
``V2G20``, secp521r1 / TLS 1.3) and the V2G/MO root certificates of a charging station current through an
OPCP (Open Plug&Charge Protocol) ecosystem such as Hubject - **without OCPP**. It complements
``EvseSecurity`` (the store) and ``Evse15118D20`` (which presents the leafs to the EV and picks up renewed
ones through ``certificate_store_update``).

Two pieces work together:

* ``opcp-enroll`` - a command line tool for commissioning. It authenticates with the CPO's OAuth2 client
  credentials, registers the station, downloads the root certificates and enrolls the first leaf
  certificate(s) directly into the certificate store on disk (no running EVerest required).
* ``OpcpCertificateManager`` - this module. It periodically checks the remaining validity of the managed
  leafs and renews them **using the still valid leaf as TLS client certificate** (RFC 7030
  ``simplereenroll``). No OAuth2 secret is stored on the station.

Onboarding prerequisites
========================

The ecosystem operator (e.g. Hubject) must have

* issued OAuth2 client credentials (``client_id`` / ``client_secret``) for the CPO account with the ``CPO``
  role, and
* provisioned the CPO's **EVSE Operator ID(s)** (the three characters after the country code of the EVSEID /
  SECCID used as certificate common name). Names of other operators are refused with HTTP 403.

Hubject additionally requires each station to be registered as an end entity
(``PUT /v1/vra/cpo/endEntities``) before a certificate can be enrolled for its common name; ``opcp-enroll``
does this. Note that the published OPCP OpenAPI files do not describe this call and use different EST paths
than the live Hubject stages; the ``hubject-*`` environment presets carry the paths that work in practice.

Environments
============

The URI scheme is ``https://{region}.{stage}.hubject.com`` with ``stage`` in ``plugncharge-test``,
``plugncharge-qa`` and ``plugncharge`` (production) and ``region`` in ``eu`` / ``us``. Select one with
``environment`` (``hubject-eu-qa`` by default); a station enrolled on QA must keep using QA. For other
ecosystems use ``environment: custom`` with ``api_base_url`` (and optionally ``est_base_url`` and the path
templates ``simplereenroll_path`` / ``cacerts_path`` with the placeholders ``{ca}``, ``{iso}``, ``{iso_opt}``,
``{alg}``).

Commissioning with opcp-enroll
==============================

.. code-block:: bash

   opcp-enroll --environment hubject-eu-qa \
       --iso both \
       --common-name-iso2 'DE*PNX*E12345' \
       --common-name-iso20 'DE-PNX-S-00000000000000000000000000000001-1' \
       --organization 'Example Charging GmbH' --country DE \
       --manufacturer Pionix --device-name 'Charger X' --device-sw-version 1.0 \
       --evse-serial SN123 --ocpp-version 2.0.1 --charge-box-serial CB123 \
       --certs-dir /etc/everest/certs

The tool prompts for ``client_id`` and ``client_secret`` (the secret is not echoed; alternatively pass
``--client-secret-env VAR``), prints the roles found in the issued token, registers the end entity for every
ISO version, installs the V2G and MO roots (``--roots``), and enrolls each leaf: CSR through
libevse-security -> ``simpleenroll`` -> ``cacerts`` -> chain (leaf, sub-CA 2, sub-CA 1) -> install. The
enroll response contains only the leaf, so the CPO sub-CAs are always fetched separately and the chain is
validated against the installed V2G root before it is written. Finally it prints the matching module
configuration (``--print-config`` prints it without enrolling; ``--dry-run`` shows the CSRs only).
Production environments ask for confirmation unless ``--yes`` is given. ``--help`` lists all options.

Module configuration
====================

.. code-block:: yaml

   opcp_certificate_manager:
     module: OpcpCertificateManager
     config_module:
       environment: hubject-eu-qa
       manage_iso15118_2: true
       manage_iso15118_20: true
       common_name_iso2: "DE*PNX*E12345"
       common_name_iso20: "DE-PNX-S-00000000000000000000000000000001-1"
       organization: "Example Charging GmbH"
       country: DE
       renewal_threshold_days: 30
       check_interval_s: 43200
       root_types: v2g,mo
     connections:
       security:
         - module_id: evse_security
           implementation_id: main

Renewal behaviour
=================

* Every ``check_interval_s`` (first after ``initial_delay_s``) the module asks ``evse_security`` for the
  remaining validity of each managed leaf. A leaf with fewer than ``renewal_threshold_days`` left (or none
  installed) is renewed, ISO 15118-2 before ISO 15118-20 - the same policy libocpp applies.
* The renewal authenticates with the leaf of the same type when it is still valid, otherwise with the other
  installed SECC leaf. Both are CPO certificates of the same PKI. Without any valid leaf the status reports
  ``NoClientCertificate`` and nothing is sent; re-run ``opcp-enroll``.
* The CPO sub-CAs are fetched via ``cacerts`` on every renewal, and the stored chain is leaf + sub-CA 2 +
  sub-CA 1; ``update_leaf_certificate`` writes the single leaf and the chain file ``Evse15118D20`` serves.
* Failures retry with exponential backoff (``retry_backoff_s`` doubled per failure, capped at 24 h).
  ``AuthRejected`` (HTTP 401/403) is reported separately - typically the PKI does not offer certificate based
  re-enrollment, in which case the leaf must be renewed with ``opcp-enroll``.
* Root certificates are downloaded from the root certificate pool every ``root_sync_interval_s`` with the same
  client certificate and installed into the V2G and MO bundles (OEM roots have no bundle in the store yet).
  If the pool rejects certificate authentication the module logs it once per interval and continues.
* When OCPP also renews SECC leafs (``V2GCertificateInstallationEnabled``), enable only one of the two
  mechanisms to avoid competing renewals.

Interface
=========

``renew_leaf_certificates(force)`` schedules a check (or forced renewal) now, ``sync_root_certificates()`` a
root synchronisation; both return ``false`` while an operation is running. The ``status`` variable carries the
days until expiry, last result / error and next retry per leaf plus the root synchronisation state.

Testing
=======

``lib/everest/opcp/tests`` holds unit tests with an openssl generated PKI; ``tests/opcp_tests`` runs the tool
and the module against ``tests/opcp_tests/mock_opcp_server.py``, a mock of the OPCP/Hubject services
including TLS client certificate authentication.
