// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/smart_ptr/make_shared.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>

#include <framework/ModuleAdapter.hpp>

#include "ISO15118_evImpl.hpp"
#include "fake_controller.hpp"
#include "module_driver.hpp"
#include "test_contract.hpp"

namespace {

namespace fs = std::filesystem;

// The eMAID is the leaf CommonName without dashes.
constexpr const char* COMMON_NAME_A = "DE-EVT-C00000001-A";
constexpr const char* EMAID_A = "DEEVTC00000001A";
constexpr const char* COMMON_NAME_B = "DE-EVT-C00000002-B";
constexpr const char* EMAID_B = "DEEVTC00000002B";

using module_driver::Driver;
using module_driver::eventually;
using module_driver::TestImpl;

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
}

// The first base64 line of a PEM block: a log line carrying it carries key bytes.
std::string first_body_line(const std::string& pem) {
    const auto start = pem.find('\n') + 1;
    return pem.substr(start, pem.find('\n', start) - start);
}

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Every EVLOG line written while it lives, each prefixed with its severity number.
class LogCapture {
public:
    LogCapture() {
        auto backend = boost::make_shared<boost::log::sinks::text_ostream_backend>();
        backend->add_stream(stream);
        backend->auto_flush(true);
        sink = boost::make_shared<Sink>(backend);
        sink->set_formatter(boost::log::expressions::stream
                            << boost::log::expressions::attr<Everest::Logging::severity_level>("Severity") << ' '
                            << boost::log::expressions::smessage);
        boost::log::core::get()->add_sink(sink);
    }
    ~LogCapture() {
        boost::log::core::get()->remove_sink(sink);
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    std::string text() {
        auto backend = sink->locked_backend();
        return stream->str();
    }

    // Whether one warning or worse carries every one of `words`.
    bool warned(std::initializer_list<std::string> words) {
        std::istringstream lines(text());
        std::string line;
        while (std::getline(lines, line)) {
            int severity = -1;
            std::istringstream(line) >> severity;
            if (severity >= Everest::Logging::warning and
                std::all_of(words.begin(), words.end(),
                            [&line](const std::string& word) { return line.find(word) != std::string::npos; })) {
                return true;
            }
        }
        return false;
    }

private:
    using Sink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;
    boost::shared_ptr<std::ostringstream> stream{boost::make_shared<std::ostringstream>()};
    boost::shared_ptr<Sink> sink;
};

class ContractStore : public ::testing::Test {
protected:
    mode_t saved_umask = ::umask(022);

    ContractStore() {
        fake_controller::reset();
        std::string pattern = (fs::temp_directory_path() / "ev15118-contract-store-XXXXXX").string();
        root = ::mkdtemp(pattern.data());
        store = root / "store";
        write_file(root / "initial_chain.pem", contract_a.chain_pem);
        write_file(root / "initial.key", contract_a.key_pem);

        conf.device = "lo";
        conf.evcc_id = "EVTESTID01";
        conf.control_mode = "Dynamic";
        conf.supported_protocols = "ISO15118_2";
        conf.cpd_rounds = 1;
        conf.enable_pnc = true;
        conf.allow_pnc_without_tls = true;
        conf.pnc_contract_cert_chain_path = (root / "initial_chain.pem").string();
        conf.pnc_contract_key_path = (root / "initial.key").string();
        conf.pnc_contract_key_password_path = (root / "no_password.txt").string();
        conf.pnc_oem_prov_cert_path = (root / "no_oem.pem").string();
        conf.pnc_contract_store_dir = store.string();

        adapter.publish = [this](const std::string&, const std::string& variable, const Value&) {
            if (variable == "v2g_session_finished") {
                std::lock_guard lock(mutex);
                ++finished;
            }
        };
        module_driver::stub_lookups(adapter);
    }

