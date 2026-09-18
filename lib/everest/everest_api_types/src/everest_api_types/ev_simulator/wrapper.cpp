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
    // val.battery_charge_wh has no counterpart in ::types::evse_manager::EVInfo
    // (no charge/energy field exists in types/evse_manager.yaml), so it is
    // intentionally dropped here. The conversion is asymmetric on both legs:
    // to_external_api zeroes it and the caller must populate it
    // post-conversion. An External -> Internal -> External round trip does
    // not preserve battery_charge_wh.
    return result;
}

EvInfo_External to_external_api(EvInfo_Internal const& val) {
    EvInfo_External result;
    // Note: EvInfo_External keeps non-optional floats (strict wire schema),
    // so missing internal fields collapse to 0.0f here. Downstream consumers
    // cannot distinguish "EV reports 0" from "EV did not report this field".
    // A typed change (making these std::optional<float> on the external side)
    // is the proper fix but breaks wire compatibility; leave as 0.0f for now.
    result.soc_pct = val.soc.value_or(0.0f);
    result.battery_capacity_wh = val.battery_capacity.value_or(0.0f);
    // No counterpart in internal EvInfo; caller must populate post-conversion.
    result.battery_charge_wh = 0.0f;
    result.target_current_a = val.target_current.value_or(0.0f);
    result.target_voltage_v = val.target_voltage.value_or(0.0f);
    return result;
}

BspEvent_Internal to_internal_api(BspEvent_External const& val) {
    using SrcK = BspEventKind;
    using TarE = ::types::board_support_common::Event;
    BspEvent_Internal result;
    switch (val.event) {
    case SrcK::A:
        result.event = TarE::A;
        return result;
    case SrcK::B:
        result.event = TarE::B;
        return result;
    case SrcK::C:
        result.event = TarE::C;
        return result;
    case SrcK::D:
        result.event = TarE::D;
        return result;
    case SrcK::E:
        result.event = TarE::E;
        return result;
    case SrcK::F:
        result.event = TarE::F;
        return result;
    case SrcK::PowerOn:
        result.event = TarE::PowerOn;
        return result;
    case SrcK::PowerOff:
        result.event = TarE::PowerOff;
        return result;
    case SrcK::Disconnected:
        result.event = TarE::Disconnected;
        return result;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::ev_simulator::BspEventKind");
}

BspEvent_External to_external_api(BspEvent_Internal const& val) {
    using SrcE = ::types::board_support_common::Event;
    using TarK = BspEventKind;
    BspEvent_External result;
    switch (val.event) {
    case SrcE::A:
        result.event = TarK::A;
        return result;
    case SrcE::B:
        result.event = TarK::B;
        return result;
    case SrcE::C:
        result.event = TarK::C;
        return result;
    case SrcE::D:
        result.event = TarK::D;
        return result;
    case SrcE::E:
        result.event = TarK::E;
        return result;
    case SrcE::F:
        result.event = TarK::F;
        return result;
    case SrcE::PowerOn:
        result.event = TarK::PowerOn;
        return result;
    case SrcE::PowerOff:
        result.event = TarK::PowerOff;
        return result;
    case SrcE::Disconnected:
        result.event = TarK::Disconnected;
        return result;
    }
    throw std::out_of_range("Unexpected value for ::types::board_support_common::Event");
}

SlacState_Internal to_internal_api(SlacState_External const& val) {
    using SrcK = SlacStateKind;
    using TarS = ::types::slac::State;
    switch (val.state) {
    case SrcK::Unmatched:
        return TarS::UNMATCHED;
    case SrcK::Matching:
        return TarS::MATCHING;
    case SrcK::Matched:
        return TarS::MATCHED;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::ev_simulator::SlacStateKind");
}

SlacState_External to_external_api(SlacState_Internal const& val) {
    using SrcS = ::types::slac::State;
    using TarK = SlacStateKind;
    SlacState_External result;
    switch (val) {
    case SrcS::UNMATCHED:
        result.state = TarK::Unmatched;
        return result;
    case SrcS::MATCHING:
        result.state = TarK::Matching;
        return result;
    case SrcS::MATCHED:
        result.state = TarK::Matched;
        return result;
    }
    throw std::out_of_range("Unexpected value for ::types::slac::State");
}

} // namespace everest::lib::API::V1_0::types::ev_simulator
