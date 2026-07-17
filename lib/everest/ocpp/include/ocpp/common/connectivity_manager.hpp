// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2024 Pionix GmbH and Contributors to EVerest

#pragma once

#include <ocpp/common/connectivity_manager_configuration.hpp>
#include <ocpp/common/websocket/websocket.hpp>
#include <ocpp/v2/messages/SetNetworkProfile.hpp>

#include <everest/util/async/monitor.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <optional>

namespace ocpp {

/// \brief The result of a configuration of a network profile.
struct ConfigNetworkResult {
    std::optional<std::string> interface_address; ///< ip address or interface string
    bool success;                                 ///< true if the configuration was successful
};

/// \brief The next configuration slot to dial after the active slot's attempts were exhausted.
struct NextSlotSelection {
    std::int32_t slot; ///< configuration slot to attempt next
    bool is_fallback;  ///< true when this is the B10.FR.07 last-successful fallback target
};

/// \brief Decide which configuration slot to dial after the active slot exhausted its attempts.
///
/// Implements OCPP 2.0.1 B10.FR.07: once every entry of the priority list has been tried since the
/// last successful connection (\p failed_slots_since_success reached the list size), fall back to
/// \p last_successful_slot independent of the priority list and keep reconnecting to it. Otherwise
/// the next entry in the priority list is selected (wrapping around).
///
/// \param priority_slots Ordered configuration slots (highest priority first).
/// \param current_slot The slot whose attempts were just exhausted.
/// \param failed_slots_since_success Number of slots exhausted since the last successful connection,
///        including \p current_slot.
/// \param last_successful_slot Slot of the last successful connection, or std::nullopt if none.
/// \param in_fallback True if the manager is already reconnecting to the fallback profile.
/// \return The next slot to dial, or std::nullopt if \p priority_slots is empty.
std::optional<NextSlotSelection> select_next_network_slot(const std::vector<std::int32_t>& priority_slots,
                                                          std::int32_t current_slot, int failed_slots_since_success,
                                                          std::optional<std::int32_t> last_successful_slot,
                                                          bool in_fallback);

using WebsocketConnectionCallback =
    std::function<void(int configuration_slot, const ocpp::v2::NetworkConnectionProfile& network_connection_profile,
                       const OcppProtocolVersion version)>;
using WebsocketConnectionFailedCallback = std::function<void(ConnectionFailedReason reason)>;
using ConfigureNetworkConnectionProfileCallback = std::function<std::future<ConfigNetworkResult>(
    const std::int32_t configuration_slot, const ocpp::v2::NetworkConnectionProfile& network_connection_profile)>;

class ConnectivityManagerInterface {
public:
    virtual ~ConnectivityManagerInterface() = default;

    /// \brief Set the \p callback that is called when a message is received from the websocket
    ///
    virtual void set_message_callback(const std::function<void(const std::string& message)>& callback) = 0;

    /// \brief Set the logger \p logging
    ///
    virtual void set_logging(std::shared_ptr<MessageLogging> logging) = 0;

    /// \brief Set the websocket \p authorization_key
    ///
    virtual void set_websocket_authorization_key(const std::string& authorization_key) = 0;

    /// \brief Set the websocket \p connection_options
    ///
    virtual void set_websocket_connection_options(const WebsocketConnectionOptions& connection_options) = 0;

    /// \brief Set the websocket connection options without triggering a reconnect
    ///
    virtual void set_websocket_connection_options_without_reconnect() = 0;

    /// \brief Apply the websocket ping interval to a connection immediately.
    /// \param ping_interval_s The ping interval in seconds (0 disables pinging)
    /// \param pong_timeout_s The pong timeout in seconds
    ///
    /// Unlike set_websocket_connection_options_without_reconnect(), this takes effect on the current connection
    /// instead of only on the next reconnect. Only has an effect while the websocket is connected; while
    /// disconnected the interval is applied from the configuration on the next reconnect.
    virtual void set_websocket_ping_interval(std::int32_t ping_interval_s, std::int32_t pong_timeout_s) = 0;

