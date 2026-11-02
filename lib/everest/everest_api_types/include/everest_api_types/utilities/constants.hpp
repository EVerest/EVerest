// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstddef>

namespace everest::lib::API {
// Payloads that nest arrays and objects deeper than this are rejected before parsing
static const std::size_t max_json_nesting_depth = 128;

// -1 produces compact JSON. Values >= 0 pretty-print with that indent width,
// which significantly increases payload size and serialization cost.
#ifndef EVEREST_API_JSON_INDENT
#define EVEREST_API_JSON_INDENT (-1)
#endif
static const int json_indent = EVEREST_API_JSON_INDENT;
} // namespace everest::lib::API
