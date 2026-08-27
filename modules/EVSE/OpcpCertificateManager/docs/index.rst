.. _everest_modules_handwritten_OpcpCertificateManager:

..  This file is a placeholder for optional multiple files
    handwritten documentation for the OpcpCertificateManager module.
    
..  This handwritten documentation is optional. In case
    you do not want to write it, you can delete the doc/ directory.

..  The documentation can be written in reStructuredText,
    and will be converted to HTML and PDF by Sphinx.
    This index.rst file is the entry point for the module documentation.

..  Use underlined-only headlines inside this document (highest-level
    sub-section headline should use "=" characters)

..  The content of this file will be included in the auto-generated HTML
    page for the module. You can link to it using the following
    reference: everest_modules_OpcpCertificateManager.

.. *******************************************
.. OpcpCertificateManager
.. *******************************************

Keeps the ISO 15118 SECC leaf certificates (ISO 15118-2 and ISO 15118-20) and the V2G/MO root certificates of a charging station current through an OPCP (Open Plug&Charge Protocol) ecosystem such as Hubject, without OCPP. Renewals use RFC 7030 simplereenroll authenticated with the still valid SECC leaf as TLS client certificate, so no OAuth2 secret is stored on the station; the first certificate is enrolled with the opcp-enroll tool.