    /// \brief Set the \p callback that is called when the websocket is connected.
    ///
    virtual void set_websocket_connected_callback(WebsocketConnectionCallback callback) = 0;

    /// \brief Set the \p callback that is called when the websocket is disconnected.
    ///
    virtual void set_websocket_disconnected_callback(WebsocketConnectionCallback callback) = 0;

    /// \brief Set the \p callback that is called when the websocket could not connect with a specific reason
    ///
    virtual void set_websocket_connection_failed_callback(WebsocketConnectionFailedCallback callback) = 0;

    /// \brief Set the \p callback that is called to configure a network connection profile when none is configured
    ///
    virtual void
    set_configure_network_connection_profile_callback(ConfigureNetworkConnectionProfileCallback callback) = 0;

    /// \brief Gets the cached NetworkConnectionProfile based on the given \p configuration_slot.
    /// This returns the value from the cached network connection profiles.
    /// \return Returns a profile if the slot is found
    virtual std::optional<ocpp::v2::NetworkConnectionProfile>
    get_network_connection_profile(const std::int32_t configuration_slot) const = 0;

    /// \brief Get the priority of the given configuration slot.
    /// \param configuration_slot   The configuration slot to get the priority from.
    /// \return The priority if the configuration slot exists.
    ///
    virtual std::optional<std::int32_t>
    get_priority_from_configuration_slot(const std::int32_t configuration_slot) const = 0;

    /// @brief Get a snapshot of the network connection slots sorted by priority.
    /// Each item in the vector contains the configured configuration slots, where the slot with index 0 has the highest
    /// priority. A copy is returned so callers do not observe mid-mutation state through a reference.
    /// @return The network connection slots
    ///
    virtual std::vector<int> get_network_connection_slots() const = 0;

    /// \brief Check if the websocket is connected
    /// \return True is the websocket is connected, else false
    ///
    virtual bool is_websocket_connected() = 0;

    /// @brief Get the time the websocket has been disconnected.
    /// @return The time the websocket has been disconnected
    ///
    virtual std::chrono::time_point<std::chrono::steady_clock> get_time_disconnected() const = 0;

    /// \brief Connect to the websocket
    /// \param configuration_slot Optional the network_profile_slot to connect to. std::nullopt will select the slot
    /// internally.
    ///
    virtual void connect(std::optional<std::int32_t> network_profile_slot = std::nullopt) = 0;

    /// \brief Disconnect the websocket
    ///
    virtual void disconnect() = 0;

    /// \brief Stop delivering connected/disconnected notifications to the registered callbacks.
    ///
    /// Called during teardown after disconnect(). A late websocket-thread callback would otherwise
    /// reach into a ChargePoint whose members are already being destroyed (use-after-free). This
    /// blocks until any in-flight callback returns, then suppresses all further ones.
    virtual void disarm_connection_callbacks() = 0;

    /// \brief Suppress automatic reconnection without closing the current websocket.
    ///
    /// Unlike disconnect(), this does NOT close the live websocket: it only clears the internal
    /// "wants to be connected" intent and cancels any pending reconnect timer. A subsequent close
    /// initiated elsewhere (e.g. the CSMS closing the socket, or the regular post-transaction
    /// shutdown path) will therefore not trigger an auto-reconnect. Used just before an imminent
    /// whole-station reset so that any in-flight messages (e.g. the graceful
    /// TransactionEvent(Ended) of an ongoing transaction) can still be flushed on the live
    /// connection before it is closed for the reboot.
    ///
    virtual void suppress_reconnect() = 0;

    /// \brief send a \p message over the websocket
    /// \returns true if the message was sent successfully
    ///
    virtual bool send_to_websocket(const std::string& message) = 0;

