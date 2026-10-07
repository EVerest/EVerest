// SPDX-License-Identifier: Apache-2.0
#ifndef MAIN_SAMPLE_SOURCE_IMPL_HPP
#define MAIN_SAMPLE_SOURCE_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 6
//

#include <generated/interfaces/sample_source/Implementation.hpp>

#include "../Sample.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
// insert your custom include headers here
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace main {

struct RwConf {
    double impl_changeable;
};

struct RwConfUpdate {
    using ConfigChangeResult = Everest::config::ConfigChangeResult;

    virtual ~RwConfUpdate() = default;

    // override in class sample_sourceImpl adding the implementation to sample_sourceImpl.cpp
    // or inline
    //
    // note: these handlers are invoked from a different thread than the one
    // executing your module code, so guard rw_config with a mutex both here
    // and wherever your module accesses config or rw_config
    // e.g.
    // ConfigChangeResult on_impl_changeable_changed(const double& value) override {
    //     std::scoped_lock lock(config_mutex);
    //     rw_config.impl_changeable = value;
    //     return ConfigChangeResult::Accepted();
    // }

    virtual ConfigChangeResult on_impl_changeable_changed(const double& /* value */) {
        return ConfigChangeResult::Rejected("handler not implemented");
    }
};

struct Conf {
    bool impl_setting;

    const double& impl_changeable;

    Conf(const RwConf& rw):
        impl_changeable(rw.impl_changeable) {}
};

class sample_sourceImpl : public sample_sourceImplBase, public RwConfUpdate {
public:
    sample_sourceImpl() = delete;
    sample_sourceImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<Sample> &mod, Conf& config, RwConf& rw_config) :
        sample_sourceImplBase(ev, "main"),
        mod(mod),
        config(config),
        rw_config(rw_config)
    {};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual types::sample::Reading handle_configure(types::sample::Mood& mood, double& interval, bool& enabled, std::vector<types::sample::Mood>& allowed) override;
    virtual void handle_reset() override;
    virtual bool handle_store(std::variant<std::nullptr_t, Array, Object, bool, double, int, std::string>& value) override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<Sample>& mod;
    const Conf& config;
    RwConf& rw_config;

    virtual void init() override;
    virtual void ready() override;
    void shutdown() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    // insert your private definitions here
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace main
} // namespace module

#endif // MAIN_SAMPLE_SOURCE_IMPL_HPP
