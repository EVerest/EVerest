.. _exp-powermeter-ocmf:

########################
Powermeter OCMF Handling 
########################

This document explains how EVerest modules implementing the :doc:`powermeter interface </reference/interfaces/powermeter>`
shall handle OCMF report generation and transmission when used in conjunction with the
:ref:`EvseManager module <everest_modules_EvseManager>`.

The following sequence diagrams illustrate the interactions between the involved modules
during the start and stop of a transaction, including error handling scenarios:

- :ref:`Start of a transaction <exp-powermeter-ocmf-start-transaction>`
- :ref:`Stopping transaction in error <exp-powermeter-ocmf-stopping-transaction-error>`
- :ref:`Start of Powermeter or recovery after communication loss <exp-powermeter-ocmf-start-recovery>`

.. _exp-powermeter-ocmf-start-transaction:

Start of a transaction
======================

.. mermaid:: images/ocmf_start_of_transaction.mmd

.. _exp-powermeter-ocmf-stopping-transaction-error:

Stopping Transaction in Error
=============================

.. mermaid:: images/ocmf_stopping_transaction_in_error.mmd

.. _exp-powermeter-ocmf-start-recovery:

Start of Powermeter or recovery after communication loss
========================================================

.. mermaid:: images/ocmf_start_of_pmeter_or_transaction_after_powerloss.mmd

----

**Authors**: Florin Mihut, Piet Gömpel
