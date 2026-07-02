// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <iostream>
#include <sstream>

namespace logging {

class UTestLogger {
public:
    enum class level_t : std::uint8_t {
        Critical = 4,
        Error = 3,
        Warning = 2,
        Info = 1,
        Debug = 0,
    };

private:
    std::ostream& m_stream;
    static level_t s_level;
    level_t m_level;
    std::ostringstream m_out;

public:
    UTestLogger() = delete;
    UTestLogger(const char* file, int line) : UTestLogger(std::cerr, level_t::Error, file, line) {
    }
    UTestLogger(std::ostream& stream, level_t level, const char* file, int line);
    ~UTestLogger();

    template <typename T> constexpr std::ostream& operator<<(const T& item) {
        m_out << item;
        return m_out;
    }

    static void set_level(level_t level) {
        s_level = level;
    }
};

} // namespace logging

#undef EVLOG_level
#undef EVLOG_critical
#undef EVLOG_error
#undef EVLOG_warning
#undef EVLOG_info
#undef EVLOG_debug
#undef EVLOG_AND_THROW

#define EVLOG_level(LEVEL) logging::UTestLogger::set_level(logging::UTestLogger::level_t::LEVEL)

#define EVLOG_critical logging::UTestLogger(std::cerr, logging::UTestLogger::level_t::Critical, __FILE__, __LINE__)
#define EVLOG_error    logging::UTestLogger(std::cerr, logging::UTestLogger::level_t::Error, __FILE__, __LINE__)
#define EVLOG_warning  logging::UTestLogger(std::cout, logging::UTestLogger::level_t::Warning, __FILE__, __LINE__)
#define EVLOG_info     logging::UTestLogger(std::cout, logging::UTestLogger::level_t::Info, __FILE__, __LINE__)
#define EVLOG_debug    logging::UTestLogger(std::cout, logging::UTestLogger::level_t::Debug, __FILE__, __LINE__)
#define EVLOG_AND_THROW(ex)                                                                                            \
    do {                                                                                                               \
        try {                                                                                                          \
            throw ex;                                                                                                  \
        } catch (std::exception & e) {                                                                                 \
            EVLOG_error << e.what();                                                                                   \
            throw;                                                                                                     \
        }                                                                                                              \
    } while (0)
