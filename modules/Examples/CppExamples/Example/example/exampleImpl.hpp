// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EXAMPLE_EXAMPLE_IMPL_HPP
#define EXAMPLE_EXAMPLE_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include <generated/interfaces/example/Implementation.hpp>

#include "../Example.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
// insert your custom include headers here
#include <atomic>
#include <condition_variable>
#include <mutex>
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace example {

struct RwConf {
    double current;
    std::string enum_test;
    int enum_test2;
};

struct RwConfUpdate {
    using ConfigChangeResult = Everest::config::ConfigChangeResult;

    virtual ~RwConfUpdate() = default;

    // override in class exampleImpl adding the implementation to exampleImpl.cpp
    // or inline
    // e.g.
    // ConfigChangeResult on_current_changed(const double& value) override {
    //     rw_config.current = value;
    //     return ConfigChangeResult::Accepted();
    // }

    virtual ConfigChangeResult on_current_changed(const double& /* value */) {
        return ConfigChangeResult::Rejected("handler not implemented");
    }
    virtual ConfigChangeResult on_enum_test_changed(const std::string& /* value */) {
        return ConfigChangeResult::Rejected("handler not implemented");
    }
    virtual ConfigChangeResult on_enum_test2_changed(const int& /* value */) {
        return ConfigChangeResult::Rejected("handler not implemented");
    }
};

struct Conf {
    int read_only;

    const double& current;
    const std::string& enum_test;
    const int& enum_test2;

    Conf(const RwConf& rw) : current(rw.current), enum_test(rw.enum_test), enum_test2(rw.enum_test2) {
    }
};

class exampleImpl : public exampleImplBase, public RwConfUpdate {
public:
    exampleImpl() = delete;
    exampleImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<Example>& mod, Conf& config,
                RwConf& rw_config) :
        exampleImplBase(ev, "example"), mod(mod), config(config), rw_config(rw_config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    Everest::config::ConfigChangeResult on_current_changed(const double& new_current);
    Everest::config::ConfigChangeResult on_enum_test_changed(const std::string& new_value);
    Everest::config::ConfigChangeResult on_enum_test2_changed(const int& new_value);
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual bool handle_uses_something(std::string& key) override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<Example>& mod;
    const Conf& config;
    RwConf& rw_config;

    virtual void init() override;
    virtual void ready() override;
    void shutdown();

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    // insert your private definitions here
    RwConf original_config{};
    std::atomic<bool> shutdown_requested{false};
    std::atomic<bool> ready_finished{false};
    std::condition_variable shutdown_cv;
    std::mutex shutdown_mutex;
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace example
} // namespace module

#endif // EXAMPLE_EXAMPLE_IMPL_HPP
