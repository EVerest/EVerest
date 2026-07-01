// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "telemetry/codec.hpp"
#include "telemetry/API.hpp"
#include "telemetry/json_codec.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <ostream>
#include <string_view>

namespace everest::lib::API::V1_0::types::telemetry {

std::string serialize(ChargeProgress val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gDin70121CommunicationState val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gIso15118AcCommunicationState val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gIso15118DcCommunicationState val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gCommunicationState val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gMessageState val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gServerStatus val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gEvErrorCode val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(CertChainState const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(CertTelemetry const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EvseControlStatus const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gTransport const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gEvElectrical const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gPaymentService const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gChargerStatus const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(V2gEvseElectrical const& val) noexcept {
    return utilities::dump_json(val);
}

template <> ChargeProgress deserialize(std::string_view val) {
    return utilities::parse_json<ChargeProgress>(val);
}

template <> V2gCommunicationState deserialize(std::string_view val) {
    return utilities::parse_json<V2gCommunicationState>(val);
}

template <> V2gDin70121CommunicationState deserialize(std::string_view val) {
    return utilities::parse_json<V2gDin70121CommunicationState>(val);
}

template <> V2gIso15118AcCommunicationState deserialize(std::string_view val) {
    return utilities::parse_json<V2gIso15118AcCommunicationState>(val);
}

template <> V2gIso15118DcCommunicationState deserialize(std::string_view val) {
    return utilities::parse_json<V2gIso15118DcCommunicationState>(val);
}

template <> V2gMessageState deserialize(std::string_view val) {
    return utilities::parse_json<V2gMessageState>(val);
}

template <> V2gServerStatus deserialize(std::string_view val) {
    return utilities::parse_json<V2gServerStatus>(val);
}

template <> V2gEvErrorCode deserialize(std::string_view val) {
    return utilities::parse_json<V2gEvErrorCode>(val);
}

template <> CertChainState deserialize(std::string_view val) {
    return utilities::parse_json<CertChainState>(val);
}

template <> CertTelemetry deserialize(std::string_view val) {
    return utilities::parse_json<CertTelemetry>(val);
}

template <> EvseControlStatus deserialize(std::string_view val) {
    return utilities::parse_json<EvseControlStatus>(val);
}

template <> V2gTransport deserialize(std::string_view val) {
    return utilities::parse_json<V2gTransport>(val);
}

template <> V2gEvElectrical deserialize(std::string_view val) {
    return utilities::parse_json<V2gEvElectrical>(val);
}

template <> V2gPaymentService deserialize(std::string_view val) {
    return utilities::parse_json<V2gPaymentService>(val);
}

template <> V2gChargerStatus deserialize(std::string_view val) {
    return utilities::parse_json<V2gChargerStatus>(val);
}

template <> V2gEvseElectrical deserialize(std::string_view val) {
    return utilities::parse_json<V2gEvseElectrical>(val);
}

std::ostream& operator<<(std::ostream& os, ChargeProgress const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gCommunicationState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gDin70121CommunicationState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gIso15118AcCommunicationState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gIso15118DcCommunicationState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gMessageState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gServerStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gEvErrorCode const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CertChainState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CertTelemetry const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EvseControlStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gTransport const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gEvElectrical const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gPaymentService const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gChargerStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, V2gEvseElectrical const& val) {
    os << serialize(val);
    return os;
}

} // namespace everest::lib::API::V1_0::types::telemetry
