# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Public API contract snapshot and diff for EVerest.

The contract is the surface defined in docs/source/project/releases/releases-and-versioning.rst:
AsyncAPI specifications, the JSON RPC API, module and configuration contracts, OCPP configuration
files and the SQL migrations of every persistent store. ``snapshot`` reduces a source tree to one
JSON document, ``rules`` classify the differences between two of them following
docs/source/project/releases/breaking-changes.rst.
"""
