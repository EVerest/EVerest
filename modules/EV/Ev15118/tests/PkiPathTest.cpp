// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include <framework/ModuleAdapter.hpp>

#include "ISO15118_evImpl.hpp"
#include "module_driver.hpp"
#include <everest/iso15118_ev_session/fake_controller.hpp>
#include <everest/iso15118_ev_session/test_contract.hpp>

namespace {

namespace fs = std::filesystem;

using module_driver::Driver;
using module_driver::eventually;
using module_driver::TestImpl;

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
}

// Where the certificate and key paths left empty resolve: under pki_path, else under <etc>/certs.
class PkiPath : public ::testing::Test {
protected:
    PkiPath() {
        fake_controller::reset();
        std::string pattern = (fs::temp_directory_path() / "ev15118-pki-path-XXXXXX").string();
        const char* created = ::mkdtemp(pattern.data());
        if (created == nullptr) {
            throw std::runtime_error("mkdtemp failed for " + pattern);
        }
        root = created;
        pki = root / "pki";
        info.paths.etc = root / "etc";

        conf.device = "lo";
        conf.evcc_id = "EVTESTID01";
        conf.control_mode = "Dynamic";
        conf.supported_protocols = "ISO15118_2";
        conf.cpd_rounds = 1;
        conf.enable_pnc = true;
        conf.allow_pnc_without_tls = true;

        adapter.publish = [this](const std::string&, const std::string& variable, const Value&) {
            if (variable == "v2g_session_finished") {
                std::lock_guard lock(mutex);
                ++finished;
            }
        };
        module_driver::stub_lookups(adapter);
    }

