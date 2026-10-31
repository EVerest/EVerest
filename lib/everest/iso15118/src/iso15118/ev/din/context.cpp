// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/context.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::ev::din {

namespace {
// Table 55: a PhysicalValueType carries at most 32767 * 10^3.
constexpr float MAX_ENERGY_WH = 32767000.0f;
} // namespace

Context::Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_,
                 params::EvSessionParams params__, session::Latches& latches_,
                 everest::lib::util::monitor<params::DcChargeParams>& dc_params_, bool has_cp_state_feedback__,
                 std::optional<message_din::datatypes::SessionId> resumed_session_id__,
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
    if (params::cap_energy(params_.energy_request.energy_wh, MAX_ENERGY_WH)) {
        logf_warning("DIN SPEC 70121: requested energy capped at %.0f Wh", MAX_ENERGY_WH);
    }
}

} // namespace iso15118::ev::din
