// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2024 Pionix GmbH and Contributors to EVerest

#include <ocpp/common/connectivity_manager.hpp>

#include <fstream>

#include <everest/logging.hpp>

namespace {
const auto WEBSOCKET_INIT_DELAY = std::chrono::seconds(2);
const std::string VARIABLE_ATTRIBUTE_VALUE_SOURCE_INTERNAL = "internal";
/// \brief Default timeout for the return value (future) of the `configure_network_connection_profile_callback`
///        function.
constexpr std::int32_t default_network_config_timeout_seconds = 60;
} // namespace

namespace ocpp {

using NetworkConnectionProfile = ocpp::v2::NetworkConnectionProfile;
using OCPPInterfaceEnum = ocpp::v2::OCPPInterfaceEnum;
using SetNetworkProfileRequest = ocpp::v2::SetNetworkProfileRequest;

ConnectivityManager::ConnectivityManager(ocpp::ConnectivityManagerConfiguration& configuration,
                                         std::shared_ptr<EvseSecurity> evse_security, const fs::path& share_path) :
    configuration{configuration},
    evse_security{std::move(evse_security)},
    share_path{share_path},
    websocket{nullptr},
    message_callback([](const std::string& message) {
        EVLOG_warning << "No message callback set in ConnectivityManager. Dropping message: " << message;
    }),
    wants_to_be_connected{false},
    connected_ocpp_version{OcppProtocolVersion::Unknown} {
    cache_network_connection_profiles();
}

void ConnectivityManager::set_message_callback(const std::function<void(const std::string& message)>& callback) {
    this->message_callback = callback;
}

void ConnectivityManager::set_logging(std::shared_ptr<MessageLogging> logging) {
    this->logging = logging;
}

void ConnectivityManager::set_websocket_authorization_key(const std::string& authorization_key) {
    if (this->websocket != nullptr) {
        this->websocket->set_authorization_key(authorization_key);
        this->websocket->disconnect(WebsocketCloseReason::ServiceRestart);
    }
}

void ConnectivityManager::set_websocket_connection_options(const WebsocketConnectionOptions& connection_options) {
    if (this->websocket != nullptr) {
        this->websocket->set_connection_options(connection_options);
    }
}

void ConnectivityManager::set_websocket_connection_options_without_reconnect() {
    const auto configuration_slot = get_active_network_configuration_slot();
    if (!configuration_slot.has_value()) {
        return;
    }
    const auto connection_options = this->get_ws_connection_options(configuration_slot.value());
    if (connection_options.has_value()) {
        this->set_websocket_connection_options(connection_options.value());
    }
}

void ConnectivityManager::set_websocket_ping_interval(std::int32_t ping_interval_s, std::int32_t pong_timeout_s) {
    if (this->websocket != nullptr && this->websocket->is_connected()) {
        this->websocket->set_websocket_ping_interval(ping_interval_s, pong_timeout_s);
    }
}

void ConnectivityManager::set_websocket_connected_callback(WebsocketConnectionCallback callback) {
    this->websocket_connected_callback = callback;
}

void ConnectivityManager::set_websocket_disconnected_callback(WebsocketConnectionCallback callback) {
    this->websocket_disconnected_callback = callback;
}

void ConnectivityManager::set_websocket_connection_failed_callback(WebsocketConnectionFailedCallback callback) {
    this->websocket_connection_failed_callback = callback;
}

void ConnectivityManager::set_configure_network_connection_profile_callback(
    ConfigureNetworkConnectionProfileCallback callback) {
    this->configure_network_connection_profile_callback = callback;
}

std::optional<NetworkConnectionProfile>
ConnectivityManager::get_network_connection_profile(const std::int32_t configuration_slot) const {
    auto state = this->m_state.handle();
    for (const auto& network_profile : state->cached_profiles) {
        if (network_profile.configurationSlot == configuration_slot) {
            if (!this->configuration.get_allow_security_level_zero_connections() &&
                network_profile.connectionData.securityProfile ==
                    security::OCPP_1_6_ONLY_UNSECURED_TRANSPORT_WITHOUT_BASIC_AUTHENTICATION) {
                EVLOG_error << "security_profile 0 not officially allowed in OCPP 2.0.1, skipping profile";
                return std::nullopt;
            }

            return network_profile.connectionData;
        }
    }
    return std::nullopt;
}

std::optional<std::int32_t>
ConnectivityManager::get_priority_from_configuration_slot(const int configuration_slot) const {
    auto state = this->m_state.handle();
    auto it = std::find(state->slots.begin(), state->slots.end(), configuration_slot);
    if (it != state->slots.end()) {
        // Index is iterator - begin iterator
        return it - state->slots.begin();
    }
    return std::nullopt;
}

std::optional<int> ConnectivityManager::get_active_network_configuration_slot() const {
    auto state = this->m_state.handle();
    if (state->slots.empty()) {
        return std::nullopt;
    }
    const auto idx = static_cast<std::size_t>(std::max<std::int32_t>(state->active_priority, 0));
    if (idx >= state->slots.size()) {
        return std::nullopt;
    }
    return state->slots[idx];
}

std::optional<int> ConnectivityManager::get_configuration_slot_from_priority(const int priority) {
    auto state = this->m_state.handle();
    if (priority < 0 || static_cast<std::size_t>(priority) >= state->slots.size()) {
        return std::nullopt;
    }
    return state->slots[static_cast<std::size_t>(priority)];
}

std::vector<int> ConnectivityManager::get_network_connection_slots() const {
    auto state = this->m_state.handle();
    return state->slots;
}

bool ConnectivityManager::is_websocket_connected() {
    return this->websocket != nullptr && this->websocket->is_connected();
}

std::chrono::time_point<std::chrono::steady_clock> ConnectivityManager::get_time_disconnected() const {
    return this->time_disconnected;
}

void ConnectivityManager::connect(std::optional<std::int32_t> network_profile_slot) {
    std::int32_t configuration_slot{};
    {
        auto state = this->m_state.handle();
        if (state->slots.empty()) {
            EVLOG_warning << "No network connection profiles configured, aborting websocket connection.";
            return;
        }
        if (network_profile_slot.has_value()) {
            configuration_slot = network_profile_slot.value();
        } else {
            const auto idx = static_cast<std::size_t>(std::max<std::int32_t>(state->active_priority, 0));
            if (idx >= state->slots.size()) {
                EVLOG_warning << "Active priority index out of range, aborting websocket connection.";
                return;
            }
            configuration_slot = state->slots[idx];
        }
        state->pending_configuration_slot = configuration_slot;
    }

    if (!this->get_network_connection_profile(configuration_slot).has_value()) {
        EVLOG_warning << "Could not find network connection profile belonging to configuration slot "
                      << configuration_slot;
        return;
    }

    this->wants_to_be_connected = true;
    if (this->websocket != nullptr && this->is_websocket_connected()) {
        // After the websocket gets closed a reconnect will be triggered
        this->websocket->disconnect(WebsocketCloseReason::ServiceRestart);
    } else {
        this->try_connect_websocket();
    }
}

void ConnectivityManager::disconnect() {
    this->wants_to_be_connected = false;
    this->websocket_timer.stop();
    if (this->websocket != nullptr) {
        this->websocket->disconnect(WebsocketCloseReason::Normal);
    }
}

void ConnectivityManager::confirm_successful_connection() {
    const auto config_slot = this->get_active_network_configuration_slot();
    if (!config_slot.has_value()) {
        EVLOG_warning << "confirm_successful_connection: no active configuration slot";
        return;
    }

    const auto network_connection_profile = this->get_network_connection_profile(config_slot.value());

    if (network_connection_profile.has_value()) {
        this->configuration.set_active_security_profile(network_connection_profile.value().securityProfile,
                                                        VARIABLE_ATTRIBUTE_VALUE_SOURCE_INTERNAL);
    }

    this->remove_network_connection_profiles_below_actual_security_profile();
    this->check_cache_for_invalid_security_profiles();
}

void ConnectivityManager::try_connect_websocket() {
    // Check the cache runtime since security profile might change async
    this->check_cache_for_invalid_security_profiles();

    std::int32_t configuration_slot_to_set{};
    {
        auto state = this->m_state.handle();
        if (state->pending_configuration_slot.has_value()) {
            configuration_slot_to_set = state->pending_configuration_slot.value();
        } else if (!state->slots.empty()) {
            const auto idx = static_cast<std::size_t>(std::max<std::int32_t>(state->active_priority, 0));
            if (idx >= state->slots.size()) {
                EVLOG_warning << "try_connect_websocket: active_priority index out of range";
                return;
            }
            configuration_slot_to_set = state->slots[idx];
        } else {
            EVLOG_warning << "try_connect_websocket: no configuration slots available";
            return;
        }
    }

    const std::optional<int> priority_to_set = this->get_priority_from_configuration_slot(configuration_slot_to_set);
    const auto network_connection_profile = this->get_network_connection_profile(configuration_slot_to_set);
    // Not const as the iface member can be set by the configure network connection profile callback
    auto connection_options = this->get_ws_connection_options(configuration_slot_to_set);
    bool can_use_connection_profile = true;

    if (!network_connection_profile.has_value() || !priority_to_set.has_value()) {
        EVLOG_warning << "No network connection profile configured for " << configuration_slot_to_set;
        can_use_connection_profile = false;
    } else if (!connection_options.has_value()) {
        EVLOG_warning << "Connection profile configured for " << configuration_slot_to_set << " failed: not valid URL";
        can_use_connection_profile = false;
    } else if (this->configure_network_connection_profile_callback.has_value()) {
        std::optional<ConfigNetworkResult> config = handle_configure_network_connection_profile_callback(
            configuration_slot_to_set, network_connection_profile.value());
        if (config.has_value() && config->success) {
            connection_options->iface = config->interface_address;
        } else {
            EVLOG_debug << "Could not use config slot " << configuration_slot_to_set;
            can_use_connection_profile = false;
        }
    }

    if (!can_use_connection_profile) {
        if (this->wants_to_be_connected) {
            this->websocket_timer.timeout(
                [this, configuration_slot_to_set] {
                    const auto next_slot = this->get_next_configuration_slot(configuration_slot_to_set);
                    if (next_slot.has_value()) {
                        auto state = this->m_state.handle();
                        state->pending_configuration_slot = next_slot.value();
                    }
                    this->try_connect_websocket();
                },
                WEBSOCKET_INIT_DELAY);
        }
        return;
    }

    {
        auto state = this->m_state.handle();
        state->pending_configuration_slot.reset();
        state->active_priority = priority_to_set.value();
    }

    if (connection_options->security_profile ==
        security::OCPP_1_6_ONLY_UNSECURED_TRANSPORT_WITHOUT_BASIC_AUTHENTICATION) {
        EVLOG_warning << "Using insecure security profile 0 without authentication";
    }

    // priority_to_set is the 0-indexed position into NetworkConfigurationPriority; log it as 1-indexed
    // for human-readable parity with how the priority list is described in the OCPP spec and configs.
    EVLOG_info << "Open websocket with NetworkConfigurationPriority: " << priority_to_set.value() + 1
               << " which is configurationSlot " << configuration_slot_to_set;

    this->configuration.set_active_network_profile_slot(configuration_slot_to_set,
                                                        VARIABLE_ATTRIBUTE_VALUE_SOURCE_INTERNAL);

    if (this->websocket == nullptr) {
        this->websocket = std::make_unique<Websocket>(connection_options.value(), this->evse_security, this->logging);

        this->websocket->register_connected_callback(
            [this](OcppProtocolVersion protocol) { this->on_websocket_connected(protocol); });
        this->websocket->register_disconnected_callback([this]() { this->on_websocket_disconnected(); });
        this->websocket->register_stopped_connecting_callback(
            [this](ocpp::WebsocketCloseReason reason) { this->on_websocket_stopped_connecting(reason); });
    } else {
        this->websocket->set_connection_options(connection_options.value());
    }

    // Attach external callbacks everytime since they might have changed
    if (websocket_connection_failed_callback.has_value()) {
        this->websocket->register_connection_failed_callback(websocket_connection_failed_callback.value());
    }

    this->websocket->register_message_callback([this](const std::string& message) { this->message_callback(message); });

    this->websocket->start_connecting();
}

std::optional<ConfigNetworkResult>
ConnectivityManager::handle_configure_network_connection_profile_callback(int slot,
                                                                          const NetworkConnectionProfile& profile) {
    if (!this->configure_network_connection_profile_callback.has_value()) {
        return std::nullopt;
    }

    std::future<ConfigNetworkResult> config_status =
        this->configure_network_connection_profile_callback.value()(slot, profile);
    const std::int32_t config_timeout =
        this->configuration.get_network_config_timeout().value_or(default_network_config_timeout_seconds);

    if (config_status.wait_for(std::chrono::seconds(config_timeout)) == std::future_status::ready) {
        return config_status.get();
    }

    EVLOG_warning << "Timeout configuring config slot: " << slot;
    return std::nullopt;
}

std::optional<int> ConnectivityManager::get_next_configuration_slot(std::int32_t configuration_slot) {
    auto state = this->m_state.handle();
    if (state->slots.empty()) {
        return std::nullopt;
    }
    if (state->slots.size() > 1) {
        EVLOG_info << "Switching to next network configuration priority";
    }
    const auto it = std::find(state->slots.begin(), state->slots.end(), configuration_slot);
    std::int32_t next_priority = 0;
    if (it != state->slots.end()) {
        const auto current_priority = static_cast<std::int32_t>(it - state->slots.begin());
        next_priority = (current_priority + 1) % clamp_to<std::int32_t>(state->slots.size());
    }
    return state->slots[static_cast<std::size_t>(next_priority)];
}

bool ConnectivityManager::send_to_websocket(const std::string& message) {
    if (this->websocket == nullptr) {
        return false;
    }

    return this->websocket->send(message);
}

void ConnectivityManager::on_network_disconnected(OCPPInterfaceEnum ocpp_interface) {
    const auto actual_configuration_slot = get_active_network_configuration_slot();
    if (!actual_configuration_slot.has_value()) {
        EVLOG_warning << "Network disconnected. No active configuration slot";
        return;
    }
    std::optional<NetworkConnectionProfile> network_connection_profile =
        this->get_network_connection_profile(actual_configuration_slot.value());

    if (!network_connection_profile.has_value()) {
        EVLOG_warning << "Network disconnected. No network connection profile configured";
    } else if (ocpp_interface == network_connection_profile.value().ocppInterface && this->websocket != nullptr) {
        // Since there is no connection anymore: disconnect the websocket, the manager will try to connect with the next
        // available network connection profile as we enable reconnects.
        this->websocket->disconnect(ocpp::WebsocketCloseReason::GoingAway);
    }
}

void ConnectivityManager::on_charging_station_certificate_changed() {
    if (this->websocket != nullptr) {
        // After the websocket gets closed a reconnect will be triggered
        this->websocket->disconnect(WebsocketCloseReason::ServiceRestart);
    }
}

std::optional<std::string> ConnectivityManager::read_everest_version() const {
    const fs::path version_file_path = this->share_path.parent_path().parent_path() / "version_information.txt";
    if (!fs::exists(version_file_path)) {
        return std::nullopt;
    }
    std::ifstream ifs(version_file_path);
    std::string version;
    std::getline(ifs, version);
    std::string trimmed_version = ocpp::trim_string(version);
    trimmed_version.erase(std::remove(trimmed_version.begin(), trimmed_version.end(), '\n'), trimmed_version.end());
    if (trimmed_version.empty()) {
        return std::nullopt;
    }
    return trimmed_version;
}

std::optional<WebsocketConnectionOptions>
ConnectivityManager::get_ws_connection_options(const std::int32_t configuration_slot) {
    auto opts = this->configuration.get_websocket_connection_options(configuration_slot);
    if (opts.has_value()) {
        if (auto version = this->read_everest_version(); version.has_value()) {
            opts->everest_version = std::move(*version);
        }
    }
    return opts;
}

void ConnectivityManager::on_websocket_connected(OcppProtocolVersion protocol) {
    this->connected_ocpp_version = protocol;
    const auto actual_configuration_slot = get_active_network_configuration_slot();
    if (!actual_configuration_slot.has_value()) {
        EVLOG_warning << "on_websocket_connected: no active configuration slot";
        return;
    }
    std::optional<NetworkConnectionProfile> network_connection_profile =
        this->get_network_connection_profile(actual_configuration_slot.value());

    // Write negotiated OcppVersion to the per-slot NetworkConfiguration DM variable (B09)
    if (protocol != OcppProtocolVersion::Unknown) {
        std::string version_str;
        switch (protocol) {
        case OcppProtocolVersion::v201:
            version_str = "OCPP201";
            break;
        case OcppProtocolVersion::v21:
            version_str = "OCPP21";
            break;
        case OcppProtocolVersion::v16:
        case OcppProtocolVersion::Unknown:
            break;
        }
        if (!version_str.empty()) {
            this->configuration.set_per_slot_ocpp_version(actual_configuration_slot.value(), version_str,
                                                          VARIABLE_ATTRIBUTE_VALUE_SOURCE_INTERNAL);
        }
    }
    if (network_connection_profile.has_value()) {
        this->configuration.set_security_ctrl_security_profile(network_connection_profile->securityProfile,
                                                               VARIABLE_ATTRIBUTE_VALUE_SOURCE_INTERNAL);
        if (network_connection_profile->identity.has_value()) {
            this->configuration.set_security_ctrl_identity(network_connection_profile->identity.value(),
                                                           VARIABLE_ATTRIBUTE_VALUE_SOURCE_INTERNAL);
        }
    }

    if (this->websocket_connected_callback.has_value() and network_connection_profile.has_value()) {
        this->websocket_connected_callback.value()(actual_configuration_slot.value(),
                                                   network_connection_profile.value(), this->connected_ocpp_version);
    }
    this->time_disconnected = std::chrono::time_point<std::chrono::steady_clock>();
}

void ConnectivityManager::mark_disconnected_at_now() {
    auto expected = std::chrono::time_point<std::chrono::steady_clock>{};
    this->time_disconnected.compare_exchange_strong(expected, std::chrono::steady_clock::now());
}

void ConnectivityManager::on_websocket_disconnected() {
    const auto actual_configuration_slot = get_active_network_configuration_slot();
    if (!actual_configuration_slot.has_value()) {
        this->mark_disconnected_at_now();
        return;
    }
    std::optional<NetworkConnectionProfile> network_connection_profile =
        this->get_network_connection_profile(actual_configuration_slot.value());
    this->mark_disconnected_at_now();

    if (this->websocket_disconnected_callback.has_value() and network_connection_profile.has_value()) {
        this->websocket_disconnected_callback.value()(actual_configuration_slot.value(),
                                                      network_connection_profile.value(), this->connected_ocpp_version);
    }
}

void ConnectivityManager::on_websocket_stopped_connecting(ocpp::WebsocketCloseReason reason) {
    const auto active_slot = this->get_active_network_configuration_slot();
    if (active_slot.has_value()) {
        auto state = this->m_state.handle();
        EVLOG_warning << "Closed websocket of NetworkConfigurationPriority: " << state->active_priority + 1
                      << " which is configurationSlot " << active_slot.value();
    } else {
        EVLOG_warning << "Closed websocket (no active configuration slot)";
    }

    if (this->wants_to_be_connected) {
        this->websocket_timer.timeout(
            [this, reason] {
                if (reason != WebsocketCloseReason::ServiceRestart) {
                    const auto current = get_active_network_configuration_slot();
                    if (current.has_value()) {
                        const auto next_slot = get_next_configuration_slot(current.value());
                        if (next_slot.has_value()) {
                            auto state = this->m_state.handle();
                            state->pending_configuration_slot = next_slot.value();
                        }
                    }
                }
                this->try_connect_websocket();
            },
            WEBSOCKET_INIT_DELAY);
    }
}

void ConnectivityManager::append_slot_to_network_configuration_priority_if_absent(const int32_t slot,
                                                                                  const std::string& source) {
    const auto priority_str = this->configuration.get_network_configuration_priority();
    const auto slot_str = std::to_string(slot);
    bool slot_found = false;
    for (const auto& s : ocpp::split_string(priority_str, ',')) {
        if (s == slot_str) {
            slot_found = true;
            break;
        }
    }
    if (!slot_found) {
        std::string new_priority = priority_str;
        if (!new_priority.empty()) {
            new_priority += ',';
        }
        new_priority += slot_str;
        this->configuration.set_network_configuration_priority(new_priority, source);
    }
}

void ConnectivityManager::cache_network_connection_profiles() {
    auto state = this->m_state.handle();
    state->cached_profiles.clear();
    state->slots.clear();

    // Build profiles and priority-ordered slot list from NetworkConfiguration DM components
    for (const std::string& str : ocpp::split_string(this->configuration.get_network_configuration_priority(), ',')) {
        int slot = 0;
        try {
            slot = std::stoi(str);
        } catch (const std::exception& e) {
            EVLOG_warning << "Skipping non-integer token '" << str << "' in NetworkConfigurationPriority: " << e.what();
            continue;
        }
        state->slots.push_back(slot);
        if (const auto profile = this->configuration.read_network_connection_profile(slot)) {
            SetNetworkProfileRequest req;
            req.configurationSlot = slot;
            req.connectionData = *profile;
            state->cached_profiles.push_back(req);
        }
    }

    // Re-clamp active_priority to remain a valid index after rebuild
    if (state->slots.empty()) {
        state->active_priority = 0;
    } else if (static_cast<std::size_t>(state->active_priority) >= state->slots.size()) {
        state->active_priority = static_cast<std::int32_t>(state->slots.size() - 1);
    }

    this->warn_if_all_security_level_zero_locked(*state);
}

void ConnectivityManager::warn_if_all_security_level_zero_locked(const NetworkProfileCacheState& state) const {
    if (state.cached_profiles.empty()) {
        return;
    }
    if (this->configuration.get_allow_security_level_zero_connections()) {
        return;
    }
    if (std::none_of(state.cached_profiles.begin(), state.cached_profiles.end(),
                     [](const SetNetworkProfileRequest& profile) {
                         return profile.connectionData.securityProfile !=
                                security::OCPP_1_6_ONLY_UNSECURED_TRANSPORT_WITHOUT_BASIC_AUTHENTICATION;
                     })) {
        EVLOG_error << "All profiles configured have security_profile 0 which is not officially allowed in OCPP 2.0.1";
    }
}

void ConnectivityManager::reload_network_profiles() {
    try {
        cache_network_connection_profiles();
    } catch (const std::exception& e) {
        EVLOG_error << "Failed to reload network profiles: " << e.what();
    }
}

bool ConnectivityManager::set_network_profile(const int32_t slot, const NetworkConnectionProfile& profile,
                                              const std::string& source) {
    // B09.FR.18: each slot's per-slot Identity / BasicAuthPassword is independent. Do not inherit
    // from the currently-active slot — when the incoming profile omits a per-slot value, reads on
    // the new slot fall back to SecurityCtrlr globals per B09.FR.16.
    if (!this->configuration.write_network_connection_profile(slot, profile, source)) {
        return false;
    }

    this->append_slot_to_network_configuration_priority_if_absent(slot, source);

    try {
        cache_network_connection_profiles();
    } catch (const std::exception& e) {
        EVLOG_error << "Failed to refresh network profiles after set_network_profile: " << e.what();
    }
    return true;
}

void ConnectivityManager::check_cache_for_invalid_security_profiles() {
    const auto security_level = this->configuration.get_security_profile();

    // Single critical section over read-modify-write of state->slots and state->pending_configuration_slot.
    // Splitting the lock between the prune step and the pending-slot reassignment can let another writer
    // mutate state->slots in between, which would invalidate the locally captured before_slot.
    auto state = this->m_state.handle();
    if (state->last_known_security_level == security_level) {
        return;
    }
    state->last_known_security_level = security_level;

    std::optional<int> before_slot;
    if (state->pending_configuration_slot.has_value()) {
        before_slot = state->pending_configuration_slot;
    } else if (!state->slots.empty()) {
        const auto idx = static_cast<std::size_t>(std::max<std::int32_t>(state->active_priority, 0));
        if (idx < state->slots.size()) {
            before_slot = state->slots[idx];
        }
    }

    auto is_lower_security_level = [this, security_level](const int slot) {
        const auto opt_profile = this->get_network_connection_profile(slot);
        return !opt_profile.has_value() || opt_profile->securityProfile < security_level;
    };

    state->slots.erase(std::remove_if(state->slots.begin(), state->slots.end(), is_lower_security_level),
                       state->slots.end());

    // Mirror the prune onto cached_profiles so get_network_connection_profile() does not
    // return stale data for slots that were just removed from the priority list.
    state->cached_profiles.erase(std::remove_if(state->cached_profiles.begin(), state->cached_profiles.end(),
                                                [security_level](const SetNetworkProfileRequest& p) {
                                                    return p.connectionData.securityProfile < security_level;
                                                }),
                                 state->cached_profiles.end());

    // Cap active_priority after shrink so later reads don't index past the end
    if (state->slots.empty()) {
        state->active_priority = 0;
        state->pending_configuration_slot.reset();
        EVLOG_warning << "All network connection slots were removed due to insufficient security profile";
        return;
    }
    if (static_cast<std::size_t>(state->active_priority) >= state->slots.size()) {
        state->active_priority = static_cast<std::int32_t>(state->slots.size() - 1);
    }

    if (!before_slot.has_value()) {
        return;
    }
    // Use the active slot and if not valid any longer use the next available one. The recursive
    // mutex allows the helper calls below to re-acquire the same handle.
    const auto opt_priority = this->get_priority_from_configuration_slot(before_slot.value());
    if (opt_priority.has_value()) {
        state->pending_configuration_slot = before_slot.value();
    } else {
        const auto next = this->get_next_configuration_slot(before_slot.value());
        if (next.has_value()) {
            state->pending_configuration_slot = next.value();
        } else {
            state->pending_configuration_slot.reset();
        }
    }
}

void ConnectivityManager::remove_network_connection_profiles_below_actual_security_profile() {
    const auto security_level = this->configuration.get_security_profile();

    std::vector<int32_t> pruned_slots;
    {
        auto state = this->m_state.handle();
        // Collect slots to prune from the in-memory cache
        for (const auto& profile : state->cached_profiles) {
            if (profile.connectionData.securityProfile < security_level) {
                pruned_slots.push_back(profile.configurationSlot);
            }
        }

        if (pruned_slots.empty()) {
            return;
        }

        // Remove pruned slots from in-memory caches
        auto is_pruned = [&pruned_slots](int32_t slot) {
            return std::find(pruned_slots.begin(), pruned_slots.end(), slot) != pruned_slots.end();
        };

        state->cached_profiles.erase(
            std::remove_if(state->cached_profiles.begin(), state->cached_profiles.end(),
                           [&is_pruned](const SetNetworkProfileRequest& p) { return is_pruned(p.configurationSlot); }),
            state->cached_profiles.end());

        state->slots.erase(std::remove_if(state->slots.begin(), state->slots.end(), is_pruned), state->slots.end());

        // Cap active_priority after shrink
        if (state->slots.empty()) {
            state->active_priority = 0;
        } else if (static_cast<std::size_t>(state->active_priority) >= state->slots.size()) {
            state->active_priority = static_cast<std::int32_t>(state->slots.size() - 1);
        }
    }

    // Clear per-slot DM variables to prevent re-injection via SetVariables; done outside the state
    // lock because DeviceModel has its own synchronization.
    for (const int32_t slot : pruned_slots) {
        this->configuration.clear_network_connection_profile(slot);
    }

    // Rebuild and persist NetworkConfigurationPriority from remaining slots
    std::string new_priority;
    {
        auto state = this->m_state.handle();
        for (const int32_t slot : state->slots) {
            if (!new_priority.empty()) {
                new_priority += ',';
            }
            new_priority += std::to_string(slot);
        }
    }

    this->configuration.set_network_configuration_priority(new_priority, VARIABLE_ATTRIBUTE_VALUE_SOURCE_INTERNAL);
}

} // namespace ocpp
