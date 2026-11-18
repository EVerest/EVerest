# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""The intermediate representation ev-cli's backends consume.

Nothing in here knows about C++, or about any other target language: it
describes what the EVerest definitions mean, and leaves the spelling of that
meaning to a backend.
"""
