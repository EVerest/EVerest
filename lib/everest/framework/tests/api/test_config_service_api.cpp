// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <map>
#include <set>
#include <sstream>
#include <string>

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include <config_service_api.hpp>
#include <config_service_type_wrapper.hpp>
#include <everest_api_types/config_service/codec.hpp>
#include <utils/config_service_interface.hpp>

using json = nlohmann::json;
using namespace everest::config;

namespace {

std::string format(const ConfigEntry& e) {
    return config_entry_to_string(e);
}

std::string format(const ConfigurationParameterCharacteristics& c) {
    std::ostringstream s;
    s << "{" << datatype_to_string(c.datatype) << "/" << mutability_to_string(c.mutability);
    if (c.unit)      s << " unit=" << *c.unit;
    if (c.min_value) s << " min=" << *c.min_value;
    if (c.max_value) s << " max=" << *c.max_value;
    s << "}";
    return s.str();
}

std::string format(const ConfigurationParameter& p) {
    return p.name + "=" + format(p.value) + " " + format(p.characteristics);
}

std::string format(const std::optional<Mapping>& m) {
    std::ostringstream s;
    s << m; // uses existing operator<<
    return s.str();
}

std::string format(const std::optional<TelemetryConfig>& t) {
    if (!t) return "(none)";
    return "id=" + std::to_string(t->id);
}

std::string format(const std::optional<std::vector<std::string>>& caps) {
    if (!caps) return "(none)";
    std::ostringstream s;
    s << "[";
    for (size_t i = 0; i < caps->size(); ++i) {
        if (i) s << ", ";
        s << '"' << (*caps)[i] << '"';
    }
    s << "]";
    return s.str();
}

std::string format(const ModuleConfigAccess& a) {
    std::ostringstream s;
    s << std::boolalpha << "{read=" << a.allow_read << " write=" << a.allow_write
      << " set_read_only=" << a.allow_set_read_only << "}";
    return s.str();
}

// Returns a human-readable description of every field that differs between a and b.
// Returns "(identical)" if there are no differences.
std::string module_config_diff(const ModuleConfig& a, const ModuleConfig& b) {
    std::ostringstream os;
    os << std::boolalpha;

    auto row = [&](const std::string& name, const std::string& va, const std::string& vb) {
        if (va != vb)
            os << "  " << name << ": " << va << " → " << vb << "\n";
    };

    // --- scalar fields ---
    row("standalone",       std::to_string(a.standalone),       std::to_string(b.standalone));
    row("module_name",      a.module_name,                      b.module_name);
    row("module_id",        a.module_id,                        b.module_id);
    row("telemetry_enabled",std::to_string(a.telemetry_enabled),std::to_string(b.telemetry_enabled));
    row("capabilities",     format(a.capabilities),             format(b.capabilities));
    row("telemetry_config", format(a.telemetry_config),         format(b.telemetry_config));

    // --- configuration_parameters ---
    if (a.configuration_parameters != b.configuration_parameters) {
        os << "  configuration_parameters:\n";
        std::set<std::string> keys;
        for (auto& [k, _] : a.configuration_parameters) keys.insert(k);
        for (auto& [k, _] : b.configuration_parameters) keys.insert(k);
        for (const auto& key : keys) {
            auto ia = a.configuration_parameters.find(key);
            auto ib = b.configuration_parameters.find(key);
            if (ia == a.configuration_parameters.end()) {
                os << "    [" << key << "] added (" << ib->second.size() << " params)\n";
            } else if (ib == b.configuration_parameters.end()) {
                os << "    [" << key << "] removed\n";
            } else if (ia->second != ib->second) {
                os << "    [" << key << "]:\n";
                const auto& pa = ia->second;
                const auto& pb = ib->second;
                for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
                    if      (i >= pa.size()) os << "      + " << format(pb[i]) << "\n";
                    else if (i >= pb.size()) os << "      - " << format(pa[i]) << "\n";
                    else if (!(pa[i] == pb[i])) os << "      " << format(pa[i]) << "\n"
                                                << "      " << format(pb[i]) << "\n";
                }
            }
        }
    }

