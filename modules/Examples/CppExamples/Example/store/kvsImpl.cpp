// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "kvsImpl.hpp"

namespace module {
namespace store {

void kvsImpl::init() {
}

void kvsImpl::ready() {
}

void kvsImpl::shutdown() {
}

void kvsImpl::handle_store(std::string& key,
                           std::variant<std::nullptr_t, Array, Object, bool, double, int, std::string>& value) {
    // your code for cmd store goes here
}

std::variant<std::nullptr_t, Array, Object, bool, double, int, std::string> kvsImpl::handle_load(std::string& key) {
    // your code for cmd load goes here
    return {};
}

void kvsImpl::handle_delete(std::string& key) {
    // your code for cmd delete goes here
}

bool kvsImpl::handle_exists(std::string& key) {
    // your code for cmd exists goes here
    return true;
}

} // namespace store
} // namespace module
