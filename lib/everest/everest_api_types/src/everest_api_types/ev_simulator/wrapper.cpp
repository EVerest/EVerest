// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "ev_simulator/wrapper.hpp"
#include "ev_simulator/API.hpp"

namespace everest::lib::API::V1_0::types::ev_simulator {

PaymentOption_Internal to_internal_api(PaymentOption_External const& val) {
    using SrcT = PaymentOption_External;
    using TarT = PaymentOption_Internal;

    switch (val) {
    case SrcT::ExternalPayment:
        return TarT::ExternalPayment;
    case SrcT::Contract:
        return TarT::Contract;
    }

    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::ev_simulator::PaymentOption_External");
}

PaymentOption_External to_external_api(PaymentOption_Internal const& val) {
    using SrcT = PaymentOption_Internal;
    using TarT = PaymentOption_External;

    switch (val) {
    case SrcT::ExternalPayment:
        return TarT::ExternalPayment;
    case SrcT::Contract:
        return TarT::Contract;
    }

    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::ev_simulator::PaymentOption_Internal");
}

EvInfo_Internal to_internal_api(EvInfo_External const& val) {
    EvInfo_Internal result;
    result.soc = val.soc_pct;
    result.battery_capacity = val.battery_capacity_wh;
    result.target_current = val.target_current_a;
    result.target_voltage = val.target_voltage_v;
    return result;
}

EvInfo_External to_external_api(EvInfo_Internal const& val) {
    EvInfo_External result;
    result.soc_pct = val.soc.value_or(0.0f);
    result.battery_capacity_wh = val.battery_capacity.value_or(0.0f);
    // No counterpart in internal EvInfo; caller must populate post-conversion.
    result.battery_charge_wh = 0.0f;
    result.target_current_a = val.target_current.value_or(0.0f);
    result.target_voltage_v = val.target_voltage.value_or(0.0f);
    return result;
}

BspEvent_Internal to_internal_api(BspEvent_External const& val) {
    BspEvent_Internal result;
    result.event = ::types::board_support_common::string_to_event(val.event);
    return result;
}

BspEvent_External to_external_api(BspEvent_Internal const& val) {
    BspEvent_External result;
    result.event = ::types::board_support_common::event_to_string(val.event);
    return result;
}

SlacState_Internal to_internal_api(SlacState_External const& val) {
    return ::types::slac::string_to_state(val.state);
}

SlacState_External to_external_api(SlacState_Internal const& val) {
    SlacState_External result;
    result.state = ::types::slac::state_to_string(val);
    return result;
}

} // namespace everest::lib::API::V1_0::types::ev_simulator
