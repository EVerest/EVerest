// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <string>

namespace ocpp {
namespace v2 {

/// \brief Identifies one OCPP connection of the charging station.
///
/// The primary connection is the single authoritative CSMS connection. Its configuration lives on
/// the instance-less \c OCPPCommCtrlr component, so it carries no instance name.
///
/// A secondary connection is a limited additional connection as described in the OCA application
/// note "Secondary OCPP Connection on a Charging Station". Its configuration lives on an instance of
/// \c OCPPCommCtrlr, and that component instance name identifies the connection.
///
/// Instance names are free to choose (application note section 3.3.1). The names EMS, MAINT, MONITOR
/// and DATACOLLECTION are conventions only; no name is special-cased here or anywhere else.
class ConnectionId {
public:
    /// \brief Longest instance name accepted, matching the CiString<50> of Component::instance.
    static constexpr std::size_t MAX_INSTANCE_LENGTH = 50;

    /// \brief Constructs the primary connection.
    ConnectionId() = default;

    /// \brief The primary (CSMS) connection.
    static ConnectionId primary();

    /// \brief A secondary connection identified by its OCPPCommCtrlr component instance name.
    /// \param instance Instance name; must satisfy is_valid_instance_name().
    static ConnectionId secondary(const std::string& instance);

    /// \brief True for the primary connection, i.e. when no instance name is held.
    [[nodiscard]] bool is_primary() const;

    /// \brief The OCPPCommCtrlr component instance name, or std::nullopt for the primary connection.
    ///
    /// This is exactly the value to put into Component::instance, so it can be handed to the device
    /// model unchanged.
    [[nodiscard]] const std::optional<std::string>& instance() const;

    /// \brief Human readable form for logging: "primary" or "OCPPCommCtrlr[<instance>]".
    ///
    /// Never parse this back; use instance() to address the device model.
    [[nodiscard]] std::string to_string() const;

    /// \brief Whether \p instance is acceptable as a secondary connection instance name.
    ///
    /// Accepts 1 to MAX_INSTANCE_LENGTH characters of [A-Za-z0-9_.-]. The character set is
    /// deliberately narrower than CiString<50> so that an instance name is always safe to embed in
    /// the device model hierarchy notation used by OCPPCommCtrlr[<n>].AllowSetVariables, which gives
    /// '/', '#' and '&' a meaning.
    static bool is_valid_instance_name(const std::string& instance);

    friend bool operator==(const ConnectionId& lhs, const ConnectionId& rhs);
    friend bool operator!=(const ConnectionId& lhs, const ConnectionId& rhs);
    /// \brief Orders the primary connection before every secondary, then secondaries by name, so
    ///        that iteration over a std::map or std::set keyed by ConnectionId reaches the primary
    ///        first and is otherwise stable.
    friend bool operator<(const ConnectionId& lhs, const ConnectionId& rhs);

private:
    explicit ConnectionId(std::string instance);

    std::optional<std::string> m_instance;
};

} // namespace v2
} // namespace ocpp
