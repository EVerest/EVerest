// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <chrono>
#include <functional>
#include <string>
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
 * @brief Trait: T declares a member 'supports_tx_coalescing'.
 */
template <typename T, typename V = void> struct has_member_supports_tx_coalescing : std::false_type {};

/**
 * @brief Specialization for a T that has it.
 */
template <typename T>
struct has_member_supports_tx_coalescing<T, std::void_t<decltype(T::supports_tx_coalescing)>> : std::true_type {};

/**
 * @brief Trait: whether generic_fd_event_client::tx_coalescing exists for a policy. False unless
 * the policy declares 'supports_tx_coalescing'.
 * @details Coalescing appends to a payload the policy may be mid way through sending. Only a byte
 * stream whose tx() leaves exactly the unsent bytes in the payload and tolerates it growing may
 * opt in; frame transports and TLS (identical buffer on retry) may not.
 */
template <typename T, typename V = void> struct policy_supports_tx_coalescing : std::false_type {};

/**
 * @brief Specialization carrying the declared value.
 */
template <typename T>
struct policy_supports_tx_coalescing<T, std::enable_if_t<has_member_supports_tx_coalescing<T>::value>>
    : std::integral_constant<bool, T::supports_tx_coalescing> {};

/**
 * @brief Variable template for policy_supports_tx_coalescing.
 */
template <typename T> inline constexpr bool policy_supports_tx_coalescing_v = policy_supports_tx_coalescing<T>::value;

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

/**
 * @brief Primary template for the trait to check if a type T has a member function
 * 'handshake_complete()' returning something convertible to bool.
 * @tparam T The type to check.
 */
template <typename T, typename V = void> struct has_member_handshake_complete : std::false_type {};

template <typename T>
struct has_member_handshake_complete<
    T, std::enable_if_t<std::is_convertible_v<decltype(std::declval<T&>().handshake_complete()), bool>>>
    : std::true_type {};

template <typename T> inline constexpr bool has_member_handshake_complete_v = has_member_handshake_complete<T>::value;

template <typename T, typename V = void> struct has_member_handshake_step : std::false_type {};

template <typename T>
struct has_member_handshake_step<
    T, std::enable_if_t<std::is_convertible_v<decltype(std::declval<T&>().handshake_step()), bool>>> : std::true_type {
};

template <typename T> inline constexpr bool has_member_handshake_step_v = has_member_handshake_step<T>::value;

/**
 * @brief Trait for a member function 'desired_events()'.
 * @details The return type is not constrained: naming event::poll_events here would close a cycle with
 * event/fd_event_handler.hpp, which includes the event client that consumes this trait.
 * @tparam T The type to check.
 */
template <typename T, typename V = void> struct has_member_desired_events : std::false_type {};

template <typename T>
struct has_member_desired_events<T, std::void_t<decltype(std::declval<T&>().desired_events())>> : std::true_type {};

template <typename T> inline constexpr bool has_member_desired_events_v = has_member_desired_events<T>::value;

/**
 * @brief Defines the policy trait for an event client with a protocol handshake phase.
 * @tparam T The type to check.
 */
template <typename T>
struct event_client_handshake_policy
    : std::integral_constant<bool, has_member_handshake_complete<T>::value && has_member_handshake_step<T>::value &&
                                       has_member_desired_events<T>::value> {};

template <typename T> inline constexpr bool event_client_handshake_policy_v = event_client_handshake_policy<T>::value;

/**
 * @brief True for a type providing none of the three handshake members.
 * @details The complement of \ref event_client_handshake_policy, which is not its negation: a
 * type providing only some of the three satisfies neither. 'handshake_timeout()' is not counted
 * here, it is optional and bounds a handshake the three implement.
 * @tparam T The type to check.
 */
template <typename T>
inline constexpr bool event_client_has_no_handshake_trio_v =
    not has_member_handshake_complete_v<T> and not has_member_handshake_step_v<T> and
    not has_member_desired_events_v<T>;

template <typename T, typename V = void> struct has_member_handshake_timeout : std::false_type {};

template <typename T>
struct has_member_handshake_timeout<
    T, std::enable_if_t<
           std::is_convertible_v<decltype(std::declval<T&>().handshake_timeout()), std::chrono::milliseconds>>>
    : std::true_type {};

template <typename T> inline constexpr bool has_member_handshake_timeout_v = has_member_handshake_timeout<T>::value;

template <typename T, typename V = void> struct has_member_get_error_string : std::false_type {};

template <typename T>
struct has_member_get_error_string<
    T, std::enable_if_t<std::is_convertible_v<decltype(std::declval<T&>().get_error_string()), std::string>>>
    : std::true_type {};

template <typename T> inline constexpr bool has_member_get_error_string_v = has_member_get_error_string<T>::value;

} // namespace everest::lib::io::utilities
