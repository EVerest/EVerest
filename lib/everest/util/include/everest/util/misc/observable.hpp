// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
#include <utility>

namespace everest::lib::util {

template <class T, class OnChange = std::function<void(const T&, const T&)>> class observable {
public:
    explicit observable(T value, OnChange onChange = {}) : m_value(std::move(value)), m_onChange(std::move(onChange)) {
    }

    const T& get() const noexcept {
        return m_value;
    }

    bool set(T newValue) {
        if (m_value == newValue) {
            return false;
        }

        T oldValue = std::move(m_value);
        m_value = std::move(newValue);

        if (m_onChange) {
            m_onChange(oldValue, m_value);
        }

        return true;
    }

    void setCallback(OnChange callback) {
        m_onChange = std::move(callback);
    }

    void clearCallback() {
        m_onChange = nullptr;
    }

    operator const T&() const noexcept {
        return m_value;
    }

private:
    T m_value;
    OnChange m_onChange;
};
} // namespace everest::lib::util
