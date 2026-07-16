// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "logging.hpp"

#include <filesystem>
#include <string_view>

namespace {
std::string_view to_string(logging::UTestLogger::level_t level) {
    using level_t = logging::UTestLogger::level_t;

    switch (level) {
    case level_t::Critical:
        return "Critical:  ";
    case level_t::Error:
        return "Error:     ";
    case level_t::Warning:
        return "Warning:   ";
    case level_t::Info:
        return "Info:      ";
    case level_t::Debug:
        return "Debug:     ";
    }
    return "<unknown>: ";
}
} // namespace

namespace logging {

UTestLogger::level_t UTestLogger::s_level{UTestLogger::level_t::Debug};

UTestLogger::UTestLogger(std::ostream& stream, level_t level, const char* file, int line) :
    m_level(level), m_stream(stream) {
    const auto f = std::filesystem::path((file == nullptr) ? "(unknown)" : file);
    m_out << to_string(level) << f.filename().string() << ':' << line << ' ';
}

UTestLogger::~UTestLogger() {
    if (m_level >= s_level) {
        m_out.put('\n');
        m_stream << m_out.str();
    }
}

} // namespace logging
