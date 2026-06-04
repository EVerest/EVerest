// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef TEST_FRAMEWORK_TRANSPORT_PROVIDER_HPP
#define TEST_FRAMEWORK_TRANSPORT_PROVIDER_HPP

#include "ld-ev.hpp"

#include <generated/interfaces/test_framework_transport/Implementation.hpp>

namespace module {

struct Conf {};

class TestFrameworkTransportProvider : public Everest::ModuleBase {
public:
    TestFrameworkTransportProvider() = delete;
    TestFrameworkTransportProvider(const ModuleInfo& info, std::unique_ptr<test_framework_transportImplBase> p_main,
                                   Conf& config) :
        ModuleBase(info), p_main(std::move(p_main)), config(config){};

    const std::unique_ptr<test_framework_transportImplBase> p_main;
    const Conf& config;

private:
    friend class LdEverest;
    void init();
    void ready();
};

} // namespace module

#endif // TEST_FRAMEWORK_TRANSPORT_PROVIDER_HPP
