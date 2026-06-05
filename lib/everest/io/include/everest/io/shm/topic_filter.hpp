// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <string_view>

namespace everest::lib::io::shm {

/// \brief Return true when an MQTT-compatible topic filter is syntactically valid.
///
/// Supported wildcard semantics:
/// - '+' matches exactly one topic level and must occupy the complete filter level.
/// - '#' matches zero or more trailing topic levels and must occupy the complete final filter level.
/// - Literal filters contain no wildcard characters and match topics exactly.
bool is_valid_topic_filter(std::string_view filter);

/// \brief Match a topic filter against a concrete topic using MQTT-compatible wildcard semantics.
///
/// Invalid filters return false. This helper is intentionally allocation-free so framework-local
/// SHM routing can use it in dispatch paths and topic discovery code.
bool topic_filter_matches(std::string_view filter, std::string_view topic);

} // namespace everest::lib::io::shm
