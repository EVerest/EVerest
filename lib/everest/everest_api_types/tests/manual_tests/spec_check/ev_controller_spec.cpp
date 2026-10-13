// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "SpecDrift.hpp"

#include <everest_api_types/ev_controller/API.hpp>
#include <everest_api_types/ev_controller/codec.hpp>
#include <everest_api_types/generic/API.hpp>
#include <everest_api_types/generic/codec.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace api = everest::lib::API::V1_0::types::ev_controller;
namespace generic = everest::lib::API::V1_0::types::generic;
using spec_drift::json;

template <class T> json round_trip(json const& payload) {
    T value{};
    if (not api::adl_deserialize(payload.dump(), value)) {
        throw std::invalid_argument("deserialization failed");
    }
    return json::parse(api::serialize(value));
}

template <class T> json generic_round_trip(json const& payload) {
    T value{};
    if (not generic::adl_deserialize(payload.dump(), value)) {
        throw std::invalid_argument("deserialization failed");
    }
    return json::parse(generic::serialize(value));
}

template <class T> json codec(T const& value) {
    return json::parse(api::serialize(value));
}

api::ChargingCurve curve() {
    return {{{0, 6.0F, false, 1000}, {5000, 16.0F, true, std::nullopt}}, true};
}

api::BptParams bpt() {
    return {32.0F, 11000.0F, 16.0F, 20.0F};
}

api::SessionConfigParams full_session(api::ChargeMode mode) {
    const auto payment = api::PaymentOption::Contract;
    const api::AcIsoD20DerParams der{payment, 3600, 20000, 16.0F, true, curve()};
    switch (mode) {
    case api::ChargeMode::AcIec:
        return api::AcIecSessionParams{16.0F, true, curve()};
    case api::ChargeMode::AcIso2:
        return api::AcIso2SessionParams{payment, 3600, 20000, 16.0F, true, curve()};
    case api::ChargeMode::AcIsoD20:
        return api::AcIsoD20SessionParams{payment, 3600, 20000, 16.0F, true, bpt(), curve()};
    case api::ChargeMode::AcIsoD20DerIec:
        return api::AcIsoD20DerIecSessionParams{der};
    case api::ChargeMode::AcIsoD20DerSae:
        return api::AcIsoD20DerSaeSessionParams{der};
    case api::ChargeMode::DcIso2:
        return api::DcIso2SessionParams{payment, 3600, 20000, curve()};
    case api::ChargeMode::DcIsoD20:
        return api::DcIsoD20SessionParams{payment, 3600, 20000, bpt(), true, curve()};
    }
    throw std::out_of_range("unknown ChargeMode");
}

template <class E> std::vector<json> values() {
    return spec_drift::enum_values<E>([](E value) { return api::serialize(value); });
}

