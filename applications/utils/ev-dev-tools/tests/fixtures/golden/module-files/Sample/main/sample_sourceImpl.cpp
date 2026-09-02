// SPDX-License-Identifier: Apache-2.0

#include "sample_sourceImpl.hpp"

namespace module {
namespace main {

void sample_sourceImpl::init() {

}

void sample_sourceImpl::ready() {

}

void sample_sourceImpl::shutdown() {

}

types::sample::Reading sample_sourceImpl::handle_configure(types::sample::Mood& mood, double& interval, bool& enabled, std::vector<types::sample::Mood>& allowed) {
    // your code for cmd configure goes here
    return {};
}

void sample_sourceImpl::handle_reset() {
    // your code for cmd reset goes here
}

bool sample_sourceImpl::handle_store(std::variant<std::nullptr_t, Array, Object, bool, double, int, std::string>& value) {
    // your code for cmd store goes here
    return true;
}

} // namespace main
} // namespace module
