// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef TEST_FRAMEWORK_TRANSPORT_CONSUMER_HPP
#define TEST_FRAMEWORK_TRANSPORT_CONSUMER_HPP

#include "ld-ev.hpp"

#include <generated/interfaces/test_framework_transport/Interface.hpp>

#include <mutex>
#include <string>

namespace module {

struct Conf {};

class TestFrameworkTransportConsumer : public Everest::ModuleBase {
public:
    TestFrameworkTransportConsumer() = delete;
    TestFrameworkTransportConsumer(const ModuleInfo& info, std::unique_ptr<test_framework_transportIntf> r_provider,
                                   Conf& config) :
        ModuleBase(info), r_provider(std::move(r_provider)), config(config){};

    const std::unique_ptr<test_framework_transportIntf> r_provider;
    const Conf& config;

private:
    friend class LdEverest;
    void init();
    void ready();
    void maybe_log_success();

    std::mutex result_mutex;
    std::string observed_variable;
    std::string command_response;
    bool variable_ok{false};
    bool command_ok{false};
    bool success_logged{false};
};

} // namespace module

#endif // TEST_FRAMEWORK_TRANSPORT_CONSUMER_HPP
