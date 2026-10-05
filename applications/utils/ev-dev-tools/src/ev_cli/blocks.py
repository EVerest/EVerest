# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The ``ev@<uuid>`` markers that protect hand-written code.

``module update`` regenerates files a human has already edited.  What makes
that safe is a pair of marker comments around each region the human owns: on
update the region is read back out of the existing file and put into the newly
rendered one, so everything between the markers survives.

The markers are therefore a format, not a convenience.  A file whose markers
have been mangled is refused rather than regenerated, because the alternative
is silently dropping someone's work.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping

from .errors import BlockError

#: The uuid pattern the markers use, which is a version-4 uuid.
_UUID = r'[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}'


@dataclass(frozen=True)
class BlockDefinition:
    """One protected region."""

    uuid: str
    #: What is written between the markers when the file is first created.
    placeholder: str


@dataclass(frozen=True)
class BlockSet:
    """The protected regions of one kind of generated file."""

    version: str
    #: How a marker is spelled, e.g. ``// ev@{uuid}:{version}``.
    marker: str
    definitions: Mapping[str, BlockDefinition]

    @property
    def pattern(self) -> re.Pattern:
        comment = re.escape(self.marker.split('ev@')[0].strip())
        return re.compile(
            rf'^(?P<indent>\s*){comment} ev@(?P<uuid>{_UUID}):(?P<version>.*)$'
        )

    def tag(self, definition: BlockDefinition) -> str:
        return self.marker.format(uuid=definition.uuid, version=self.version)

    def by_uuid(self, uuid: str) -> tuple[str, BlockDefinition] | None:
        for name, definition in self.definitions.items():
            if definition.uuid == uuid:
                return name, definition
        return None


@dataclass
class Block:
    """A protected region, ready to render."""

    tag: str
    content: str
    #: Whether this is the placeholder rather than something a human wrote.
    #: Only the placeholder gets indented by the renderer; captured content
    #: keeps the indentation it was written with.
    first_use: bool = True

    def render(self, indent: int = 0) -> str:
        padding = ' ' * indent
        opening = f'{padding}{self.content}' if self.first_use else self.content
        return f'{self.tag}\n{opening}\n{padding}{self.tag}'


def placeholders(block_set: BlockSet) -> dict[str, Block]:
    """The regions of a file that does not exist yet."""
    return {
        name: Block(tag=block_set.tag(definition), content=definition.placeholder)
        for name, definition in block_set.definitions.items()
    }


def load(block_set: BlockSet, path: Path, *, update: bool) -> dict[str, Block]:
    """The regions to render, read back from ``path`` when updating."""
    if not update or not path.exists():
        return placeholders(block_set)
    return extract(block_set, path)


def extract(block_set: BlockSet, path: Path) -> dict[str, Block]:
    """Read the protected regions out of an existing generated file."""
    blocks = placeholders(block_set)

    try:
        text = path.read_text()
    except OSError as err:
        raise BlockError(
            f'could not read {path} to preserve its marked regions: {err.strerror}'
        ) from err

    open_block: tuple[str, str] | None = None   # (name, tag)
    collected: list[str] = []

    for number, line in enumerate(text.splitlines(keepends=True), start=1):
        if open_block is None:
            open_block = _match_marker(block_set, line.rstrip(), number, path)
            collected = []
            continue

        name, tag = open_block
        if line.strip() == tag:
            if collected:
                blocks[name] = Block(
                    tag=tag, content=''.join(collected).rstrip(), first_use=False
                )
            open_block = None
        else:
            collected.append(line)

    if open_block is not None:
        raise BlockError(
            f'{path}: the marked region opened by "{open_block[1]}" is never '
            f'closed. Regenerating would drop whatever follows it, so nothing '
            f'was written.'
        )

    return blocks


def _match_marker(
    block_set: BlockSet, line: str, number: int, path: Path
) -> tuple[str, str] | None:
    match = block_set.pattern.match(line)
    if not match:
        return None

    uuid, version = match.group('uuid'), match.group('version')

    if version != block_set.version:
        raise BlockError(
            f'{path}:{number}: marked region is version "{version}", but this '
            f'kind of file uses version "{block_set.version}"'
        )

    found = block_set.by_uuid(uuid)
    if found is None:
        raise BlockError(
            f'{path}:{number}: marked region has uuid "{uuid}", which is not '
            f'one of the regions this kind of file defines'
        )

    name, definition = found
    return name, block_set.tag(definition)


# --------------------------------------------------------------------------
# The block sets of the files ``module create`` manages.
#
# The uuids are part of the file format: they are what lets an already
# generated file be matched up with the region it belongs to, so they are
# fixed for all time and must never be regenerated.
# --------------------------------------------------------------------------

CMAKELISTS = BlockSet(
    version='v1',
    marker='# ev@{uuid}:{version}',
    definitions={
        'add_general': BlockDefinition(
            'bcc62523-e22b-41d7-ba2f-825b493a3c97',
            '# insert your custom targets and additional config variables here'),
        'add_other': BlockDefinition(
            'c55432ab-152c-45a9-9d2e-7281d50c69c3',
            '# insert other things like install cmds etc here'),
    },
)

IMPLEMENTATION_HEADER = BlockSet(
    version='v1',
    marker='// ev@{uuid}:{version}',
    definitions={
        'add_headers': BlockDefinition(
            '75ac1216-19eb-4182-a85c-820f1fc2c091',
            '// insert your custom include headers here'),
        'public_defs': BlockDefinition(
            '8ea32d28-373f-4c90-ae5e-b4fcc74e2a61',
            '// insert your public definitions here'),
        'protected_defs': BlockDefinition(
            'd2d1847a-7b88-41dd-ad07-92785f06f5c4',
            '// insert your protected definitions here'),
        'private_defs': BlockDefinition(
            '3370e4dd-95f4-47a9-aaec-ea76f34a66c9',
            '// insert your private definitions here'),
        'after_class': BlockDefinition(
            '3d7da0ad-02c2-493d-9920-0bbbd56b9876',
            '// insert other definitions here'),
    },
)

MODULE_HEADER = BlockSet(
    version='v1',
    marker='// ev@{uuid}:{version}',
    definitions={
        'add_headers': BlockDefinition(
            '4bf81b14-a215-475c-a1d3-0a484ae48918',
            '// insert your custom include headers here'),
        'public_defs': BlockDefinition(
            '1fce4c5e-0ab8-41bb-90f7-14277703d2ac',
            '// insert your public definitions here'),
        'protected_defs': BlockDefinition(
            '4714b2ab-a24f-4b95-ab81-36439e1478de',
            '// insert your protected definitions here'),
        'private_defs': BlockDefinition(
            '211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8',
            '// insert your private definitions here'),
        'after_class': BlockDefinition(
            '087e516b-124c-48df-94fb-109508c7cda9',
            '// insert other definitions here'),
    },
)
