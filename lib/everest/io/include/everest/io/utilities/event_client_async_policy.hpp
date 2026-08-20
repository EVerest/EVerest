// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <functional>
#include <type_traits>

namespace everest::lib::io::utilities {

/**
 * @brief Primary template for the trait to check for the existence of a member function 'setup'.
 * @tparam T The type to check.
 */
template <typename T, typename V = void> struct has_member_setup : std::false_type {};

/**
 * @brief Specialization of has_member_setup.
 * This checks for existence and accessibility of a member function 'setup', accepting any signature.
 * @tparam T The type to check.
 */
template <typename T> struct has_member_setup<T, std::void_t<decltype(&T::setup)>> : std::true_type {};

/**
 * @brief Primary template for the trait to check if a type T has a member function
 * 'connect(std::function<void(bool, int)> const&)' with any return type.
 * @tparam T The type to check.
 */
template <typename T, typename V = void> struct has_member_connect : std::false_type {};

/**
 * @brief Specialization of has_member_connect.
 * This checks the existence of a member function connect with 'std::function<void(bool, int)>' as parameter.
 * @tparam T The type to check.
 */
template <typename T>
struct has_member_connect<
    T, std::void_t<decltype(std::declval<T>().connect(std::declval<std::function<void(bool, int)> const&>()))>>
    : std::true_type {};

/**
 * @brief Defines the policy trait for an asynchronous event client.
 * A type T satisfies this policy if it has:
 * 1. An accessible member 'setup' (any signature).
 * 2. A callable member function 'connect(const std::function<void(bool, int)>&)' (any return type).
 * @tparam T The type to check.
 */
template <typename T>
struct event_client_async_policy
    : std::integral_constant<bool, has_member_setup<T>::value && has_member_connect<T>::value> {};

/**
 * @brief Convenience variable template for the event_client_async_policy trait's value.
 * @tparam T The type to check.
 */
template <typename T> inline constexpr bool event_client_async_policy_v = event_client_async_policy<T>::value;

/**
 * @brief Primary template for the trait to check for the existence of a member
 * 'buffer_tx_before_connect'.
 * @tparam T The type to check.
 */
template <typename T, typename V = void> struct has_member_buffer_tx_before_connect : std::false_type {};

/**
 * @brief Specialization of has_member_buffer_tx_before_connect.
 * This checks for existence and accessibility of a static member 'buffer_tx_before_connect'.
 * @tparam T The type to check.
 */
template <typename T>
struct has_member_buffer_tx_before_connect<T, std::void_t<decltype(T::buffer_tx_before_connect)>> : std::true_type {};

/**
 * @brief Primary template for the trait carrying the tx buffering default a policy declares.
 * A policy that declares no 'buffer_tx_before_connect' does not buffer, see \ref tx_buffering.
 * @tparam T The type to check.
 */
template <typename T, typename V = void> struct policy_buffers_tx_before_connect : std::false_type {};

/**
 * @brief Specialization of policy_buffers_tx_before_connect for a policy that declares
 * 'static constexpr bool buffer_tx_before_connect'. It carries the declared value.
 * @tparam T The type to check.
 */
template <typename T>
struct policy_buffers_tx_before_connect<T, std::enable_if_t<has_member_buffer_tx_before_connect<T>::value>>
    : std::integral_constant<bool, T::buffer_tx_before_connect> {};

/**
 * @brief Convenience variable template for the policy_buffers_tx_before_connect trait's value.
 * @tparam T The type to check.
 */
template <typename T>
inline constexpr bool policy_buffers_tx_before_connect_v = policy_buffers_tx_before_connect<T>::value;

/**
 * @enum tx_buffering
 * @brief Whether a client holds payloads written before its connection is up.
 * @details Selected per instance at construction, defaulting to what the policy declares through
 * \ref policy_buffers_tx_before_connect. Only a policy that connects asynchronously has a
 * pre-connect window, see \ref event_client_async_policy.
 */
enum class tx_buffering {
    /** Reject tx() until the connection is up. */
    discard,
    /** Accept while the connection is fresh, hold in the bounded buffer, deliver once it is up. */
    buffer
};

} // namespace everest::lib::io::utilities
