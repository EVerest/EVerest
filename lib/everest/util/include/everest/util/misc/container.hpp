// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <algorithm>
#include <iterator>
#include <optional>
#include <type_traits>

/**
 * @file container_utils.hpp
 * @brief Utility functions for generic STL container operations.
 */

namespace everest::lib::util {

/**
 * @brief Internal type trait to detect if a container has a .find() member function.
 * @tparam T The container type to check.
 * @tparam Value The type of the value to search for.
 */
template <typename T, typename Value, typename = void> struct has_find : std::false_type {};

template <typename T, typename Value>
struct has_find<T, Value, std::void_t<decltype(std::declval<const T&>().find(std::declval<const Value&>()))>>
    : std::true_type {};

/**
 * @brief Checks if a value exists in a sequence container (vector, list, etc.).
 * @note Internal implementation using O(n) linear search.
 */
template <typename Container, typename T> auto exists_impl(const Container& c, const T& val, std::false_type) -> bool {
    return std::find(std::begin(c), std::end(c), val) != std::end(c);
}

/**
 * @brief Checks if a value exists in an associative container (set, map, etc.).
 * @note Internal implementation using the container's optimized .find() method.
 */
template <typename Container, typename T> auto exists_impl(const Container& c, const T& val, std::true_type) -> bool {
    return c.find(val) != c.end();
}

/**
 * @brief Generically checks if a specific item exists within a container.
 * * This function automatically selects the most efficient search algorithm
 * available for the provided container type at compile-time.
 * This is supposed to be a replacement for C++20 'contains' method
 * * @tparam Container The type of the STL-compatible container.
 * @tparam T The type of the value to search for.
 * @param c The constant reference to the container to search.
 * @param val The value to look for.
 * @return true if the item is found, false otherwise.
 * * @par Complexity:
 * - **O(log n)** for associative containers (std::set, std::map).
 * - **O(1)** average for unordered containers (std::unordered_set/map).
 * - **O(n)** for sequence containers (std::vector, std::list).
 */
template <typename Container, typename T> bool exists(const Container& c, const T& val) {
    return exists_impl(c, val, has_find<Container, T>{});
}

/**
 * @brief Implementation for sequence containers (vector, list, etc.).
 * @return Pointer to the found element or nullptr.
 */
template <typename Container, typename T> auto find_ptr_impl(Container& c, const T& val, std::false_type) {
    auto it = std::find(std::begin(c), std::end(c), val);
    return (it != std::end(c)) ? &(*it) : nullptr;
}

/**
 * @brief Implementation for associative containers (set, map, etc.).
 * @return Pointer to the found element or nullptr.
 */
template <typename Container, typename T> auto find_ptr_impl(Container& c, const T& val, std::true_type) {
    auto it = c.find(val);
    return (it != std::end(c)) ? &(*it) : nullptr;
}

/**
 * @brief Generically finds an item and returns a pointer to it.
 * * This utility provides a unified interface to find an element across different
 * STL containers. It returns a pointer to the element if found, allowing
 * for both existence checking and immediate access.
 * * @tparam Container The STL container type.
 * @tparam T The type of the value/key to search for.
 * @param c The container (can be const or non-const).
 * @param val The value to search for.
 * @return A pointer to the element within the container, or `nullptr` if not found.
 * * @example
 * if (auto* item = utils::find_ptr(my_vector, 42)) {
 * *item = 43; // Modify if non-const
 * }
 */
template <typename Container, typename T> auto find_ptr(Container& c, const T& val) {
    return find_ptr_impl(c, val, has_find<Container, T>{});
}

/**
 * @brief Searches for an element and returns an optional iterator.
 * * This function abstracts the difference between sequence containers (like vector)
 * and associative containers (like set/map) to provide the most efficient
 * lookup possible.
 * * @tparam Container The STL container type.
 * @tparam T The value or key to search for.
 * @param c The container to search within.
 * @param val The value/key to find.
 * @return std::optional wrapping the iterator. Returns std::nullopt if not found.
 * * @note If found, the iterator can be used to access the element (*it) or
 * pass to c.erase(it) for efficient deletion.
 */
template <typename Container, typename T>
auto find_optional(Container& c, const T& val) -> std::optional<decltype(std::begin(c))> {
    if constexpr (has_find<Container, T>::value) {
        auto it = c.find(val);
        if (it != std::end(c))
            return it;
    } else {
        auto it = std::find(std::begin(c), std::end(c), val);
        if (it != std::end(c))
            return it;
    }
    return std::nullopt;
}

} // namespace everest::lib::util
