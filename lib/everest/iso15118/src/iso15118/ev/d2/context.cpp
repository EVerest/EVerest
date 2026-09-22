// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/context.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>

namespace iso15118::ev::d2 {

namespace {
// [V2G2-832]: EAmount and EVEnergyRequest are at most 200000 Wh (Table 68).
constexpr float MAX_ENERGY_WH = 200000.0f;
} // namespace

Context::Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_,
                 params::EvSessionParams params__, session::Latches& latches_,
                 everest::lib::util::monitor<params::DcChargeParams>& dc_params_, bool has_cp_state_feedback__,
                 std::optional<message_2::datatypes::SessionId> resumed_session_id__,
                 config::WeldingDetectionConfig welding_detection_) :
    welding_detection(welding_detection_),
    feedback(std::move(feedback_callbacks)),
    message_exchange(message_exchange_),
    params_(std::move(params__)),
    latches(latches_),
    dc_params(dc_params_),
    has_cp_state_feedback_(has_cp_state_feedback__),
    resumed_session_id_(resumed_session_id__) {
    if (resumed_session_id_.has_value()) {
        session_id = resumed_session_id_.value();
    }
    // AC sends the configured EAmount when start_charging states none.
    auto& energy_wh = is_dc_mode(params_.energy_transfer_mode) or params_.energy_request.stated_energy_wh().has_value()
                          ? params_.energy_request.energy_wh
                          : params_.ac.e_amount;
    if (params::cap_energy(energy_wh, MAX_ENERGY_WH)) {
        logf_warning("ISO 15118-2: requested energy capped at %.0f Wh", MAX_ENERGY_WH);
    }
}

} // namespace iso15118::ev::d2
