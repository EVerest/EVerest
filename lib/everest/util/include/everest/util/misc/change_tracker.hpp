// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/util/math/comparison.hpp>

#include <type_traits>
#include <utility>

/**
 * @file change_tracker.hpp
 * @brief Tracks incremental changes for a telemetry-like struct during one update cycle.
 */

namespace everest::lib::util {

/**
 * @brief Non-owning change tracker for an existing value object.
 *
 * The tracker holds a reference to an existing value object and records whether that object changed through one update
 * transaction. It is intended to be constructed locally, passed to update code, queried once, and then discarded.
 *
 * @tparam T Type of the tracked value object.
 */
template <typename T> class change_tracker {
public:
    /**
     * @brief Construct a tracker for an existing value.
     * @param value Reference to the value that should be tracked.
     */
    explicit constexpr change_tracker(T& value) : m_value(value) {
    }

    /**
     * @brief Copy construction is disabled to keep change state tied to the active tracker.
     */
    change_tracker(const change_tracker&) = delete;

    /**
     * @brief Move construction is disabled to keep change state tied to the active tracker.
     */
    change_tracker(change_tracker&&) = delete;

    /**
     * @brief Copy assignment is disabled to keep change state tied to the active tracker.
     */
    change_tracker& operator=(const change_tracker&) = delete;

    /**
     * @brief Move assignment is disabled to keep change state tied to the active tracker.
     */
    change_tracker& operator=(change_tracker&&) = delete;

    /**
     * @brief Read-only access to the tracked value.
     * @return Const reference to the currently tracked value.
     */
    const T& value() const {
        return m_value;
    }

    /**
     * @brief Get mutable access to the tracked value and mark the transaction as changed.
     * @return Mutable reference to the tracked value.
     */
    T& mutable_value() {
        mark_changed();
        return m_value;
    }

    /**
     * @brief Set a single member field using exact comparison.
     * @tparam Field Pointer-to-member type.
     * @param field Pointer to the member on @p T to update.
     * @param new_value New value for the member.
     */
    template <typename Field, typename NewValue> void set(Field T::*field, NewValue&& new_value) {
        using ValueType = std::remove_cv_t<std::remove_reference_t<decltype(m_value.*field)>>;
        ValueType value = static_cast<ValueType>(std::forward<NewValue>(new_value));

        if (m_value.*field != value) {
            m_value.*field = std::move(value);
            mark_changed();
        }
    }

    /**
     * @brief Set a floating-point member field with decimal precision tolerance.
     *
     * If the current and new values compare equal via @ref almost_eq, the tracked value is left unchanged. This lets
     * accumulated drift eventually publish relative to the last accepted value instead of the last sampled value.
     *
     * @tparam Prec Decimal precision to tolerate as in @ref almost_eq.
     * @tparam Field Pointer-to-member type.
     * @param field Pointer to the member on @p T to update.
     * @param new_value New floating-point value for the member.
     */
    template <int Prec, typename Field, typename NewValue> void set_almost_eq(Field T::*field, NewValue&& new_value) {
        using ValueType = std::remove_cv_t<std::remove_reference_t<decltype(m_value.*field)>>;
        ValueType value = static_cast<ValueType>(std::forward<NewValue>(new_value));

        if (!everest::lib::util::almost_eq<Prec>(m_value.*field, value)) {
            m_value.*field = std::move(value);
            mark_changed();
        }
    }

    /**
     * @brief Explicitly mark the tracked value as changed.
     */
    void mark_changed() {
        m_changed = true;
    }

    /**
     * @brief Check whether this update transaction changed any field.
     * @return true if at least one assignment changed the tracked value in this tracker lifetime.
     */
    bool changed() const {
        return m_changed;
    }

private:
    T& m_value;
    bool m_changed = false;
};

} // namespace everest::lib::util
