// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include <chrono>
#include <everest/slac/timer.hpp>

namespace everest::lib::slac {

timer::timer() : reference(clock::now()), target(reference), duration(0) {
}

timer::operator bool() const {
    return timeout();
}

void timer::reset() {
    resetReference();
    target = reference + duration;
}

void timer::resetReference() {
    reference = clock::now();
}

void timer::forceTimeoutState() {
    reference = clock::now() - duration;
    target = clock::now();
}

void timer::setDurationMicroSeconds(long long value) {
    setDuration(std::chrono::microseconds(value));
}

void timer::setDurationMilliSeconds(long long value) {
    setDuration(std::chrono::milliseconds(value));
}

void timer::setDurationSeconds(long long value) {
    setDuration(std::chrono::seconds(value));
}

void timer::setDurationMinutes(long long value) {
    setDuration(std::chrono::minutes(value));
}

timer::tick timer::remaining() const {
    return std::chrono::duration_cast<tick>(target - clock::now());
}

long long timer::getRemainingMicroSeconds() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(remaining()).count();
}

long long timer::getRemainingMilliSeconds() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(remaining()).count();
}

long long timer::getRemainingSeconds() const {
    return std::chrono::duration_cast<std::chrono::seconds>(remaining()).count();
}

long long timer::getRemainingMinutes() const {
    return std::chrono::duration_cast<std::chrono::minutes>(remaining()).count();
}

void timer::setTimePoint(tp const& value) {
    auto now = clock::now();
    target = value;
    auto diff = std::chrono::duration_cast<tick>(now - value);
    duration = diff.count() > 0 ? diff : tick(0);
}

timer::tp timer::getTargetTime() const {
    return target;
}

bool timer::timeout() const {
    return clock::now() > target;
}

} // namespace everest::lib::slac