    ~PkiPath() override {
        module.reset();
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    // Writes, below `dir` in the layout the defaults name, a contract leaf with `intermediates` copies of
    // it as its MO intermediates, the same certificate and key as OEM provisioning material, and
    // `password` as the vehicle, contract and OEM key passwords.
    void write_pki(const fs::path& dir, const test_contract::Contract& contract, int intermediates,
                   const std::string& password) {
        write_file(dir / "client/mo/MO_LEAF.pem", contract.chain_pem);
        write_file(dir / "client/mo/MO_LEAF.key", contract.key_pem);
        std::string chain;
        for (int i = 0; i < intermediates; ++i) {
            chain += contract.chain_pem;
        }
        write_file(dir / "ca/mo/INTERMEDIATE_MO_CA_CERTS.pem", chain);
        write_file(dir / "client/oem/OEM_LEAF.pem", contract.chain_pem);
        write_file(dir / "client/oem/OEM_LEAF.key", contract.key_pem);
        for (const auto* file : {"client/vehicle/VEHICLE_LEAF_PASSWORD.txt", "client/mo/MO_LEAF_PASSWORD.txt",
                                 "client/oem/OEM_LEAF_PASSWORD.txt"}) {
            write_file(dir / file, password);
        }
    }

    // Starts the module and runs one session; the Construction it was built with.
    fake_controller::Construction run_session() {
        auto owned = std::make_unique<TestImpl>(&adapter, mod, impl_conf);
        impl = owned.get();
        module = std::make_unique<module::Ev15118>(info, std::move(owned), conf);
        mod.set(module.get());
        Driver{info}.ready(*impl);
        module_driver::set_sane_dc_params(*impl);

        auto mode = types::iso15118::EnergyTransferMode::DC;
        types::iso15118::SelectedPaymentOption payment{};
        double departure_time = 0;
        double e_amount = 0;
        EXPECT_TRUE(impl->handle_start_charging(mode, payment, departure_time, e_amount));
        EXPECT_TRUE(eventually([&] {
            std::lock_guard lock(mutex);
            return finished > 0;
        }));
        const auto constructions = fake_controller::constructions();
        EXPECT_EQ(constructions.size(), 1U);
        return constructions.empty() ? fake_controller::Construction{} : constructions.back();
    }

    const test_contract::Contract contract_a{test_contract::generate("DE-EVT-C00000001-A")};
    const test_contract::Contract contract_b{test_contract::generate("DE-EVT-C00000002-B")};
    fs::path root;
    fs::path pki;
    std::mutex mutex;
    int finished{0};
    module::Conf conf{};
    module::ev::Conf impl_conf{};
    ModuleInfo info{};
    Everest::ModuleAdapter adapter{};
    Everest::PtrContainer<module::Ev15118> mod;
    TestImpl* impl{nullptr};
    std::unique_ptr<module::Ev15118> module;
};

TEST_F(PkiPath, EmptyPathsResolveUnderPkiPath) {
    write_pki(pki, contract_a, 1, "from-pki");
    write_pki(info.paths.etc / "certs", contract_b, 2, "from-etc");
    conf.pki_path = pki.string();

    const auto construction = run_session();

    const auto& tls = construction.config.tls;
    EXPECT_EQ(tls.v2g_root_cert_path, (pki / "ca/v2g/V2G_ROOT_CA.pem").string());
    EXPECT_EQ(tls.client_cert_chain_path, (pki / "client/vehicle/VEHICLE_CERT_CHAIN.pem").string());
    EXPECT_EQ(tls.client_key_path, (pki / "client/vehicle/VEHICLE_LEAF.key").string());
    EXPECT_EQ(tls.client_key_password, "from-pki");
    ASSERT_TRUE(construction.pnc.contract.has_value());
    EXPECT_EQ(construction.pnc.contract->emaid, "DEEVTC00000001A");
    EXPECT_EQ(construction.pnc.contract->sub_certs_der.size(), 1U);
    EXPECT_EQ(construction.pnc.contract->key_password, "from-pki");
    EXPECT_EQ(construction.pnc.oem_prov_key_pem, contract_a.key_pem);
    EXPECT_EQ(construction.pnc.oem_prov_key_password, "from-pki");
}

TEST_F(PkiPath, EmptyPkiPathSelectsTheEtcCerts) {
    const auto certs = info.paths.etc / "certs";
    write_pki(certs, contract_b, 2, "from-etc");

    const auto construction = run_session();

    const auto& tls = construction.config.tls;
    EXPECT_EQ(tls.v2g_root_cert_path, (certs / "ca/v2g/V2G_ROOT_CA.pem").string());
    EXPECT_EQ(tls.client_key_password, "from-etc");
    ASSERT_TRUE(construction.pnc.contract.has_value());
    EXPECT_EQ(construction.pnc.contract->emaid, "DEEVTC00000002B");
    EXPECT_EQ(construction.pnc.contract->sub_certs_der.size(), 2U);
    EXPECT_EQ(construction.pnc.oem_prov_key_pem, contract_b.key_pem);
}

TEST_F(PkiPath, ExplicitPathsWinOverPkiPath) {
    write_pki(pki, contract_a, 1, "from-pki");
    write_file(root / "explicit/chain.pem", contract_b.chain_pem);
    write_file(root / "explicit/contract.key", contract_b.key_pem);
    write_file(root / "explicit/password.txt", "explicit");
    conf.pki_path = pki.string();
    conf.v2g_root_cert_path = (root / "explicit/root.pem").string();
    conf.device_key_path = (root / "explicit/vehicle.key").string();
    conf.device_key_password_path = (root / "explicit/password.txt").string();
    conf.pnc_contract_cert_chain_path = (root / "explicit/chain.pem").string();
    conf.pnc_contract_key_path = (root / "explicit/contract.key").string();

    const auto construction = run_session();

    const auto& tls = construction.config.tls;
    EXPECT_EQ(tls.v2g_root_cert_path, conf.v2g_root_cert_path);
    EXPECT_EQ(tls.client_cert_chain_path, (pki / "client/vehicle/VEHICLE_CERT_CHAIN.pem").string());
    EXPECT_EQ(tls.client_key_path, conf.device_key_path);
    EXPECT_EQ(tls.client_key_password, "explicit");
    ASSERT_TRUE(construction.pnc.contract.has_value());
    EXPECT_EQ(construction.pnc.contract->emaid, "DEEVTC00000002B");
}

} // namespace
