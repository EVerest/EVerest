// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "energy/codec.hpp"
#include "energy/API.hpp"
#include "energy/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"

namespace everest::lib::API::V1_0::types::energy {

std::string serialize(NumberWithSource const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(IntegerWithSource const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(FrequencyWattPoint const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SetpointType const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(PricePerkWh const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(LimitsReq const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(LimitsRes const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ScheduleReqEntry const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ScheduleResEntry const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ScheduleSetpointEntry const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ExternalLimits const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EnforcedLimits const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(CapabilityLimits const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(NodeType val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EvseState val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(OptimizerTarget const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EnergyFlowRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, NumberWithSource const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, IntegerWithSource const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, FrequencyWattPoint const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SetpointType const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, PricePerkWh const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, LimitsReq const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, LimitsRes const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ScheduleReqEntry const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ScheduleResEntry const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ScheduleSetpointEntry const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ExternalLimits const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EnforcedLimits const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CapabilityLimits const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, NodeType val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EvseState val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, OptimizerTarget const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EnergyFlowRequest const& val) {
    os << serialize(val);
    return os;
}

template <> NumberWithSource deserialize(std::string_view val) {
    return utilities::parse_json<NumberWithSource>(val);
}

template <> IntegerWithSource deserialize(std::string_view val) {
    return utilities::parse_json<IntegerWithSource>(val);
}

template <> FrequencyWattPoint deserialize(std::string_view val) {
    return utilities::parse_json<FrequencyWattPoint>(val);
}

template <> SetpointType deserialize(std::string_view val) {
    return utilities::parse_json<SetpointType>(val);
}

template <> PricePerkWh deserialize(std::string_view val) {
    return utilities::parse_json<PricePerkWh>(val);
}

template <> LimitsReq deserialize(std::string_view val) {
    return utilities::parse_json<LimitsReq>(val);
}

template <> LimitsRes deserialize(std::string_view val) {
    return utilities::parse_json<LimitsRes>(val);
}

template <> ScheduleReqEntry deserialize(std::string_view val) {
    return utilities::parse_json<ScheduleReqEntry>(val);
}

template <> ScheduleResEntry deserialize(std::string_view val) {
    return utilities::parse_json<ScheduleResEntry>(val);
}

template <> ScheduleSetpointEntry deserialize(std::string_view val) {
    return utilities::parse_json<ScheduleSetpointEntry>(val);
}

template <> ExternalLimits deserialize(std::string_view val) {
    return utilities::parse_json<ExternalLimits>(val);
}

template <> EnforcedLimits deserialize(std::string_view val) {
    return utilities::parse_json<EnforcedLimits>(val);
}

template <> CapabilityLimits deserialize(std::string_view val) {
    return utilities::parse_json<CapabilityLimits>(val);
}

template <> NodeType deserialize(std::string_view val) {
    return utilities::parse_json<NodeType>(val);
}

template <> EvseState deserialize(std::string_view val) {
    return utilities::parse_json<EvseState>(val);
}

template <> OptimizerTarget deserialize(std::string_view val) {
    return utilities::parse_json<OptimizerTarget>(val);
}

template <> EnergyFlowRequest deserialize(std::string_view val) {
    return utilities::parse_json<EnergyFlowRequest>(val);
}

} // namespace everest::lib::API::V1_0::types::energy
