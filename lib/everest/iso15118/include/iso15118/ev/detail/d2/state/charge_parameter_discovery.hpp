// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <optional>

#include <everest/util/vector/fixed_vector.hpp>

#include <iso15118/ev/params/dc.hpp>
#include <iso15118/ev/params/energy_request.hpp>
#include <iso15118/ev/params/iso2_ac.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace charge_parameter_discovery {

// [V2G2-310/610]: how many PMaxScheduleEntry/SalesTariff entries per tuple the EVCC can process; the
// SAScheduleList pmax_schedule fixed_vector capacity.
constexpr uint16_t MAX_ENTRIES_SA_SCHEDULE_TUPLE = 12;

// DC_EVChargeParameter from the module's DC limits and start_charging's energy request as of \p now.
// full_soc/bulk_soc have no DcChargeParams counterpart and stay unset.
message_2::ChargeParameterDiscoveryRequest create_dc_request(dt::EnergyTransferMode mode,
                                                             const params::DcChargeParams& params,
                                                             const params::EnergyRequest& energy,
                                                             std::chrono::steady_clock::time_point now);

// EAmount falls back to the configured one when start_charging states none.
message_2::ChargeParameterDiscoveryRequest create_ac_request(dt::EnergyTransferMode mode,
                                                             const params::Iso2AcParams& params,
                                                             const params::EnergyRequest& energy,
                                                             std::chrono::steady_clock::time_point now);

struct Result {
    // A Finished response that leaves a legal way on: [V2G2-286]/[V2G2-773] need an SAScheduleTupleID.
    bool valid{false};
    bool finished{false};

    // From the first SAScheduleTuple, if present.
    std::optional<uint8_t> sa_schedule_tuple_id{std::nullopt};
    everest::lib::util::fixed_vector<dt::PMaxScheduleEntry, 12> selected_pmax_schedule{};

    // SECC-advertised limits.
    std::optional<iso15118::session::feedback::DcMaximumLimits> dc_limits{std::nullopt};
    std::optional<dt::PhysicalValue> ac_nominal_voltage{std::nullopt};
    std::optional<dt::PhysicalValue> ac_evse_max_current{std::nullopt};
};

Result handle_response(const message_2::ChargeParameterDiscoveryResponse& res);

} // namespace charge_parameter_discovery

} // namespace iso15118::ev::d2::state