spec_drift::Channels channels() {
    std::vector<json> sessions;
    for (auto const& mode : values<api::ChargeMode>()) {
        sessions.push_back(codec(full_session(api::deserialize<api::ChargeMode>(mode.dump()))));
    }
    const auto error =
        json::parse(generic::serialize(generic::Error{generic::ErrorEnum::VendorError, "sub type", "message"}));
    const std::vector<json> flags{json::parse(generic::serialize(true)), json::parse(generic::serialize(false))};

    return {
        {"m2e/enable", {generic_round_trip<bool>, flags}},
        {"m2e/configure_session", {round_trip<api::SessionConfigParams>, sessions}},
        {"m2e/stop_session", {round_trip<api::StopSessionParams>, {codec(api::StopSessionParams{true})}}},
        // No codec: the module ignores the payload of a command without arguments.
        {"m2e/pause_session", {nullptr, {json::object()}}},
        {"m2e/resume_session", {nullptr, {json::object()}}},
        {"m2e/set_charging_current",
         {round_trip<api::SetChargingCurrentParams>, {codec(api::SetChargingCurrentParams{16.0F, true, 2000})}}},
        {"m2e/set_soc", {round_trip<api::SetSocParams>, {codec(api::SetSocParams{42.5F})}}},
        {"m2e/set_present_values",
         {round_trip<api::SetPresentValuesParams>, {codec(api::SetPresentValuesParams{400.0F, 11000.0F})}}},
        {"m2e/bcb_toggle", {round_trip<api::BcbToggleParams>, {codec(api::BcbToggleParams{3})}}},
        {"m2e/query_state", {nullptr, {json::object()}}},
        {"m2e/raise_error", {generic_round_trip<generic::Error>, {error}}},
        {"m2e/clear_error", {generic_round_trip<generic::Error>, {error}}},
        {"m2e/communication_check", {generic_round_trip<bool>, flags}},
        {"e2m/state", {round_trip<api::FsmState>, values<api::FsmState>()}},
        {"e2m/ev_info", {round_trip<api::EvInfo>, {codec(api::EvInfo{50.0F, 60000.0F, 30000.0F, 0.0F, 0.0F})}}},
        {"e2m/bsp_event", {round_trip<api::BspEvent>, {codec(api::BspEvent{api::BspEventKind::B})}}},
        // EvController writes this payload by hand (FsmContext::publish_e2m_bsp_measurement), not with a codec.
        {"e2m/bsp_measurement", {nullptr, {json{{"cp_pwm_duty_cycle", 53.0}, {"rcd_current_mA", 5.0}}}}},
        {"e2m/slac_state", {round_trip<api::SlacState>, {codec(api::SlacState{api::SlacStateKind::Matched})}}},
        {"e2m/iso_session_event",
         {round_trip<api::IsoSessionEvent>,
          {codec(api::IsoSessionEvent{api::IsoSessionEventKind::DcPowerOn, 400.0F, 100.0F})}}},
        {"e2m/der_negotiated_functions",
         {round_trip<api::DerNegotiatedFunctions>,
          {codec(api::DerNegotiatedFunctions{api::DerFlavor::IEC, {"FixedPowerFactor"}})}}},
        {"e2m/der_control",
         {round_trip<api::DerControl>,
          {codec(api::DerControl{api::DerFlavor::SAE, api::DerControlSource::ChargeLoop, true,
                                 std::vector<std::string>{"ConstantVar"}, std::vector<std::string>{"problem"}, 1500.0F,
                                 0.5F})}}},
        {"e2m/fault",
         {round_trip<api::FaultReport>, {codec(api::FaultReport{api::FaultType::Internal, "message", 30.0F})}}},
        {"e2m/command_ack",
         {round_trip<api::CommandAck>,
          {codec(api::CommandAck{"resume_session", api::CommandAckStatus::Rejected, "reason"})}}},
        {"e2m/heartbeat", {generic_round_trip<std::size_t>, {json::parse(generic::serialize(std::size_t{42}))}}},
    };
}

spec_drift::Enums enums() {
    return {
        {"FsmState", values<api::FsmState>()},
        {"PaymentOption", values<api::PaymentOption>()},
        {"BspEventKind", values<api::BspEventKind>()},
        {"SlacStateKind", values<api::SlacStateKind>()},
        {"IsoSessionEventKind", values<api::IsoSessionEventKind>()},
        {"DerFlavor", values<api::DerFlavor>()},
        {"DerControlSource", values<api::DerControlSource>()},
        {"FaultType", values<api::FaultType>()},
        {"CommandAckStatus", values<api::CommandAckStatus>()},
        {"ErrorEnum", spec_drift::enum_values<generic::ErrorEnum>(
                          [](generic::ErrorEnum value) { return generic::serialize(value); })},
    };
}

TEST(ApiSpecifications, ev_controller_spec_matches_its_codec) {
    const auto spec = std::filesystem::path{EVEREST_API_SPEC_DIR} / "ev_controller_API.yaml";
    for (auto const& error : spec_drift::check_api(spec, channels(), enums())) {
        ADD_FAILURE() << error;
    }
}

} // namespace
