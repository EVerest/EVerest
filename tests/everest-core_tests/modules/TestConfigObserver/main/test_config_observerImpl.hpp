// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_TEST_CONFIG_OBSERVER_IMPL_HPP
#define MAIN_TEST_CONFIG_OBSERVER_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 4
//

#include <generated/interfaces/test_config_observer/Implementation.hpp>

#include "../TestConfigObserver.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
#include <everest/util/async/monitor.hpp>
#include <nlohmann/json.hpp>
#include <vector>
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace main {

struct Conf {};

class test_config_observerImpl : public test_config_observerImplBase {
public:
    test_config_observerImpl() = delete;
    test_config_observerImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<TestConfigObserver>& mod,
                             Conf& config) :
        test_config_observerImplBase(ev, "main"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual Array handle_get_received() override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<TestConfigObserver>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;
    void shutdown() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    everest::lib::util::monitor<std::vector<nlohmann::json>> received;
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace main
} // namespace module

#endif // MAIN_TEST_CONFIG_OBSERVER_IMPL_HPP