    ///
    /// \brief Can be called when a network is disconnected, for example when an ethernet cable is removed.
    ///
    /// This is introduced because the websocket can take several minutes to timeout when a network interface becomes
    /// unavailable, whereas the system can detect this sooner.
    ///
    /// \param ocpp_interface The interface that is disconnected.
    ///
    virtual void on_network_disconnected(ocpp::v2::OCPPInterfaceEnum ocpp_interface) = 0;

    /// \brief Called when the charging station certificate is changed
    ///
    virtual void on_charging_station_certificate_changed() = 0;

    /// \brief Confirms the connection is successful so the security profile requirements can be handled
    virtual void confirm_successful_connection() = 0;

    /// \brief Reload network connection profiles from NetworkConfiguration DM components into the in-memory cache
    virtual void reload_network_profiles() = 0;

    /// \brief Write a network connection profile to the device model and refresh the in-memory cache.
    /// \param slot The configuration slot to write to
    /// \param profile The profile to write
    /// \param source The source of the change (e.g. 'csms', 'internal')
    /// \return true if the profile was written successfully
    virtual bool set_network_profile(int32_t slot, const ocpp::v2::NetworkConnectionProfile& profile,
                                     const std::string& source) = 0;
};

class ConnectivityManager : public ConnectivityManagerInterface {
private:
    /// \brief Configuration interface used to persist and retrieve network configuration
    ocpp::ConnectivityManagerConfiguration& configuration;
    /// \brief Pointer to the evse security class
    std::shared_ptr<EvseSecurity> evse_security;
    /// \brief Pointer to the logger
    std::shared_ptr<MessageLogging> logging;
    /// \brief The share path
    fs::path share_path;
    /// \brief Pointer to the websocket
    std::unique_ptr<Websocket> websocket;
    /// \brief The message callback
    std::function<void(const std::string& message)> message_callback;
    /// \brief Callback that is called when the websocket is connected successfully
    std::optional<WebsocketConnectionCallback> websocket_connected_callback;
    /// \brief Callback that is called when the websocket connection is disconnected
    std::optional<WebsocketConnectionCallback> websocket_disconnected_callback;
    /// \brief Callback that is called when the websocket could not connect with a specific reason
    std::optional<WebsocketConnectionFailedCallback> websocket_connection_failed_callback;
    /// \brief Callback that is called to configure a network connection profile when none is configured
    std::optional<ConfigureNetworkConnectionProfileCallback> configure_network_connection_profile_callback;

    Everest::SteadyTimer websocket_timer;

    // Serializes invocation of the connected/disconnected callbacks (websocket thread) with
    // disarm_connection_callbacks() (teardown thread). Once disarmed, the callbacks are suppressed.
    std::mutex connection_callbacks_mutex;
    bool connection_callbacks_disarmed{false};
    // Written from the OCPP message-handler thread (suppress_reconnect/disconnect) and read from the
    // websocket callback and websocket_timer threads, so it must be atomic.
    std::atomic<bool> wants_to_be_connected;
    OcppProtocolVersion connected_ocpp_version;

