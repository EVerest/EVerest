// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <Connector.hpp>

namespace module {

void to_json(json& j, const Identifier& k) {
    // the required parts of the type
    j = json{
        {"id_token", k.id_token},
        {"type", types::authorization::authorization_type_to_string_view(k.type)},
    };
    // the optional parts of the type
    if (k.authorization_status.has_value()) {
        j["authorization_status"] =
            types::authorization::authorization_status_to_string_view(k.authorization_status.value());
    }
    if (k.expiry_time.has_value()) {
        j["expiry_time"] = k.expiry_time.value();
    }
    if (k.parent_id_token.has_value()) {
        j["parent_id_token"] = k.parent_id_token.value();
    }
}

void from_json(const json& j, Identifier& k) {
    // the required parts of the type
    k.id_token = j.at("id_token").get<types::authorization::IdToken>();
    k.type = types::authorization::string_to_authorization_type(j.at("type"));

    // the optional parts of the type
    k.authorization_status.reset();
    k.expiry_time.reset();
    k.parent_id_token.reset();
    if (const auto it = j.find("authorization_status"); it != j.end() and not it->is_null()) {
        k.authorization_status = types::authorization::string_to_authorization_status(*it);
    }
    if (const auto it = j.find("expiry_time"); it != j.end() and not it->is_null()) {
        k.expiry_time = it->get<std::string>();
    }
    if (const auto it = j.find("parent_id_token"); it != j.end() and not it->is_null()) {
        k.parent_id_token = it->get<types::authorization::IdToken>();
    }
}

void Connector::submit_event(ConnectorEvent event) {
    state_machine.handle_event(event);
}

ConnectorState Connector::get_state() const {
    return this->state_machine.get_state();
}

bool Connector::is_unavailable() const {
    return this->get_state() == ConnectorState::UNAVAILABLE || this->get_state() == ConnectorState::UNAVAILABLE_FAULTED;
}

namespace conversions {
std::string connector_state_to_string(const ConnectorState& state) {
    switch (state) {
    case ConnectorState::AVAILABLE:
        return "AVAILABLE";
    case ConnectorState::OCCUPIED:
        return "OCCUPIED";
    case ConnectorState::UNAVAILABLE:
        return "UNAVAILABLE";
    case ConnectorState::FAULTED:
        return "FAULTED";
    case ConnectorState::FAULTED_OCCUPIED:
        return "FAULTED_OCCUPIED";
    case ConnectorState::UNAVAILABLE_FAULTED:
        return "UNAVAILABLE_FAULTED";
    default:
        throw std::runtime_error("No known conversion for the given connector state");
    }
}

} // namespace conversions

bool EVSEContext::is_available() {
    if (this->plug_in_timeout) {
        return false;
    }

    // if an identifier is present, an EVSE is not considered available
    if (this->identifier.has_value()) {
        return false;
    }

    bool occupied = false;
    bool available = false;
    for (const auto& connector : this->connectors) {
        if (connector.get_state() == ConnectorState::OCCUPIED ||
            connector.get_state() == ConnectorState::FAULTED_OCCUPIED) {
            occupied = true;
        }
        if (connector.get_state() != ConnectorState::UNAVAILABLE && connector.get_state() != ConnectorState::FAULTED) {
            available = true;
        }
    }

    if (occupied) {
        // When at least one connector is occupied, they are both not available.
        return false;
    }

    return available;
}

bool EVSEContext::is_unavailable() {
    for (const auto& connector : this->connectors) {
        if (!connector.is_unavailable()) {
            return false;
        }
    }

    return true;
}

// namespace conversions

// namespace conversions
} // namespace module
