.. _htg_integrate_tariff_and_cost:

#########################
Integrate Tariff and Cost
#########################

This guide explains how to subscribe to tariff and session cost updates
published by EVerest, and how the data flow differs between OCPP 1.6 and
OCPP 2.x.

*************
Prerequisites
*************

- An EVerest deployment with an OCPP module (``OCPP`` for 1.6 or ``OCPP201``
  for 2.x) and the ``Auth`` module and the relevant API modules (see next section).
- An API client that connects via MQTT and subscribes to the relevant
  consumer API topics.

**********************
Relevant Consumer APIs
**********************

Two consumer APIs carry tariff and cost information:

- **Session cost consumer API** — provides the ``tariff_message`` and
  ``session_cost`` variables on the ``session_cost`` interface.
- **Auth consumer API** — provides ``token_validation_status``, which may
  include tariff messages received at authorization time.

At least the session cost consumer API must be subscribed to receive tariff and cost
updates.

See the AsyncAPI reference for the `auth_consumer_API <../reference/api/auth_consumer_API/index.html>`_
and the `session_cost_consumer_API <../reference/api/session_cost_consumer_API/index.html>`_ for message schemas.


.. note::

    The OCPP implementations follow the requirements of the OCA California
    Pricing Whitepaper. Note that this whitepaper contains requirements for
    displaying pricing information to the user, so in order to comply with it,
    the tariff and session cost messages must be used to show the required
    information on a charger display. The APIs only provide the necessary data.

*************************
No local cost calculation
*************************

EVerest does not calculate session costs locally. All cost figures
published on the session cost consumer API originate exclusively from the
CSMS:

- In OCPP 2.x, cost values come from the ``totalCost`` field in
  ``TransactionEventResponse`` or from a ``CostUpdated`` message.
- In OCPP 1.6, cost values come from ``RunningCost`` and ``FinalCost``
  DataTransfer messages sent by the CSMS.

EVerest only formats these values (e.g. applying the configured number of
decimal places) before forwarding them. No energy-based or time-based
cost computation is performed on the charging station.

When the charging station is offline, no cost figures are available at
all. The session cost consumer API will only receive human-readable tariff
text messages (from the locally configured fallback variables), never
computed cost amounts. The actual session cost is only published once the
CSMS responds after the connection is restored.

In addition to CSMS-provided cost data, the locally configured fallback
messages (``TariffFallbackMessage``, ``OfflineTariffFallbackMessage``,
``TotalCostFallbackMessage`` in OCPP 2.x; ``DefaultPriceText`` in OCPP 1.6)
are also published via the session cost consumer API as ``tariff_message``
publications. These contain human-readable pricing text only — they carry
no cost figures and are not derived from any local computation.

******************
OCPP configuration
******************

If you plan to use session cost information from OCPP (e.g. according to the California
Pricing requirements), you need to configure the OCPP modules to receive tariff and
cost information from the CSMS.

OCPP 1.6
=========

Enable the ``CostAndPrice`` profile in the OCPP 1.6 module configuration.
Once enabled, the OCPP module registers the tariff message and session cost
callbacks. The relevant configuration keys are:

- **DefaultPriceText** — the tariff fallback message shown when no
  ``SetUserPrice`` DataTransfer is received within the timeout. This is the
  OCPP 1.6 equivalent of ``TariffFallbackMessage`` in OCPP 2.x.
- **WaitForSetUserPriceTimeout** — how long to wait for a ``SetUserPrice``
  DataTransfer before publishing the ``DefaultPriceText`` fallback.

There is no OCPP 1.6 equivalent of ``TotalCostFallbackMessage``: no
fallback cost message is published when a transaction ends while offline.

OCPP 2.x
========

Enable the ``TariffCostCtrlr`` in the OCPP 2.x device model. The
component variables ``TariffEnabled`` and ``CostEnabled`` control which
features are active:

- **TariffEnabled** — publishes tariff / pricing messages via the
  ``tariff_message`` variable.
- **CostEnabled** — publishes running and final session cost via the
  ``session_cost`` variable.

Both can be enabled independently. Also configure other variables of the
``TariffCostCtrlr`` as needed.

Fallback messages
-----------------

When the CSMS provides no tariff information, the charging station
displays locally configured fallback messages. The following
``TariffCostCtrlr`` variables control this:

- **TariffFallbackMessage** — published when tariff is enabled but the
  CSMS provides no ``personalMessage`` in the ``AuthorizeResponse``.
- **OfflineTariffFallbackMessage** — used instead of
  ``TariffFallbackMessage`` when the charging station is offline. If not
  configured, ``TariffFallbackMessage`` is used as a fallback.
- **TotalCostFallbackMessage** — published when a transaction ends while
  the charging station is offline and no CSMS ``totalCost`` response can
  be received.

For multi-language deployments, add a language-specific instance of
``TariffFallbackMessage`` for each supported language (e.g. instance
``de`` for German, ``nl`` for Dutch). The supported languages are derived
from ``DisplayMessageCtrlr.Language.valuesList``. The default-language
text goes into ``personalMessage``; up to four additional language
entries go into ``customData.personalMessageExtra`` per California
Pricing spec 4.3.4. All languages are forwarded together as entries
in the ``messages`` field of the ``tariff_message`` publication.

.. note::

    Additional requirements of the OCPP 2.1 specification with respect to
    tariff and costs have not yet been implemented. The implemented features
    can still be used with OCPP 2.1.

*******************
Tariff message flow
*******************

Tariff messages contain human-readable pricing text in one or more
languages. They are published on the ``tariff_message`` variable of the
session cost consumer API.

OCPP 1.6
=========