    /// @brief Mutable shared state, accessed concurrently from the OCPP message-handling thread,
    /// the websocket callback thread, and the websocket_timer thread. All access goes through the
    /// monitor handle, which acquires a recursive mutex for the handle's lifetime.
    struct NetworkProfileCacheState {
        /// Cached NetworkConnectionProfile entries reflecting the per-slot NetworkConfiguration DM variables.
        std::vector<ocpp::v2::SetNetworkProfileRequest> cached_profiles;
        /// Ordered list of configuration slots (highest priority first) derived from NetworkConfigurationPriority.
        std::vector<std::int32_t> slots;
        /// Index into \c slots of the slot currently considered active.
        std::int32_t active_priority{0};
        /// Slot currently being evaluated by \c try_connect_websocket (overrides active_priority if set).
        std::optional<std::int32_t> pending_configuration_slot;
        /// Last SecurityProfile value observed when pruning invalid profiles from the cache.
        int last_known_security_level{0};
        /// Slot of the last successful connection, used as the B10.FR.07 fallback target.
        std::optional<std::int32_t> last_successful_slot;
        /// Number of slots whose attempts were exhausted since the last successful connection.
        int failed_slots_since_success{0};
        /// True while reconnecting to the fallback (last-successful) profile after full-list exhaustion.
        bool in_fallback{false};
        /// Consecutive failed reconnect cycles since the last successful connection, used to drive the
        /// OCPP part 4 section 5.3 backoff between cross-attempt/fallback re-dials. Reset to 0 on success.
        int reconnect_attempts{0};
        /// Backoff (milliseconds) computed for the most recent reconnect cycle; base that get_reconnect_backoff_ms
        /// doubles for the next cycle. Reset to 0 on success.
        long reconnect_backoff_ms{0};
    };
    mutable everest::lib::util::monitor<NetworkProfileCacheState, std::recursive_mutex> m_state;

public:
    ConnectivityManager(ocpp::ConnectivityManagerConfiguration& configuration,
                        std::shared_ptr<EvseSecurity> evse_security, const fs::path& share_path = {});

    void reload_network_profiles() override;
    bool set_network_profile(int32_t slot, const ocpp::v2::NetworkConnectionProfile& profile,
                             const std::string& source) override;

    void set_message_callback(const std::function<void(const std::string& message)>& callback) override;
    void set_logging(std::shared_ptr<MessageLogging> logging) override;
    void set_websocket_authorization_key(const std::string& authorization_key) override;
    void set_websocket_connection_options(const WebsocketConnectionOptions& connection_options) override;
    void set_websocket_connection_options_without_reconnect() override;
    void set_websocket_ping_interval(std::int32_t ping_interval_s, std::int32_t pong_timeout_s) override;
    void set_websocket_connected_callback(WebsocketConnectionCallback callback) override;
    void set_websocket_disconnected_callback(WebsocketConnectionCallback callback) override;
    void set_websocket_connection_failed_callback(WebsocketConnectionFailedCallback callback) override;
    void set_configure_network_connection_profile_callback(ConfigureNetworkConnectionProfileCallback callback) override;
    std::optional<ocpp::v2::NetworkConnectionProfile>
    get_network_connection_profile(const std::int32_t configuration_slot) const override;
    std::optional<std::int32_t>
    get_priority_from_configuration_slot(const std::int32_t configuration_slot) const override;
    std::vector<int> get_network_connection_slots() const override;
    bool is_websocket_connected() override;
    std::chrono::time_point<std::chrono::steady_clock> get_time_disconnected() const override;
    void connect(std::optional<std::int32_t> network_profile_slot = std::nullopt) override;
    void disconnect() override;
    void disarm_connection_callbacks() override;
    void suppress_reconnect() override;
    bool send_to_websocket(const std::string& message) override;
    void on_network_disconnected(ocpp::v2::OCPPInterfaceEnum ocpp_interface) override;
    void on_charging_station_certificate_changed() override;
    void confirm_successful_connection() override;

    /// \brief Removes all connection profiles from the cache that have a security profile lower than the currently
    /// connected security profile
    void check_cache_for_invalid_security_profiles();

private:
    std::atomic<std::chrono::time_point<std::chrono::steady_clock>> time_disconnected{};

    /// \brief Mark \c time_disconnected with steady_clock::now() iff currently unset (zero).
    ///        Atomic compare-exchange so concurrent disconnect callbacks cannot overwrite the
    ///        first observed disconnect time.
    void mark_disconnected_at_now();

    /// \brief Initializes the websocket and tries to connect
    ///
    void try_connect_websocket();

    /// \brief Get the current websocket connection options for the given slot.
    /// Delegates to configuration and appends the local everest version string.
    /// \return the current websocket connection options, or nullopt on failure.
    ///
    std::optional<WebsocketConnectionOptions> get_ws_connection_options(const std::int32_t configuration_slot);

