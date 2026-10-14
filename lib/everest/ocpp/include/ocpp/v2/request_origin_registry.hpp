// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <utility>

#include <everest/util/async/monitor.hpp>

#include <ocpp/v2/connection_id.hpp>

namespace ocpp {
namespace v2 {

/// \brief Remembers which OCPP connection asked for something, so that the messages that answer it
///        later go back to that connection rather than to the CSMS.
///
/// Some requests are confirmed straight away and then answered over time by a series of further
/// messages, tied to the original request only by a requestId: GetBaseReport by NotifyReports,
/// GetLog by LogStatusNotifications, and so on (the generic OCPP requirement FR.05). Those
/// follow-ups are produced long after the request was handled, from timers and from EVerest
/// callbacks, so the connection cannot be carried along on the thread the way a direct response is.
///
/// A requestId is a plain integer, and two connections can easily pick the same one. Entries are
/// therefore keyed by the kind of request as well, and dropped as soon as the last follow-up has
/// been sent, so a collision needs two connections to have the same kind of request outstanding at
/// the same moment with the same id (application note section 5.3.1).
///
/// Safe to use from several threads.
class RequestOriginRegistry {
public:
    /// \brief The kinds of request that are answered by later messages.
    enum class RequestKind : std::uint8_t {
        Report,              ///< GetBaseReport, GetReport -> NotifyReport
        MonitoringReport,    ///< GetMonitoringReport -> NotifyMonitoringReport
        CustomerInformation, ///< CustomerInformation -> NotifyCustomerInformation
        Log,                 ///< GetLog -> LogStatusNotification
        FirmwareUpdate,      ///< UpdateFirmware -> FirmwareStatusNotification
    };

    /// \brief Records that \p connection asked for \p request_id of \p kind.
    ///
    /// Replaces any earlier entry for the same kind and id: the newer request is the live one.
    void remember(RequestKind kind, std::int32_t request_id, const ConnectionId& connection);

    /// \brief The connection that asked, or std::nullopt when nothing was recorded.
    ///
    /// \param last True when this is the final follow-up for the request, which drops the entry.
    ///        Keeping entries only for as long as a request is actually outstanding is what keeps
    ///        the risk of a requestId collision to the moment it is genuinely ambiguous.
    std::optional<ConnectionId> lookup(RequestKind kind, std::int32_t request_id, bool last);

    /// \brief Drops the entry for \p request_id of \p kind, if any.
    void forget(RequestKind kind, std::int32_t request_id);

private:
    using Key = std::pair<RequestKind, std::int32_t>;

    everest::lib::util::monitor<std::map<Key, ConnectionId>> origins;
};

} // namespace v2
} // namespace ocpp
