# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Deterministic ordering rules.

Order is part of the generated C++ API rather than a detail of it -- generated
types are positionally aggregate initialised in everest-core -- so the rules
live in one named place instead of emerging from the order in which a parser
happened to append to a list.
"""

from __future__ import annotations

from typing import Iterable, Sequence

from .types import Field, StructDef


def order_fields(fields: Iterable[Field]) -> tuple[Field, ...]:
    """Required fields first, source order preserved within each group.

    Required members must precede optional ones because the generated
    ``to_json`` builds the required part as a brace initialiser, and because
    aggregate initialisation of a generated struct would otherwise change
    meaning.  The partition is stable, so two fields with the same requiredness
    keep the order they were written in.
    """
    materialised = list(fields)
    return tuple(
        [f for f in materialised if f.required]
        + [f for f in materialised if not f.required]
    )


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
