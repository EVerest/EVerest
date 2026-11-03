// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
#include <string>

#include <generated/types/iso15118.hpp>

#include <iso15118/ev/d2/pnc_config.hpp>
#include <iso15118/ev/feedback.hpp>
#include <iso15118/message/v2g_message_type.hpp>

namespace everest::lib::iso15118_ev {

// Where a session's feedback goes, in EVerest types. An unset member drops that output. Every member
// is called on the thread that runs the Controller's loop.
struct SessionOutputs {
    std::function<void(const iso15118::V2gMessageType&)> v2g_message;
    std::function<void(float voltage, float current)> dc_evse_present_values;
    std::function<void()> ev_power_ready;
    std::function<void()> dc_power_on;
    std::function<void()> stop_from_charger;
    std::function<void()> pause_from_charger;
    std::function<void()> power_delivery_stopped;
    std::function<void(const types::iso15118::WeldingDetectionFailure&)> welding_detection_failed;
    std::function<void(float)> ac_evse_max_current;
    std::function<void(const types::iso15118::AcTargetPower&)> ac_evse_target_power;
    std::function<void(const types::iso15118::DerNegotiatedFunctions&)> der_negotiated_functions;
    std::function<void(const types::iso15118::DerControlReceived&)> der_control_received;
    // A contract the session installed and that decodes; kept so a resume of this session presents it.
    std::function<void(iso15118::ev::d2::ContractMaterial)> contract_installed;
};

// The Controller callbacks of one session. An installed contract is stored in `pnc_contract_store_dir`
// when it is set. Log lines start with `prefix`.
iso15118::ev::feedback::Callbacks
make_session_callbacks(SessionOutputs outputs, const std::string& pnc_contract_store_dir, const std::string& prefix);

} // namespace everest::lib::iso15118_ev
