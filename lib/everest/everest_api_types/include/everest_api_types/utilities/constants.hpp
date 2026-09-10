// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

namespace everest::lib::API {
// -1 produces compact JSON. Values >= 0 pretty-print with that indent width,
// which significantly increases payload size and serialization cost.
#ifndef EVEREST_API_JSON_INDENT
#define EVEREST_API_JSON_INDENT (-1)
#endif
static const int json_indent = EVEREST_API_JSON_INDENT;
} // namespace everest::lib::API
