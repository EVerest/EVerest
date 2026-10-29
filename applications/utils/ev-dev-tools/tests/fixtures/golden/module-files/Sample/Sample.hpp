// SPDX-License-Identifier: Apache-2.0
#ifndef SAMPLE_HPP
#define SAMPLE_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 6
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/sample_source/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/sample_source/Interface.hpp>
#include <generated/interfaces/sample_source/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct RwConf {
    int changeable_number;
    std::string changeable_text;
};

struct RwConfUpdate {
    using ConfigChangeResult = Everest::config::ConfigChangeResult;

    virtual ~RwConfUpdate() = default;

    // override in class Sample adding the implementation to Sample.cpp
    // or inline
    //
    // note: these handlers are invoked from a different thread than the one
    // executing your module code, so guard rw_config with a mutex both here
    // and wherever your module accesses config or rw_config
    // e.g.
    // ConfigChangeResult on_changeable_number_changed(const int& value) override {
    //     std::scoped_lock lock(config_mutex);
    //     rw_config.changeable_number = value;
    //     return ConfigChangeResult::Accepted();
    // }

    virtual ConfigChangeResult on_changeable_number_changed(const int& /* value */) {
        return ConfigChangeResult::Rejected("handler not implemented");
    }
    virtual ConfigChangeResult on_changeable_text_changed(const std::string& /* value */) {
        return ConfigChangeResult::Rejected("handler not implemented");
    }
};

struct Conf {
    std::string read_only_setting;

    const int& changeable_number;
    const std::string& changeable_text;

    Conf(const RwConf& rw):
        changeable_number(rw.changeable_number),
        changeable_text(rw.changeable_text) {}
};

class Sample : public Everest::ModuleBase, public RwConfUpdate {
public:
    Sample() = delete;
    Sample(
        const ModuleInfo& info,
        Everest::MqttProvider& mqtt_provider,
        Everest::TelemetryProvider& telemetry,
        std::unique_ptr<sample_sourceImplBase> p_main,
        std::unique_ptr<sample_sourceIntf> r_one_source,
        std::vector<std::unique_ptr<sample_sourceIntf>> r_many_sources,
        Conf& config,
        RwConf& rw_config
    ) :
        ModuleBase(info),
        mqtt(mqtt_provider),
        telemetry(telemetry),
        p_main(std::move(p_main)),
        r_one_source(std::move(r_one_source)),
        r_many_sources(std::move(r_many_sources)),
        config(config),
        rw_config(rw_config)
    {};

    Everest::MqttProvider& mqtt;
    Everest::TelemetryProvider& telemetry;
    const std::unique_ptr<sample_sourceImplBase> p_main;
    const std::unique_ptr<sample_sourceIntf> r_one_source;
    const std::vector<std::unique_ptr<sample_sourceIntf>> r_many_sources;
    const Conf& config;
    RwConf& rw_config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // insert your public definitions here
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    friend class LdEverest;
    void init();
    void ready();
    void shutdown();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    // insert your private definitions here
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1

};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // SAMPLE_HPP