    // --- connections ---
    if (a.connections != b.connections) {
        os << "  connections:\n";
        std::set<std::string> keys;
        for (auto& [k, _] : a.connections) keys.insert(k);
        for (auto& [k, _] : b.connections) keys.insert(k);
        for (const auto& key : keys) {
            auto ia = a.connections.find(key);
            auto ib = b.connections.find(key);
            if      (ia == a.connections.end()) os << "    [" << key << "] added\n";
            else if (ib == b.connections.end()) os << "    [" << key << "] removed\n";
            else if (ia->second != ib->second) {
                os << "    [" << key << "]: " << ia->second.size()
                   << " → " << ib->second.size() << " fulfillments\n";
                for (size_t i = 0; i < std::max(ia->second.size(), ib->second.size()); ++i) {
                    auto fmt_f = [](const Fulfillment& f) {
                        return f.module_id + "/" + f.implementation_id +
                               " (req=" + f.requirement.id + "[" + std::to_string(f.requirement.index) + "])";
                    };
                    if      (i >= ia->second.size()) os << "      + " << fmt_f(ib->second[i]) << "\n";
                    else if (i >= ib->second.size()) os << "      - " << fmt_f(ia->second[i]) << "\n";
                    else if (!(ia->second[i] == ib->second[i]))
                        os << "      " << fmt_f(ia->second[i]) << " → " << fmt_f(ib->second[i]) << "\n";
                }
            }
        }
    }

    // --- mapping ---
    if (!(a.mapping == b.mapping)) {
        os << "  mapping:\n";
        row("  mapping.module", format(a.mapping.module), format(b.mapping.module));
        std::set<std::string> keys;
        for (auto& [k, _] : a.mapping.implementations) keys.insert(k);
        for (auto& [k, _] : b.mapping.implementations) keys.insert(k);
        for (const auto& key : keys) {
            auto ia = a.mapping.implementations.find(key);
            auto ib = b.mapping.implementations.find(key);
            if      (ia == a.mapping.implementations.end()) os << "    impl[" << key << "] added\n";
            else if (ib == b.mapping.implementations.end()) os << "    impl[" << key << "] removed\n";
            else if (!(ia->second == ib->second))
                row("  mapping.impl[" + key + "]", format(ia->second), format(ib->second));
        }
    }

    // --- access ---
    if (!(a.access == b.access)) {
        const auto& ca = a.access.config;
        const auto& cb = b.access.config;
        os << "  access:\n";
        if (!ca && cb) { os << "    config: (none) → set\n"; }
        else if (ca && !cb) { os << "    config: set → (none)\n"; }
        else if (ca && cb) {
            os << std::boolalpha;
            row("  access.config.allow_global_read",  std::to_string(ca->allow_global_read),  std::to_string(cb->allow_global_read));
            row("  access.config.allow_global_write", std::to_string(ca->allow_global_write), std::to_string(cb->allow_global_write));
            row("  access.config.allow_set_read_only",std::to_string(ca->allow_set_read_only),std::to_string(cb->allow_set_read_only));
            std::set<std::string> mkeys;
            for (auto& [k, _] : ca->modules) mkeys.insert(k);
            for (auto& [k, _] : cb->modules) mkeys.insert(k);
            for (const auto& key : mkeys) {
                auto ima = ca->modules.find(key);
                auto imb = cb->modules.find(key);
                if      (ima == ca->modules.end()) os << "    access.modules[" << key << "] added: "   << format(imb->second) << "\n";
                else if (imb == cb->modules.end()) os << "    access.modules[" << key << "] removed\n";
                else if (!(ima->second == imb->second))
                    row("  access.modules[" + key + "]", format(ima->second), format(imb->second));
            }
        }
    }

