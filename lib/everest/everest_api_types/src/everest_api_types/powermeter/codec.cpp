// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "powermeter/codec.hpp"
#include "nlohmann/json.hpp"
#include "powermeter/API.hpp"
#include "powermeter/json_codec.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::powermeter {

std::string serialize(OCMFUserIdentificationStatus val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(OCMFIdentificationFlags val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(OCMFIdentificationType val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(OCMFIdentificationLevel val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Current const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Voltage const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Frequency const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Power const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Energy const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ReactivePower const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SignedMeterValue const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SignedCurrent const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SignedVoltage const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SignedFrequency const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SignedPower const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SignedEnergy const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SignedReactivePower const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(PowermeterValues const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Temperature const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(TransactionStatus val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ReplyStartTransaction const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ReplyStopTransaction const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(RequestStartTransaction const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Capabilities const& val) noexcept {
    return utilities::dump_json(val);
}

template <> OCMFUserIdentificationStatus deserialize(std::string_view val) {
    return utilities::parse_json<OCMFUserIdentificationStatus>(val);
}

template <> OCMFIdentificationFlags deserialize(std::string_view val) {
    return utilities::parse_json<OCMFIdentificationFlags>(val);
}

template <> OCMFIdentificationType deserialize(std::string_view val) {
    return utilities::parse_json<OCMFIdentificationType>(val);
}

template <> OCMFIdentificationLevel deserialize(std::string_view val) {
    return utilities::parse_json<OCMFIdentificationLevel>(val);
}

template <> Current deserialize(std::string_view val) {
    return utilities::parse_json<Current>(val);
}

template <> Voltage deserialize(std::string_view val) {
    return utilities::parse_json<Voltage>(val);
}

template <> Frequency deserialize(std::string_view val) {
    return utilities::parse_json<Frequency>(val);
}

template <> Power deserialize(std::string_view val) {
    return utilities::parse_json<Power>(val);
}

template <> Energy deserialize(std::string_view val) {
    return utilities::parse_json<Energy>(val);
}

template <> ReactivePower deserialize(std::string_view val) {
    return utilities::parse_json<ReactivePower>(val);
}

template <> SignedMeterValue deserialize(std::string_view val) {
    return utilities::parse_json<SignedMeterValue>(val);
}

template <> SignedCurrent deserialize(std::string_view val) {
    return utilities::parse_json<SignedCurrent>(val);
}

template <> SignedVoltage deserialize(std::string_view val) {
    return utilities::parse_json<SignedVoltage>(val);
}

template <> SignedFrequency deserialize(std::string_view val) {
    return utilities::parse_json<SignedFrequency>(val);
}

template <> SignedPower deserialize(std::string_view val) {
    return utilities::parse_json<SignedPower>(val);
}

template <> SignedEnergy deserialize(std::string_view val) {
    return utilities::parse_json<SignedEnergy>(val);
}

template <> SignedReactivePower deserialize(std::string_view val) {
    return utilities::parse_json<SignedReactivePower>(val);
}

template <> Temperature deserialize(std::string_view val) {
    return utilities::parse_json<Temperature>(val);
}

template <> PowermeterValues deserialize(std::string_view val) {
    return utilities::parse_json<PowermeterValues>(val);
}

template <> TransactionStatus deserialize(std::string_view val) {
    return utilities::parse_json<TransactionStatus>(val);
}

template <> ReplyStartTransaction deserialize(std::string_view val) {
    return utilities::parse_json<ReplyStartTransaction>(val);
}

template <> ReplyStopTransaction deserialize(std::string_view val) {
    return utilities::parse_json<ReplyStopTransaction>(val);
}

template <> RequestStartTransaction deserialize(std::string_view val) {
    return utilities::parse_json<RequestStartTransaction>(val);
}

template <> Capabilities deserialize(std::string_view val) {
    return utilities::parse_json<Capabilities>(val);
}

std::ostream& operator<<(std::ostream& os, OCMFUserIdentificationStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, OCMFIdentificationFlags const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, OCMFIdentificationType const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, OCMFIdentificationLevel const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Current const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Voltage const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Frequency const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Power const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Energy const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ReactivePower const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SignedMeterValue const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SignedCurrent const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SignedVoltage const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SignedFrequency const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SignedPower const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SignedEnergy const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SignedReactivePower const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Temperature const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, PowermeterValues const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, TransactionStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ReplyStartTransaction const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ReplyStopTransaction const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, RequestStartTransaction const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Capabilities const& val) {
    os << serialize(val);
    return os;
}

} // namespace everest::lib::API::V1_0::types::powermeter
