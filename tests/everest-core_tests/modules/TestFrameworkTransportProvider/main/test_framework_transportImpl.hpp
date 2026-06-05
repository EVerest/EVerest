// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef TEST_FRAMEWORK_TRANSPORT_PROVIDER_TEST_FRAMEWORK_TRANSPORT_IMPL_HPP
#define TEST_FRAMEWORK_TRANSPORT_PROVIDER_TEST_FRAMEWORK_TRANSPORT_IMPL_HPP

#include <generated/interfaces/test_framework_transport/Implementation.hpp>

#include "../TestFrameworkTransportProvider.hpp"

namespace module {
namespace main {

struct Conf {};

class test_framework_transportImpl : public test_framework_transportImplBase {
public:
    test_framework_transportImpl() = delete;
    test_framework_transportImpl(Everest::ModuleAdapter* ev,
                                 const Everest::PtrContainer<TestFrameworkTransportProvider>& mod, Conf& config) :
        test_framework_transportImplBase(ev, "main"), mod(mod), config(config){};

private:
    const Everest::PtrContainer<TestFrameworkTransportProvider>& mod;
    const Conf& config;

    void init() override;
    void ready() override;
    std::string handle_roundtrip(std::string& payload) override;
};

} // namespace main
} // namespace module

#endif // TEST_FRAMEWORK_TRANSPORT_PROVIDER_TEST_FRAMEWORK_TRANSPORT_IMPL_HPP
