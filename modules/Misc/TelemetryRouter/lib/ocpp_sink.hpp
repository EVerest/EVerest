// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "sink.hpp"
#include "value_format.hpp"

namespace telemetry_router {

/// Address and value of one device model write (attribute Actual)
struct OcppWrite {
    std::string component_name;
    std::optional<std::string> component_instance;
    std::optional<int> evse;
    std::optional<int> connector;
    std::string variable_name;
    std::optional<std::string> variable_instance;
    std::string value;
};

enum class OcppWriteStatus {
    Accepted,
    Rejected,
    UnknownComponent,
    UnknownVariable,
    NotSupportedAttributeType,
    RebootRequired,
};

/// \brief Writes to the OCPP device model
class IOcppClient {
public:
    virtual ~IOcppClient() = default;
    /// \returns one status per write, in the order of \p writes
    /// \throws std::exception if the call fails as a whole
    virtual std::vector<OcppWriteStatus> set_variables(const std::vector<OcppWrite>& writes) = 0;
};

struct OcppSinkOptions {
    std::chrono::milliseconds flush_interval{1000};
    std::chrono::seconds min_interval{10};
    std::size_t max_batch{50};
    std::chrono::seconds dead_retry{600};
    std::chrono::seconds max_backoff{30};
    bool run_flush_thread{true};
    std::function<std::chrono::steady_clock::time_point()> clock{std::chrono::steady_clock::now};
};

/// \brief Writes gauge, counter and state elements to OCPP device model variables
///
/// Values are coalesced per variable: only the latest value is kept, a value equal to the last written one
/// (or within the deadband) is not written again, and a variable is written at most once per min_interval.
/// Without an OCPP client the sink only logs what it would write.
class OcppSink : public Sink {
public:
    OcppSink(std::string name, const nlohmann::json& options, IOcppClient* client, OcppSinkOptions defaults);
    ~OcppSink() override;

    std::string_view type() const override;
    std::unique_ptr<SinkBinding> bind(const Target& target, const Producer& producer,
                                      const ElementDeclaration& element) override;
    void submit(const Sample& sample, const SinkBinding& binding) override;
    void release(const SinkBinding& binding) override;
    void enable() override;
    void stop() override;

    /// \brief Write all due values; called periodically by the flush thread
    void flush();

private:
    struct Entry;
    struct Binding;

    std::vector<std::size_t> collect_due(std::chrono::steady_clock::time_point now);
    void write_batch(const std::vector<std::size_t>& batch, std::chrono::steady_clock::time_point now);
    void run();

    IOcppClient* m_client;
    OcppSinkOptions m_options;

    std::mutex m_mutex;
    std::vector<Entry> m_entries;
    std::map<std::string, std::size_t> m_entry_by_address;
    bool m_enabled{false};
    bool m_any_accepted{false};
    std::optional<std::chrono::steady_clock::time_point> m_backoff_until;
    std::chrono::milliseconds m_backoff{0};

    std::thread m_thread;
    std::condition_variable m_stop_cv;
    bool m_stop{false};
};

} // namespace telemetry_router
