// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <everest/utils/yaml_loader.hpp>
#include <framework/ModuleAdapter.hpp>
#include <utils/config.hpp>

#include <iso15118/ev/session/sap_offer.hpp>

#include "ISO15118_evImpl.hpp"
#include "fake_controller.hpp"
#include "module_driver.hpp"

namespace {

namespace fs = std::filesystem;

const fs::path source_root{EVEREST_SOURCE_DIR};

using module_driver::Driver;
using module_driver::TestImpl;

// The manager finds a manifest at <modules_dir>/<name>/manifest.yaml; the source tree groups
// modules by category, so this links every module of the tree into one flat directory.
class ConfigValidation {
public:
    ConfigValidation() {
        modules_dir = fs::temp_directory_path() / ("ev15118-config-defaults-" + std::to_string(::getpid()));
        fs::remove_all(modules_dir);
        fs::create_directories(modules_dir);
        for (const auto& category : fs::directory_iterator(source_root / "modules")) {
            if (not category.is_directory()) {
                continue;
            }
            for (const auto& module_dir : fs::directory_iterator(category.path())) {
                if (fs::exists(module_dir.path() / "manifest.yaml")) {
                    fs::create_directory_symlink(module_dir.path(), modules_dir / module_dir.path().filename());
                }
            }
        }
    }

    ~ConfigValidation() {
        fs::remove_all(modules_dir);
    }

    Everest::ValidatedModuleConfigurations validate(const nlohmann::json& config) const {
        Everest::ConfigParseSettings settings;
        settings.schemas_dir = source_root / "lib/everest/framework/schemas";
        settings.interfaces_dir = source_root / "interfaces";
        settings.types_dir = source_root / "types";
        settings.errors_dir = source_root / "errors";
        settings.modules_dir = modules_dir;
        settings.configs_dir = source_root / "config";
        return Everest::validate_module_configs(settings, config);
    }

private:
    fs::path modules_dir;
};

std::string resolved_string(const Everest::ValidatedModuleConfigurations& validated, const std::string& module_id,
                            const std::string& name) {
    for (const auto& parameter : validated.module_configs.at(module_id).configuration_parameters.at("!module")) {
        if (parameter.name == name) {
            return std::get<std::string>(parameter.value);
        }
    }
    throw std::out_of_range("no resolved config parameter " + name);
}

// The SAP offer of the first session a module with these protocol settings starts.
std::vector<iso15118::ProtocolId> offered_protocols(const Everest::ValidatedModuleConfigurations& validated) {
    fake_controller::reset();
    module::Conf conf{};
    conf.device = "lo";
    conf.evcc_id = "EVTESTID01";
    conf.control_mode = "Dynamic";
    conf.cpd_rounds = 1;
    conf.supported_protocols = resolved_string(validated, "car", "supported_protocols");

    Everest::ModuleAdapter adapter{};
    adapter.publish = [](const std::string&, const std::string&, const Value&) {};
    module_driver::stub_lookups(adapter);
    ModuleInfo info{};
    module::ev::Conf impl_conf{};
    Everest::PtrContainer<module::Ev15118> mod;
    auto owned = std::make_unique<TestImpl>(&adapter, mod, impl_conf);
    auto* impl = owned.get();
    auto module = std::make_unique<module::Ev15118>(info, std::move(owned), conf);
    mod.set(module.get());
    Driver{info}.ready(*impl);
    module_driver::set_sane_dc_params(*impl);

    auto mode = types::iso15118::EnergyTransferMode::DC;
    types::iso15118::SelectedPaymentOption payment{};
    double departure_time = 0;
    double e_amount = 0;
    EXPECT_TRUE(impl->handle_start_charging(mode, payment, departure_time, e_amount));
    EXPECT_TRUE(module_driver::eventually([] { return not fake_controller::constructions().empty(); }));
    module.reset();
    const auto constructions = fake_controller::constructions();
    EXPECT_EQ(constructions.size(), 1u);
    return constructions.empty() ? std::vector<iso15118::ProtocolId>{} : constructions.front().protocols;
}

nlohmann::json ev15118_config(const nlohmann::json& config_module) {
    return {{"active_modules", {{"car", {{"module", "Ev15118"}, {"config_module", config_module}}}}}};
}

TEST(ConfigDefaults, NothingIsOfferedByDefault) {
    const ConfigValidation validation;
    const auto validated = validation.validate(ev15118_config(nlohmann::json::object()));
    EXPECT_EQ(resolved_string(validated, "car", "supported_protocols"), "");
}

TEST(ConfigDefaults, ConfigListingOnlyIso2OffersIso2Alone) {
    const ConfigValidation validation;
    const auto validated = validation.validate(ev15118_config({{"supported_protocols", "ISO15118_2"}}));
    EXPECT_EQ(offered_protocols(validated), std::vector<iso15118::ProtocolId>{iso15118::ProtocolId::ISO15118_2});
}

TEST(ConfigDefaults, ListOrderIsTheSapPriority) {
    const ConfigValidation validation;
    const auto validated = validation.validate(ev15118_config({{"supported_protocols", "ISO15118_2,ISO15118_20"}}));
    const auto protocols = offered_protocols(validated);
    EXPECT_EQ(protocols,
              (std::vector<iso15118::ProtocolId>{iso15118::ProtocolId::ISO15118_2, iso15118::ProtocolId::ISO15118_20}));

    iso15118::ev::session::SapOfferInput input;
    input.supported_protocols = protocols;
    const auto offer = iso15118::ev::session::build_sap_offer(input);
    ASSERT_EQ(offer.size(), 2u);
    EXPECT_EQ(offer[0].entry.protocol_namespace, iso15118::ISO2_NAMESPACE);
    EXPECT_EQ(offer[0].entry.priority, 1);
    EXPECT_EQ(offer[1].protocol, iso15118::ProtocolId::ISO15118_20);
    EXPECT_EQ(offer[1].entry.priority, 2);
}

TEST(ConfigDefaults, SpacesAroundEntriesAreIgnored) {
    const ConfigValidation validation;
    const auto validated = validation.validate(ev15118_config({{"supported_protocols", " ISO15118_20 , DIN70121"}}));
    EXPECT_EQ(offered_protocols(validated),
              (std::vector<iso15118::ProtocolId>{iso15118::ProtocolId::ISO15118_20, iso15118::ProtocolId::DIN70121}));
}

const fs::path aux_config_dir{source_root / "tests/ocpp_tests/test_sets/everest-aux/config"};

// The config_module of every Ev15118 entry in the file, by module id.
std::vector<std::pair<std::string, nlohmann::json>> ev15118_entries(const fs::path& path) {
    std::vector<std::pair<std::string, nlohmann::json>> entries;
    const nlohmann::json config = Everest::load_yaml(path);
    if (not config.contains("active_modules")) {
        return entries;
    }
    for (const auto& [module_id, module_config] : config.at("active_modules").items()) {
        if (module_config.value("module", "") == "Ev15118") {
            entries.emplace_back(module_id, module_config.value("config_module", nlohmann::json::object()));
        }
    }
    return entries;
}

// A shipped Ev15118 entry written against a -20 default would offer nothing.
TEST(ConfigDefaults, EveryShippedConfigOffersAProtocol) {
    const ConfigValidation validation;
    int checked = 0;
    for (const auto& dir : {source_root / "config", aux_config_dir}) {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() != ".yaml") {
                continue;
            }
            for (const auto& [module_id, config_module] : ev15118_entries(entry.path())) {
                const auto validated = validation.validate(ev15118_config(config_module));
                EXPECT_FALSE(offered_protocols(validated).empty()) << entry.path() << " " << module_id;
                ++checked;
            }
        }
    }
    EXPECT_GT(checked, 0);
}

