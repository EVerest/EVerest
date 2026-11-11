# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The order C++ needs structs in.

The IR keeps the order the definitions were written in.  For C++ the order is
part of the generated API -- generated types are positionally aggregate
initialised in everest-core -- so the rules live here, in one named place.
"""

from __future__ import annotations

from collections.abc import Sequence

from ...ir.types import StructDef


def order_structs(structs: Sequence[StructDef]) -> tuple[StructDef, ...]:
    """Emit every struct after the structs it embeds, else in source order.

    C++ needs a complete type before it can be used as a member, and the
    generated headers carry no forward declarations.  A struct that embeds an
    inline object therefore has to follow it.  This is a stable topological
    sort: with no dependencies -- which is the case for every type file in
    everest-core today -- it returns the source order untouched.
    """
    by_name = {struct.name: struct for struct in structs}
    ordered: list[StructDef] = []
    placed: set[str] = set()
    visiting: set[str] = set()

    def place(struct: StructDef) -> None:
        if struct.name in placed:
            return
        if struct.name in visiting:
            # A cycle cannot be expressed in C++ without indirection; leave the
            # struct where it is and let the compiler report it against the
            # generated source rather than guessing at a fix here.
            return
        visiting.add(struct.name)
        for dependency in struct.depends_on:
            embedded = by_name.get(dependency)
            if embedded is not None:
                place(embedded)
        visiting.discard(struct.name)
        placed.add(struct.name)
        ordered.append(struct)

    for struct in structs:
        place(struct)
    return tuple(ordered)
