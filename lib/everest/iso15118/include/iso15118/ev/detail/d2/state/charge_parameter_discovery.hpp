// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <everest/util/vector/fixed_vector.hpp>

#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/session_params.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace charge_parameter_discovery {

// [V2G2-310/610]: how many PMaxScheduleEntry/SalesTariff entries per tuple the EVCC can process; the
// SAScheduleList pmax_schedule fixed_vector capacity.
constexpr uint16_t MAX_ENTRIES_SA_SCHEDULE_TUPLE = 12;

// DC_EVChargeParameter from the module's DC limits. ev_energy_request/full_soc/bulk_soc have no
// DcChargeParams counterpart and stay unset.
message_2::ChargeParameterDiscoveryRequest create_dc_request(dt::EnergyTransferMode mode, const DcChargeParams& params);

message_2::ChargeParameterDiscoveryRequest create_ac_request(dt::EnergyTransferMode mode, const Iso2AcParams& params);

struct Result {
    // A Finished response that leaves a legal way on: [V2G2-286]/[V2G2-773] need an SAScheduleTupleID.
    bool valid{false};
    bool finished{false};

    // From the first SAScheduleTuple, if present.
    std::optional<uint8_t> sa_schedule_tuple_id{std::nullopt};
    everest::lib::util::fixed_vector<dt::PMaxScheduleEntry, 12> selected_pmax_schedule{};

    // SECC-advertised limits.
    std::optional<session::feedback::DcMaximumLimits> dc_limits{std::nullopt};
    std::optional<dt::PhysicalValue> ac_nominal_voltage{std::nullopt};
};

Result handle_response(const message_2::ChargeParameterDiscoveryResponse& res);

} // namespace charge_parameter_discovery

} // namespace iso15118::ev::d2::state