Tariff messages originate from a ``SetUserPrice`` DataTransfer message sent
by the CSMS. The CSMS typically sends this message shortly after the
``Authorize.conf``, so the tariff message is available close to
authorization time.

If the ``SetUserPrice`` DataTransfer is not received within the
``WaitForSetUserPriceTimeout``, the ``DefaultPriceText`` fallback (if
configured) is published. The ``identifier_type`` is ``IdToken`` at
authorization time, and ``TransactionId`` once a transaction has started.

OCPP 2.x
========

Tariff messages are published at two points:

1. **Authorization time** — from the ``personalMessage`` field in the
   ``AuthorizeResponse``. The tariff message is also included in the
   ``messages`` field of the ``token_validation_status`` on the auth
   consumer API. At this point the ``identifier_type`` is ``IdToken`` and
   the ``identifier_id`` is the token value, since no transaction exists
   yet.

   If the CSMS does not provide a ``personalMessage``, the charging
   station automatically injects the configured ``TariffFallbackMessage``
   (or ``OfflineTariffFallbackMessage`` when offline) so that a tariff
   message is always published when tariff is enabled and a fallback is
   configured.

2. **During the session** — from the ``updatedPersonalMessage`` field in
   ``TransactionEventResponse`` messages. At this point the
   ``identifier_type`` is ``TransactionId`` and the ``identifier_id`` is
   the OCPP transaction ID.

.. note::

   To correlate auth-time tariff messages (keyed by ``IdToken``) with
   in-session updates (keyed by ``TransactionId``), use the
   ``token_validation_status`` message on the auth consumer API to capture
   the token, then match it against the session information from the EVSE
   manager consumer API.

*****************
Session cost flow
*****************

Session cost messages contain the accumulated cost of a charging session,
broken down into cost chunks (energy, time, flat fees), with current and
next-period pricing. They are published on the ``session_cost`` variable of
the session cost consumer API.

OCPP 1.6
=========

Session cost updates are triggered by ``RunningCost`` and ``FinalCost``
DataTransfer messages from the CSMS.

When a transaction ends while the charging station is offline, the queued
``StopTransaction`` is sent to the CSMS once the connection is
re-established. The ``StopTransactionResponse`` carries no cost data. No
fallback cost message is published. If the CSMS sends a ``FinalCost``
DataTransfer after reconnect, a ``session_cost`` will be published at
that point — but this is entirely at the CSMS's discretion and may not
happen at all.

OCPP 2.x
========

Session cost updates are triggered by:

- ``totalCost`` in a ``TransactionEventResponse`` — publishes a running
  cost update with status ``Running`` or ``Idle``.
- ``CostUpdated`` message — publishes a final cost update with status
  ``Finished``.

When both ``totalCost`` and ``updatedPersonalMessage`` are present in the
same ``TransactionEventResponse``, the personal message appears in both the
``tariff_message`` variable and the ``message`` field of the
``session_cost`` record.

When a transaction ends while the charging station is offline, the CSMS
``totalCost`` response will never arrive. In this case, if
``TotalCostFallbackMessage`` is configured, a ``tariff_message`` is
published with ``identifier_type = TransactionId``. This message contains
human-readable text only — no actual cost figures are available.
Consumers should display this text as a notification that the session
cost is unavailable, rather than as a cost breakdown.

Once the OCPP connection is re-established, the queued
``TransactionEvent(Ended)`` is sent to the CSMS. If the CSMS responds
with a ``totalCost``, a ``session_cost`` with status ``Finished`` is
published for the same transaction ID.

*****************************
Relationship between the APIs
*****************************

OCPP 2.x
=========

.. list-table::
   :header-rows: 1
   :widths: 30 35 35

   * - Event
     - Auth consumer API
     - Session cost consumer API
   * - Token authorized, CSMS provides tariff
     - ``token_validation_status`` includes ``messages``
     - ``tariff_message`` published (``identifier_type = IdToken``)
   * - Token authorized, no CSMS tariff, fallback configured
     - ``token_validation_status`` includes ``messages``
     - ``tariff_message`` published from ``TariffFallbackMessage`` (``identifier_type = IdToken``)
   * - In-session tariff update
     - —
     - ``tariff_message`` published (``identifier_type = TransactionId``)
   * - Running / final cost (online)
     - —
     - ``session_cost`` published (may include ``message``)
   * - Transaction ended offline, fallback configured
     - —
     - ``tariff_message`` published from ``TotalCostFallbackMessage`` (``identifier_type = TransactionId``); no ``session_cost``
   * - Reconnected, CSMS sends ``totalCost`` for offline transaction
     - —
     - ``session_cost`` with status ``Finished`` published; supersedes the earlier fallback text

OCPP 1.6
=========

.. list-table::
   :header-rows: 1
   :widths: 30 35 35

   * - Event
     - Auth consumer API
     - Session cost consumer API
   * - Token authorized, CSMS sends ``SetUserPrice``
     - ``token_validation_status`` includes ``messages``
     - ``tariff_message`` published (``identifier_type = IdToken``)
   * - ``SetUserPrice`` timeout, ``DefaultPriceText`` configured
     - ``token_validation_status`` includes ``messages``
     - ``tariff_message`` published from ``DefaultPriceText`` (``identifier_type = IdToken``)
   * - In-session ``SetUserPrice`` DataTransfer
     - —
     - ``tariff_message`` published (``identifier_type = TransactionId``)
   * - ``RunningCost`` / ``FinalCost`` DataTransfer (online)
     - —
     - ``session_cost`` published (may include ``message``)
   * - Transaction ended offline
     - —
     - nothing published
   * - Reconnected, CSMS sends ``FinalCost`` DataTransfer
     - —
     - ``session_cost`` with status ``Finished`` published
