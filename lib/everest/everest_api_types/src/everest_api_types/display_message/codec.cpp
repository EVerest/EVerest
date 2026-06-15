// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "display_message/codec.hpp"
#include "display_message/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"

namespace everest::lib::API::V1_0::types::display_message {

std::string serialize(MessagePriorityEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(MessageStateEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(DisplayMessageStatusEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ClearMessageResponseEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Identifier_type val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(DisplayMessage const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SetDisplayMessageRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SetDisplayMessageResponse const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(GetDisplayMessageRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(GetDisplayMessageResponse const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ClearDisplayMessageRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ClearDisplayMessageResponse const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, MessagePriorityEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, MessageStateEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, DisplayMessageStatusEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ClearMessageResponseEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Identifier_type const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, DisplayMessage const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SetDisplayMessageRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SetDisplayMessageResponse const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, GetDisplayMessageRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, GetDisplayMessageResponse const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ClearDisplayMessageRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ClearDisplayMessageResponse const& val) {
    os << serialize(val);
    return os;
}

template <> MessagePriorityEnum deserialize(std::string_view val) {
    return utilities::parse_json<MessagePriorityEnum>(val);
}

template <> MessageStateEnum deserialize(std::string_view val) {
    return utilities::parse_json<MessageStateEnum>(val);
}

template <> DisplayMessageStatusEnum deserialize(std::string_view val) {
    return utilities::parse_json<DisplayMessageStatusEnum>(val);
}

template <> ClearMessageResponseEnum deserialize(std::string_view val) {
    return utilities::parse_json<ClearMessageResponseEnum>(val);
}

template <> Identifier_type deserialize(std::string_view val) {
    return utilities::parse_json<Identifier_type>(val);
}

template <> DisplayMessage deserialize(std::string_view val) {
    return utilities::parse_json<DisplayMessage>(val);
}

template <> SetDisplayMessageRequest deserialize(std::string_view val) {
    return utilities::parse_json<SetDisplayMessageRequest>(val);
}

template <> SetDisplayMessageResponse deserialize(std::string_view val) {
    return utilities::parse_json<SetDisplayMessageResponse>(val);
}

template <> GetDisplayMessageRequest deserialize(std::string_view val) {
    return utilities::parse_json<GetDisplayMessageRequest>(val);
}

template <> GetDisplayMessageResponse deserialize(std::string_view val) {
    return utilities::parse_json<GetDisplayMessageResponse>(val);
}

template <> ClearDisplayMessageRequest deserialize(std::string_view val) {
    return utilities::parse_json<ClearDisplayMessageRequest>(val);
}

template <> ClearDisplayMessageResponse deserialize(std::string_view val) {
    return utilities::parse_json<ClearDisplayMessageResponse>(val);
}

} // namespace everest::lib::API::V1_0::types::display_message
