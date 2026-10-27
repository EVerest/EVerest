# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Turn a parsed AsyncAPI spec into the in-memory model the client is built from.

Only the small spec subset the client needs is read:

  - servers.<server>.pathname          -> the MQTT topic prefix
  - operations.<id>.action             -> send / receive
  - operations.<id>.channel.$ref       -> channel.address (str or None)
  - operations.<id>.reply.channel.$ref -> reply cross-reference
  - operations.<id>.title/description  -> command help
  - components.messages.<id>.examples[0].payload -> tab-completion payload

Everything else (schemas, info, tags) is ignored. Invariants the client relies
on are checked so a malformed spec fails loudly.
"""

import json
import re
from dataclasses import dataclass, field
from typing import List, Optional

from everest_api.spec_loader import (
    SpecError, channel_ref_id, find_request_operation, json_compatible, resolve_channel)

# A channel parameter in an address, e.g. {api_type} in query-modules/{api_type}.
_CHANNEL_PARAM = re.compile(r"\{([A-Za-z_][A-Za-z0-9_]*)\}")


def reply_topic(request_topic: str) -> str:
    """The topic a reply to a request on *request_topic* is published to: the
    direction level m2e becomes e2m (the replyTo address the client generates)."""
    return "/".join("e2m" if level == "m2e" else level for level in request_topic.split("/"))


def _channel_params(address: Optional[str]) -> List[str]:
    return _CHANNEL_PARAM.findall(address) if address else []


def compact_json(payload) -> str:
    """Compact JSON without spaces, keys in source order, non-ASCII kept verbatim."""
    # default=str: YAML turns unquoted dates and timestamps into datetime objects
    return json.dumps(json_compatible(payload), separators=(",", ":"),
                      ensure_ascii=False, default=str)


@dataclass
class Operation:
    op_id: str
    action: str                       # "send" or "receive"
    address: Optional[str]            # channel address, or None for reply channels
    is_reply: bool                    # address is None
    # For a reply channel (address None), the address of the request operation
    # that references it (used as the reply's topic). None otherwise.
    referencing_address: Optional[str] = None
    # Single example payload as compact JSON, or "" when not exactly one example.
    payload_json: str = ""
    help: str = ""
    # For a request (a send operation that gets a reply), the receive
    # operation its reply arrives on. None otherwise.
    reply_op_id: Optional[str] = None

    @property
    def params(self) -> List[str]:
        """Channel parameters of the operation's topic, in address order."""
        return _channel_params(self.address if self.address is not None else self.referencing_address)

    @property
    def subscribe_topic(self) -> str:
        """The MQTT subscription of a receive operation. A reply receiver (null
        address) subscribes to the reply topic of its request plus a /+ level
        for the per-request id, and each channel parameter becomes a + wildcard."""
        topic = self.address if self.address is not None else reply_topic(self.referencing_address) + "/+"
        return _CHANNEL_PARAM.sub("+", topic)

    def send_topic(self, values) -> str:
        """The address of a send operation with each channel parameter replaced
        by values[<param>]."""
        return _CHANNEL_PARAM.sub(lambda m: str(values[m.group(1)]), self.address)


@dataclass
class ApiModel:
    api: str                          # e.g. powermeter (from powermeter_API.yaml)
    pathname: str                     # e.g. everest_api/1/powermeter/{module_id}
    send_ops: List[Operation] = field(default_factory=list)
    recv_ops: List[Operation] = field(default_factory=list)

    @property
    def needs_module_id(self) -> bool:
        return "{module_id}" in self.pathname

    def prefix(self, module_id: str) -> str:
        return self.pathname.replace("{module_id}", module_id)


def api_name_from_file(stem: str) -> str:
    """powermeter_API -> powermeter, power_supply_DC_API -> power_supply_dc"""
    if stem.endswith("_API"):
        stem = stem[:-len("_API")]
    return stem.lower()


def _find_referencing_address(doc, operations, target_channel_id):
    """Find the operation whose reply.channel points at *target_channel_id* and
    return that operation's own channel address."""
    _, request_op = find_request_operation(operations, target_channel_id)
    if request_op is None:
        return None
    return _mapping(resolve_channel(doc, request_op.get("channel")), "channel").get("address")


def _mapping(node, what):
    if not isinstance(node, dict):
        raise SpecError(f"{what} is not a mapping")
    return node


def _example_payload_json(doc, op_id) -> str:
    messages = _mapping(_mapping(doc.get("components", {}), "components").get("messages", {}),
                        "components.messages")
    if op_id not in messages:
        raise SpecError(
            f"operation '{op_id}' has no matching components.messages entry")
    examples = _mapping(messages[op_id], f"message '{op_id}'").get("examples")
    if isinstance(examples, list) and len(examples) == 1 and isinstance(examples[0], dict):
        return compact_json(examples[0].get("payload"))
    return ""


def _help_text(op) -> str:
    text = op.get("title") or op.get("summary") or op.get("description") or ""
    return " ".join(str(text).split())


def build_model(doc, api, server="default"):
    servers = _mapping(doc.get("servers", {}), "servers")
    if server not in servers:
        raise SpecError(f"server '{server}' not found in spec")
    pathname = _mapping(servers[server], f"server '{server}'").get("pathname")
    if not isinstance(pathname, str) or not pathname:
        raise SpecError(f"server '{server}' has no pathname")
    # Addresses are joined to the pathname with '/'. APIs served by a fixed
    # endpoint (e.g. everest_api/1/configuration/) end their pathname with one.
    pathname = pathname.rstrip("/")

    operations = doc.get("operations")
    if not isinstance(operations, dict):
        raise SpecError("spec has no operations mapping")

    model = ApiModel(api=api, pathname=pathname)

    for op_id, op in operations.items():
        _mapping(op, f"operation '{op_id}'")
        action = op.get("action")
        if action not in ("send", "receive"):
            raise SpecError(f"operation '{op_id}' has invalid action {action!r}")
        channel = _mapping(resolve_channel(doc, op.get("channel")), f"channel of operation '{op_id}'")
        address = channel.get("address")  # str or None
        is_reply = address is None

        referencing_address = None
        if is_reply:
            target_channel_id = channel_ref_id(op["channel"])
            referencing_address = _find_referencing_address(
                doc, operations, target_channel_id)
            if referencing_address is None:
                raise SpecError(
                    f"reply operation '{op_id}' (channel '{target_channel_id}') "
                    "is not referenced by any operation's reply.channel")

        entry = Operation(
            op_id=op_id,
            action=action,
            address=address,
            is_reply=is_reply,
            referencing_address=referencing_address,
            payload_json=_example_payload_json(doc, op_id) if action == "send" else "",
            help=_help_text(op),
        )
        if action == "send":
            model.send_ops.append(entry)
        else:
            model.recv_ops.append(entry)

    for reply_op in (op for op in model.recv_ops if op.is_reply):
        for request in model.send_ops:
            if request.address == reply_op.referencing_address:
                request.reply_op_id = reply_op.op_id

    return model