    ~ContractStore() override {
        module.reset();
        ::umask(saved_umask);
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    void start_module() {
        auto owned = std::make_unique<TestImpl>(&adapter, mod, impl_conf);
        impl = owned.get();
        module = std::make_unique<module::Ev15118>(info, std::move(owned), conf);
        mod.set(module.get());
        Driver{info}.ready(*impl);
        module_driver::set_sane_dc_params(*impl);
    }

    // Runs one complete session with `body` inside its loop; the PnC material it was built with.
    iso15118::ev::d2::PnCConfig run_session(fake_controller::LoopBody body = nullptr) {
        const auto before = fake_controller::constructions().size();
        int finished_before = 0;
        {
            std::lock_guard lock(mutex);
            finished_before = finished;
        }
        fake_controller::set_loop_body(std::move(body));
        auto mode = types::iso15118::EnergyTransferMode::DC;
        types::iso15118::SelectedPaymentOption payment{};
        double departure_time = 0;
        double e_amount = 0;
        EXPECT_TRUE(impl->handle_start_charging(mode, payment, departure_time, e_amount));
        EXPECT_TRUE(eventually([&] {
            std::lock_guard lock(mutex);
            return finished > finished_before;
        }));
        const auto constructions = fake_controller::constructions();
        EXPECT_EQ(constructions.size(), before + 1);
        return constructions.empty() ? iso15118::ev::d2::PnCConfig{} : constructions.back().pnc;
    }

    static fake_controller::LoopBody installing(const std::string& chain, const std::string& key,
                                                const std::string& emaid) {
        return [chain, key, emaid] { fake_controller::install_contract(chain, key, emaid); };
    }

    void write_store(const std::string& chain, const std::string& key, const std::string& emaid) {
        fs::create_directories(store);
        write_file(store / "CONTRACT_CHAIN.pem", chain);
        write_file(store / "CONTRACT_LEAF.key", key);
        write_file(store / "EMAID.txt", emaid);
    }

    const test_contract::Contract contract_a{test_contract::generate(COMMON_NAME_A)};
    const test_contract::Contract contract_b{test_contract::generate(COMMON_NAME_B)};
    // No log line may carry it.
    const std::string key_b_line{first_body_line(contract_b.key_pem)};
    LogCapture logs;
    fs::path root;
    fs::path store;
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

TEST_F(ContractStore, InstalledContractIsStoredAndPresentedByTheNextSession) {
    // A CertificateInstallation delivers the contract key as SEC1.
    ASSERT_EQ(contract_b.key_pem.rfind("-----BEGIN EC PRIVATE KEY-----", 0), 0U);
    start_module();
    EXPECT_EQ(run_session(installing(contract_b.chain_pem, contract_b.key_pem, EMAID_B)).contract.value().emaid,
              EMAID_A);

    EXPECT_EQ(read_file(store / "CONTRACT_CHAIN.pem"), contract_b.chain_pem);
    EXPECT_EQ(read_file(store / "CONTRACT_LEAF.key"), contract_b.key_pem);
    EXPECT_EQ(read_file(store / "EMAID.txt"), EMAID_B);
    EXPECT_EQ(fs::status(store / "CONTRACT_LEAF.key").permissions() & fs::perms::all,
              fs::perms::owner_read | fs::perms::owner_write);

    const auto next = run_session();
    EXPECT_EQ(next.contract.value().emaid, EMAID_B);
    EXPECT_EQ(next.contract.value().key_pem, contract_b.key_pem);
    EXPECT_FALSE(next.contract.value().key_password.has_value());

    const auto log = logs.text();
    EXPECT_NE(log.find(EMAID_B), std::string::npos);
    EXPECT_NE(log.find(store.string()), std::string::npos);
    EXPECT_EQ(log.find(key_b_line), std::string::npos);
    EXPECT_EQ(log.find("PRIVATE KEY"), std::string::npos);
}

TEST_F(ContractStore, StoredContractWinsOverTheConfiguredOneAfterARestart) {
    write_store(contract_b.chain_pem, contract_b.key_pem, EMAID_B);
    start_module();
    const auto pnc = run_session();
    EXPECT_EQ(pnc.contract.value().emaid, EMAID_B);
    EXPECT_EQ(pnc.contract.value().key_pem, contract_b.key_pem);
}

TEST_F(ContractStore, EmptyStoreDirectoryWritesNothing) {
    conf.pnc_contract_store_dir.clear();
    const auto cwd = fs::current_path();
    const auto scratch = root / "cwd";
    fs::create_directories(scratch);
    fs::current_path(scratch);
    start_module();
    run_session(installing(contract_b.chain_pem, contract_b.key_pem, EMAID_B));
    const auto next = run_session();
    fs::current_path(cwd);

    EXPECT_TRUE(fs::is_empty(scratch));
    EXPECT_FALSE(fs::exists(store));
    EXPECT_EQ(next.contract.value().emaid, EMAID_A);
    EXPECT_EQ(logs.text().find(key_b_line), std::string::npos);
}

TEST_F(ContractStore, MissingStoredKeyFallsBackToTheConfiguredContract) {
    write_store(contract_b.chain_pem, contract_b.key_pem, EMAID_B);
    fs::remove(store / "CONTRACT_LEAF.key");
    start_module();
    const auto pnc = run_session();
    EXPECT_EQ(pnc.contract.value().emaid, EMAID_A);
    EXPECT_EQ(pnc.contract.value().key_pem, contract_a.key_pem);
    EXPECT_TRUE(logs.warned({"CONTRACT_LEAF.key is missing", "this session uses the configured contract material"}));
}

// Before the first installation the store is empty, which is no reason to warn.
TEST_F(ContractStore, EmptyStoreFallsBackToTheConfiguredContractWithoutAWarning) {
    start_module();
    EXPECT_EQ(run_session().contract.value().emaid, EMAID_A);
    EXPECT_NE(logs.text().find("no Plug&Charge contract stored in " + store.string() + " yet"), std::string::npos);
    EXPECT_FALSE(logs.warned({store.string()}));
}

// PnC needs TLS here, so the session presents no contract and has no use for the store.
TEST_F(ContractStore, StoreIsNotReadWhenTheSessionCannotOfferPnc) {
    write_store(contract_b.chain_pem, contract_a.key_pem, EMAID_B);
    conf.allow_pnc_without_tls = false;
    start_module();
    run_session();
    EXPECT_EQ(logs.text().find(store.string()), std::string::npos);
}

// Reading a directory fails with EISDIR after it opens, which libstdc++ reports by throwing.
TEST_F(ContractStore, UnreadableStoredChainFallsBackToTheConfiguredContract) {
    write_store(contract_b.chain_pem, contract_b.key_pem, EMAID_B);
    fs::remove(store / "CONTRACT_CHAIN.pem");
    fs::create_directory(store / "CONTRACT_CHAIN.pem");
    start_module();
    const auto pnc = run_session();
    EXPECT_EQ(pnc.contract.value().emaid, EMAID_A);
    EXPECT_EQ(pnc.contract.value().key_pem, contract_a.key_pem);
    EXPECT_TRUE(logs.warned({"cannot read CONTRACT_CHAIN.pem", "this session uses the configured contract material"}));
}

TEST_F(ContractStore, KeyOfAnotherContractFallsBackToTheConfiguredContract) {
    write_store(contract_b.chain_pem, contract_a.key_pem, EMAID_B);
    start_module();
    const auto pnc = run_session();
    EXPECT_EQ(pnc.contract.value().emaid, EMAID_A);
    EXPECT_EQ(pnc.contract.value().key_pem, contract_a.key_pem);
    EXPECT_TRUE(logs.warned(
        {"the key is not the private key of the contract leaf", "this session uses the configured contract material"}));
}

// An overwrite interrupted after the key was replaced leaves the old chain and eMAID beside the
// new key.
TEST_F(ContractStore, InterruptedOverwriteFallsBackToTheConfiguredContract) {
    start_module();
    run_session(installing(contract_a.chain_pem, contract_a.key_pem, EMAID_A));
    write_file(store / "CONTRACT_LEAF.key", contract_b.key_pem);

    const auto pnc = run_session();
    EXPECT_EQ(pnc.contract.value().emaid, EMAID_A);
    EXPECT_EQ(pnc.contract.value().key_pem, contract_a.key_pem);
    EXPECT_TRUE(logs.warned(
        {"the key is not the private key of the contract leaf", "this session uses the configured contract material"}));
}

TEST_F(ContractStore, EmaidOfAnotherContractFallsBackToTheConfiguredContract) {
    write_store(contract_b.chain_pem, contract_b.key_pem, EMAID_A);
    start_module();
    EXPECT_EQ(run_session().contract.value().emaid, EMAID_A);
    EXPECT_TRUE(logs.warned({"EMAID.txt does not name the eMAID of the stored contract leaf",
                             "this session uses the configured contract material"}));
}

// The store directory cannot be created where a regular file sits.
TEST_F(ContractStore, StoreFailureLeavesTheInstalledSessionRunning) {
    write_file(store, "not a directory");
    bool continued = false;
    start_module();
    run_session([this, &continued] {
        fake_controller::install_contract(contract_b.chain_pem, contract_b.key_pem, EMAID_B);
        continued = true;
    });

    EXPECT_TRUE(continued);
    EXPECT_EQ(read_file(store), "not a directory");
    for (const auto& entry : fs::directory_iterator(root)) {
        EXPECT_EQ(entry.path().filename().string().find("CONTRACT"), std::string::npos);
    }
    EXPECT_EQ(run_session().contract.value().emaid, EMAID_A);
    const auto log = logs.text();
    EXPECT_NE(log.find(store.string()), std::string::npos);
    EXPECT_EQ(log.find("session failed"), std::string::npos);
    EXPECT_EQ(log.find(key_b_line), std::string::npos);
}

// The paused session's contract is in memory only, as when storing it failed.
TEST_F(ContractStore, ResumedSessionPresentsTheContractItsPausedSessionInstalled) {
    write_file(store, "not a directory");
    start_module();
    run_session([this] {
        fake_controller::install_contract(contract_b.chain_pem, contract_b.key_pem, EMAID_B);
        fake_controller::set_paused_session(iso15118::ev::config::PausedSession{
            {1}, iso15118::ProtocolId::ISO15118_2, iso15118::v2gtp::Security::NO_TRANSPORT_SECURITY});
    });
    fake_controller::set_paused_session(std::nullopt);

    std::optional<iso15118::ev::d2::PnCConfig> resumed;
    {
        LogCapture resumed_logs;
        resumed = run_session();
        EXPECT_NE(resumed_logs.text().find("resumed session presents the Plug&Charge contract for eMAID " +
                                           std::string(EMAID_B)),
                  std::string::npos);
        EXPECT_EQ(resumed_logs.text().find("configured contract material"), std::string::npos);
    }
    ASSERT_TRUE(fake_controller::constructions().back().resume.has_value());
    EXPECT_EQ(resumed->contract.value().emaid, EMAID_B);
    EXPECT_EQ(resumed->contract.value().key_pem, contract_b.key_pem);
    EXPECT_EQ(run_session().contract.value().emaid, EMAID_A);
}

// A destination that is a directory fails the rename after the key's went through.
TEST_F(ContractStore, FailedRenameRemovesTheRemainingTemporaryFiles) {
    fs::create_directories(store / "CONTRACT_CHAIN.pem" / "occupied");
    start_module();
    run_session(installing(contract_b.chain_pem, contract_b.key_pem, EMAID_B));

    EXPECT_NE(logs.text().find("cannot rename into CONTRACT_CHAIN.pem"), std::string::npos);
    for (const auto& entry : fs::directory_iterator(store)) {
        EXPECT_NE(entry.path().filename().string().front(), '.') << entry.path();
    }
}

TEST_F(ContractStore, UnwritableStoreDirectoryFailsTheStore) {
    if (::geteuid() == 0) {
        GTEST_SKIP() << "root writes into a read-only directory";
    }
    fs::create_directories(store);
    fs::permissions(store, fs::perms::owner_read | fs::perms::owner_exec);
    start_module();
    run_session(installing(contract_b.chain_pem, contract_b.key_pem, EMAID_B));
    fs::permissions(store, fs::perms::owner_all);

    EXPECT_NE(logs.text().find("cannot create a temporary file for CONTRACT_LEAF.key"), std::string::npos);
    EXPECT_TRUE(fs::is_empty(store));
}

// A run killed between staging and renaming leaves its temporary files behind.
TEST_F(ContractStore, StoringRemovesTemporaryFilesAnInterruptedRunLeft) {
    fs::create_directories(store);
    write_file(store / ".CONTRACT_LEAF.key.Ab12Cd", contract_a.key_pem);
    write_file(store / ".EMAID.txt.Ef34Gh", EMAID_A);
    start_module();
    run_session(installing(contract_b.chain_pem, contract_b.key_pem, EMAID_B));

    EXPECT_FALSE(fs::exists(store / ".CONTRACT_LEAF.key.Ab12Cd"));
    EXPECT_FALSE(fs::exists(store / ".EMAID.txt.Ef34Gh"));
    EXPECT_EQ(read_file(store / "CONTRACT_LEAF.key"), contract_b.key_pem);
}

// The loader would reject an EMAID.txt that does not name the leaf, losing the contract later.
TEST_F(ContractStore, EmaidThatDoesNotNameTheLeafIsNotStored) {
    start_module();
    run_session(installing(contract_b.chain_pem, contract_b.key_pem, EMAID_A));

    EXPECT_FALSE(fs::exists(store / "CONTRACT_LEAF.key"));
    EXPECT_NE(logs.text().find(std::string(EMAID_A) + " does not match the contract leaf's eMAID " + EMAID_B),
              std::string::npos);
}

TEST_F(ContractStore, KeyThatIsNotTheLeafsIsNotStored) {
    start_module();
    run_session(installing(contract_b.chain_pem, contract_a.key_pem, EMAID_B));

    EXPECT_FALSE(fs::exists(store / "CONTRACT_LEAF.key"));
    EXPECT_NE(logs.text().find("the key is not the private key of the contract leaf"), std::string::npos);
}

TEST_F(ContractStore, StoreDirectoryThatIsAFileIsReported) {
    write_file(store, "not a directory");
    start_module();
    EXPECT_EQ(run_session().contract.value().emaid, EMAID_A);
    EXPECT_TRUE(logs.warned({"not a directory", "this session uses the configured contract material"}));
}

} // namespace