    const auto result = os.str();
    return result.empty() ? "(identical)\n" : result;
}

} // namespace

TEST_CASE("ConfigServiceAPI::get_config_value", "[config_service]") {
    ModuleConfig config;

    config.module_id = "module_id";
    config.module_name = "Module Name";
    config.standalone = true;
    config.capabilities = std::nullopt;
    config.telemetry_enabled = true;
    config.telemetry_config = std::make_optional<TelemetryConfig>(5);

    ConfigurationParameterCharacteristics characteristics1;
    characteristics1.datatype = Datatype::Integer;
    characteristics1.mutability = Mutability::ReadWrite;
    characteristics1.unit = "ms";

    ConfigurationParameterCharacteristics characteristics2;
    characteristics2.datatype = Datatype::String;
    characteristics2.mutability = Mutability::ReadOnly;

    ConfigurationParameterCharacteristics characteristics4;
    characteristics4.datatype = Datatype::Decimal;
    characteristics4.mutability = Mutability::ReadWrite;

    ConfigurationParameterCharacteristics characteristics5;
    characteristics5.datatype = Datatype::Boolean;
    characteristics5.mutability = Mutability::WriteOnly;

    ConfigurationParameter param1;
    param1.name = "integer_param";
    param1.value = 10;
    param1.characteristics = characteristics1;

    ConfigurationParameter param2;
    param2.name = "string_param";
    param2.value = std::string("example_value");
    param2.characteristics = characteristics2;

    ConfigurationParameter param4;
    param4.name = "decimal_param";
    param4.value = 42.23;
    param4.characteristics = characteristics4;

    ConfigurationParameter param5;
    param5.name = "boolean_param";
    param5.value = true;
    param5.characteristics = characteristics5;

    config.configuration_parameters["!module"].push_back({param1});
    config.configuration_parameters["impl_1"].push_back({param2});
    config.configuration_parameters["!module"].push_back({param4});
    config.configuration_parameters["!module"].push_back({param5});

    Fulfillment f1{"module_a", "impl_a1", {"conn1", 0}};
    Fulfillment f2{"module_a", "impl_a2", {"conn2", 0}};
    Fulfillment f3{"module_b", "impl_b1", {"conn3", 0}};
    Fulfillment f4{"module_c", "impl_b2", {"conn4", 0}};
    Fulfillment f5{"module_d", "impl_b2", {"conn4", 1}};
    Fulfillment f6{"module_e", "impl_b2", {"conn4", 2}};

    config.connections = {
        {"conn1", {f1}},
        {"conn2", {f2}},
        {"conn3", {f3}},
        {"conn4", {f4, f5, f6}},
    };

    ModuleTierMappings mtm;
    mtm.module = std::make_optional<Mapping>(1, 2);
    mtm.implementations = {
        {"impl_1", std::make_optional<Mapping>(3)},
        {"impl_2", std::make_optional<Mapping>(6)},
        {"impl_3", std::make_optional<Mapping>(4, 5)},
    };
    config.mapping = mtm;

    ModuleConfigAccess mca;
    mca.allow_read = true;
    mca.allow_write = false;
    mca.allow_set_read_only = true;
    ConfigAccess ca;
    ca.allow_global_read = false;
    ca.allow_global_write = true;
    ca.allow_set_read_only = false;
    ca.modules["other_module_id"] = mca;
    config.access = {ca};

    SECTION("ModuleConfig round-trip conversion") {
        auto external_config = Everest::api::types::config_service::to_external_api(config);
        auto serialized_config = serialize(external_config);
        auto deserialized_external_config = everest::lib::API::V1_0::types::config_service::deserialize<everest::lib::API::V1_0::types::config_service::ModuleConfiguration>(serialized_config);
        auto internal_config = Everest::api::types::config_service::to_internal_api(deserialized_external_config);

        CHECK(internal_config == config);
    }
}