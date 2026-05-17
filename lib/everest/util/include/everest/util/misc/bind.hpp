// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>

namespace everest::lib::util {
/**
 * @brief Bind member function to std::function
 * @param[in] func Member function
 * @param[in] ptr to object (likely this)
 */

template <typename R, typename C, typename... Args>
std::function<R(Args...)> function_bind_obj(R (C::*func)(Args...), C* obj) {
    return [func, obj](Args... args) -> R { return (obj->*func)(std::forward<Args>(args)...); };
}

/**
 * @brief Binds a member function to a specific object instance without std::function overhead.
 *
 * This utility creates a lightweight lambda closure that captures a member function pointer
 * and an object pointer.
 *
 * @tparam R     The return type of the member function.
 * @tparam C     The class type owning the member function.
 * @tparam Args  The argument types expected by the member function.
 *
 * @param func   A pointer to the member function (e.g., &MyClass::handle_data).
 * @param obj    A pointer to the instance the function should be called on.
 *
 * @return A lambda closure that, when called, invokes the member function on @p obj.
 *
 * @note By returning 'auto', this function avoids the type-erasure overhead of std::function.
 * The compiler can often inline the resulting call entirely. It remains implicitly
 * convertible to std::function if required by an API.
 *
 * @par Example:
 * @code
 * auto processor = bind_obj(&MyCalss::handle_data, this);
 * consume(queue, processor);
 * @endcode
 */
template <typename R, typename C, typename... Args> auto bind_obj(R (C::*func)(Args...), C* obj) {
    return [func, obj](Args&&... args) -> R { return (obj->*func)(std::forward<Args>(args)...); };
}

/**
 * @brief Const-qualified version of bind_obj.
 *
 * Overload to support binding to member functions marked with 'const'.
 * @tparam R     The return type of the member function.
 * @tparam C     The class type owning the member function.
 * @tparam Args  The argument types expected by the member function.
 *
 * @param func   A pointer to the const member function.
 * @param obj    A pointer to the const instance.
 *
 * @return A lambda closure that invokes the const member function on @p obj.
 */
template <typename R, typename C, typename... Args> auto bind_obj(R (C::*func)(Args...) const, const C* obj) {
    return [func, obj](Args&&... args) -> R { return (obj->*func)(std::forward<Args>(args)...); };
}

} // namespace everest::lib::util