// The configs whose EV offered -20 under the old default, and still offers it first. One that also
// lists -2 or DIN still offers a protocol without -20, which the check above cannot see.
class ShippedIso20Config : public ::testing::TestWithParam<fs::path> {};

TEST_P(ShippedIso20Config, StillOffersIso20First) {
    const ConfigValidation validation;
    const auto entries = ev15118_entries(GetParam());
    ASSERT_FALSE(entries.empty()) << GetParam();
    for (const auto& [module_id, config_module] : entries) {
        const auto protocols = offered_protocols(validation.validate(ev15118_config(config_module)));
        ASSERT_FALSE(protocols.empty()) << GetParam() << " " << module_id;
        EXPECT_EQ(protocols.front(), iso15118::ProtocolId::ISO15118_20) << GetParam() << " " << module_id;
    }
}

INSTANTIATE_TEST_SUITE_P(
    Pinned, ShippedIso20Config,
    ::testing::Values(
        source_root / "config/config-CB-EVAL-EV.yaml", source_root / "config/config-CB-EVAL-MCS-EV.yaml",
        source_root / "config/config-sil-ac-bpt-d20.yaml", source_root / "config/config-sil-ac-d20-der-iec.yaml",
        source_root / "config/config-sil-ac-d20-der-sae.yaml", source_root / "config/config-sil-ac-d20-plaintext.yaml",
        source_root / "config/config-sil-ac-d20.yaml", source_root / "config/config-sil-ac-der-d20.yaml",
        source_root / "config/config-sil-dc-bpt-d20.yaml", source_root / "config/config-sil-dc-d20-plaintext.yaml",
        source_root / "config/config-sil-dc-d20-tls.yaml", source_root / "config/config-sil-dc-d20.yaml",
        source_root / "config/config-sil-dc-evsev2g.yaml", source_root / "config/config-sil-dc-isomux.yaml",
        source_root / "config/config-sil-mcs.yaml", aux_config_dir / "everest-config-ocpp201-sil-dc-d20-eim.yaml",
        aux_config_dir / "everest-config-ocpp201-sil-dc-d20.yaml",
        aux_config_dir / "everest-config-ocppmulti-sil-dc-d20-eim.yaml"));

class SaeJ2847SilConfig : public ::testing::TestWithParam<std::string> {};

TEST_P(SaeJ2847SilConfig, IsMarkedUnsupportedAndStillValidates) {
    const auto path = source_root / "config" / GetParam();
    std::ifstream file(path);
    std::string first_line;
    ASSERT_TRUE(std::getline(file, first_line)) << path;
    EXPECT_EQ(first_line.rfind("# UNSUPPORTED:", 0), 0u) << first_line;

    const ConfigValidation validation;
    EXPECT_NO_THROW(validation.validate(nlohmann::json(Everest::load_yaml(path))));
}

INSTANTIATE_TEST_SUITE_P(BothModes, SaeJ2847SilConfig,
                         ::testing::Values("config-sil-dc-sae-v2g.yaml", "config-sil-dc-sae-v2h.yaml"));

} // namespace
