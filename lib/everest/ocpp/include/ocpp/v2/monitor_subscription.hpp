// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <set>
#include <string>

#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp {
namespace v2 {

/// \brief The monitor ids of a MonitoringCtrlr[<instance>].SubscribedMonitors value.
///
/// The variable holds a comma separated list of the monitor ids whose NotifyEvents one OCPP
/// connection wants to be sent (application note section 5.5). It is written both by the charging
/// station, when a connection creates a monitor, and by hand from a CSMS, so it has to survive
/// whitespace and the odd stray entry: anything that is not a number is logged and skipped rather
/// than voiding the rest of the list.
///
/// Ordered and deduplicated, because the list is a set of ids and nothing about it depends on the
/// order they were written in.
std::set<std::int32_t> parse_monitor_id_list(const std::string& value);

/// \brief The value to store for \p ids, in the form parse_monitor_id_list reads back.
std::string to_monitor_id_list(const std::set<std::int32_t>& ids);

/// \brief Whether \p event is a hardwired notification, which every connection that is allowed
///        NotifyEvent is sent regardless of what it has subscribed to.
///
/// Section 5.5: hardwired monitors and hardwired notifications report the charging station's own
/// behaviour rather than something a system asked to watch, so there is no subscription to consult
/// and nobody to attribute them to.
bool is_hardwired_notification(const EventData& event);

} // namespace v2
} // namespace ocpp
