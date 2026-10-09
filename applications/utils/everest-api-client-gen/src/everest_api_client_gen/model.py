# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Turn a parsed AsyncAPI spec into the model the Jinja templates render.

This encodes every derivation rule the old npm/React template implemented,
reading only the small spec subset the clients actually need:

  - servers.<server>.pathname          -> the MQTT topic prefix
  - operations.<id>.action             -> send / receive
  - operations.<id>.channel.$ref       -> channel.address (str or None)
  - operations.<id>.reply.channel.$ref -> reply cross-reference
  - components.messages.<id>.examples[0].payload -> tab-completion payload

Everything else (schemas, info, tags, descriptions) is ignored. Invariants the
generation relies on are asserted so a malformed spec fails loudly.
"""

import json
from dataclasses import dataclass, field
from typing import List, Optional

from everest_api_client_gen.spec_loader import SpecError, channel_ref_id, resolve_channel, resolve_ref

# Operations provided globally by the shell (api_client.py), so they are left
# out of the per-client do_* command verbs. client.py still wires them up.
_SHELL_GLOBAL_SEND = "send_communication_check"
_SHELL_GLOBAL_RECV = "receive_heartbeat"


def _js_normalize(value):
    """Recursively convert values so json.dumps reproduces JS JSON.stringify.

    JS numbers are all doubles: an integral float like 230.0 serializes as
    "230", not "230.0". Non-integral floats already match (both JS and Python
    print the shortest round-tripping representation). bool must be handled
    before the numeric branch since bool is a subclass of int.
    """
    if isinstance(value, bool):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    if isinstance(value, dict):
        return {k: _js_normalize(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_js_normalize(v) for v in value]
    return value


def _compact_json(payload) -> str:
    """Match JS JSON.stringify default output: no spaces, keys in source order,
    non-ASCII kept verbatim, integral floats without a trailing ".0"."""
    return json.dumps(_js_normalize(payload), separators=(",", ":"),
                      ensure_ascii=False)


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


@dataclass
class ClientModel:
    name_camel: str
    name_snake: str
    interface_prefix: str
    pathname: str                     # e.g. everest_api/1/powermeter/{module_id}
    send_ops: List[Operation] = field(default_factory=list)
    recv_ops: List[Operation] = field(default_factory=list)

    @property
    def class_name(self) -> str:
        return self.name_camel + "Client"

    @property
    def command_set_name(self) -> str:
        return self.name_camel + "ClientCommandSet"

    # do_* verbs exclude the shell-global operations, but client.py init and the
    # command set's callbacks/on_* handlers use the full lists.
    @property
    def send_command_ops(self) -> List[Operation]:
        return [o for o in self.send_ops if o.op_id != _SHELL_GLOBAL_SEND]

    @property
    def recv_command_ops(self) -> List[Operation]:
        return [o for o in self.recv_ops if o.op_id != _SHELL_GLOBAL_RECV]

    def receive_topic(self, op: Operation) -> str:
        """The per-operation _topic used in _initialize_receive_operations
        (bare address; no trailing /+)."""
        addr = op.address if op.address is not None else op.referencing_address
        return addr

    def subscribe_topic(self, op: Operation) -> str:
        """The topic string put in the client's subscribe set. Reply-receiver
        topics (null address) get a trailing /+ wildcard."""
        if op.address is not None:
            return op.address
        return op.referencing_address + "/+"


def _find_referencing_address(doc, operations, target_channel_id):
    """Find the operation whose reply.channel points at *target_channel_id* and
    return that operation's own channel address.

    Mirrors the template's getReferencingChannelAddress, including the historic
    'm2b' -> 'b2m' rewrite (a no-op on current specs, which use m2e/e2m).
    """
    for other in operations.values():
        reply = other.get("reply")
        if not isinstance(reply, dict):
            continue
        reply_channel = reply.get("channel")
        if not isinstance(reply_channel, dict) or "$ref" not in reply_channel:
            continue
        if channel_ref_id(reply_channel) == target_channel_id:
            addr = resolve_channel(doc, other["channel"]).get("address")
            if addr is None:
                return None
            return addr.replace("m2b", "b2m")
    return None


def _example_payload_json(doc, op_id) -> str:
    messages = doc.get("components", {}).get("messages", {})
    if op_id not in messages:
        raise SpecError(
            f"operation '{op_id}' has no matching components.messages entry")
    examples = messages[op_id].get("examples")
    if isinstance(examples, list) and len(examples) == 1:
        return _compact_json(examples[0].get("payload"))
    return ""


def build_model(doc, name_camel, name_snake, interface_prefix, server):
    servers = doc.get("servers", {})
    if server not in servers:
        raise SpecError(f"server '{server}' not found in spec")
    pathname = servers[server].get("pathname")
    if not pathname:
        raise SpecError(f"server '{server}' has no pathname")

    operations = doc.get("operations")
    if not isinstance(operations, dict):
        raise SpecError("spec has no operations mapping")

    model = ClientModel(
        name_camel=name_camel,
        name_snake=name_snake,
        interface_prefix=interface_prefix,
        pathname=pathname,
    )

    for op_id, op in operations.items():
        action = op.get("action")
        if action not in ("send", "receive"):
            raise SpecError(f"operation '{op_id}' has invalid action {action!r}")
        channel = resolve_channel(doc, op.get("channel"))
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

        payload_json = ""
        if action == "send":
            payload_json = _example_payload_json(doc, op_id)

        entry = Operation(
            op_id=op_id,
            action=action,
            address=address,
            is_reply=is_reply,
            referencing_address=referencing_address,
            payload_json=payload_json,
        )
        if action == "send":
            model.send_ops.append(entry)
        else:
            model.recv_ops.append(entry)

    return model