    /// \brief Read the everest version string from the deployed version_information.txt,
    ///        next to the binary. Returns nullopt if the file is missing or contains no usable line.
    std::optional<std::string> read_everest_version() const;

    /// \brief Calls the configuration callback to get the interface to use, if there is a callback
    /// \param slot The configuration slot to get the interface for
    /// \param profile The network connection profile to get the interface for
    ///
    /// \return The network configuration containing the network interface to use, nullptr if the request failed or the
    /// callback is not configured
    std::optional<ConfigNetworkResult>
    handle_configure_network_connection_profile_callback(int slot, const ocpp::v2::NetworkConnectionProfile& profile);

    /// \brief Function invoked when the web socket connected with the \p security_profile
    ///
    void on_websocket_connected(OcppProtocolVersion protocol);

    /// \brief Function invoked when the web socket disconnected
    ///
    void on_websocket_disconnected();

    /// \brief Function invoked when the web socket stops connecting
    ///        and does not re-attempt to connect again
    ///
    void on_websocket_stopped_connecting(ocpp::WebsocketCloseReason reason);

    ///
    /// \brief Get the active network configuration slot in use.
    /// \return The active slot (or pending slot override) if one is available, std::nullopt if the slot
    ///         list is empty or the current priority index would be out of range.
    ///
    std::optional<int> get_active_network_configuration_slot() const;

    ///
    /// \brief Get the network configuration slot of the given priority.
    /// \param priority The priority to get the configuration slot.
    /// \return The configuration slot if \p priority is a valid index, std::nullopt otherwise.
    ///
    std::optional<int> get_configuration_slot_from_priority(const int priority);

    ///
    /// \brief Get the next prioritized network configuration slot of the given configuration slot.
    /// \param configuration_slot The current configuration slot.
    /// \return The next prioritized configuration slot, or std::nullopt if the slot list is empty.
    ///
    std::optional<int> get_next_configuration_slot(std::int32_t configuration_slot);

    /// \brief Append the given slot to NetworkConfigurationPriority if it is not already listed.
    void append_slot_to_network_configuration_priority_if_absent(int32_t slot, const std::string& source);

    /// \brief Ensure the fallback slot is present in the in-memory working set (slots + cached
    ///        profiles) so try_connect_websocket() can dial it even when it is absent from the
    ///        current NetworkConfigurationPriority list. Reads the profile from the configuration
    ///        when not already cached. Caller must hold the state lock.
    void ensure_slot_in_working_set(NetworkProfileCacheState& state, std::int32_t slot);

    /// \brief Advance the section 5.3 reconnect-backoff counter for one failed reconnect cycle and return the
    ///        wait before the next re-dial. Reads RetryBackOff* from the connection options of \p slot
    ///        (these are global device-model values, identical across slots) and mutates
    ///        \c state.reconnect_attempts / \c state.reconnect_backoff_ms. The returned delay is
    ///        max(WEBSOCKET_INIT_DELAY, computed backoff) so genuine reconnection waits never dip below
    ///        the quick-advance floor. Caller must hold the state lock.
    std::chrono::milliseconds advance_reconnect_backoff(NetworkProfileCacheState& state, std::int32_t slot);

    /// \brief Cache all the network connection profiles
    void cache_network_connection_profiles();

    /// \brief Log an error if all cached profiles have security_profile 0. Caller must hold the state lock.
    void warn_if_all_security_level_zero_locked(const NetworkProfileCacheState& state) const;

    /// \brief Removes all NetworkConfiguration DM slots and in-memory cache entries for profiles that have a security
    /// profile lower than the currently connected security profile, and persists the updated
    /// NetworkConfigurationPriority
    void remove_network_connection_profiles_below_actual_security_profile();
};

} // namespace ocpp
