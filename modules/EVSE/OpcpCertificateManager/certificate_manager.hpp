// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <everest/opcp/enroller.hpp>
#include <everest/opcp/environment.hpp>
#include <everest/opcp/http_client.hpp>
#include <everest/opcp/security_store.hpp>
#include <everest/opcp/types.hpp>

#include <generated/types/opcp_certificate_manager.hpp>

namespace module {

/// What the manager knows about one SECC leaf when deciding
struct LeafObservation {
    opcp::IsoVersion version;
    bool managed{false};
    int days_until_expiry{0};   ///< 0: none installed (or expired)
    bool has_valid_leaf{false}; ///< a valid leaf with private key exists (usable for TLS client auth)
};

/// Pure decision logic, unit tested without I/O
struct RenewalPlanner {
    /// Managed leafs that need renewal, ISO 15118-2 before ISO 15118-20
    static std::vector<opcp::IsoVersion> leafs_due(const std::vector<LeafObservation>& observations, int threshold_days,
                                                   bool force);

    /// Which installed leaf to authenticate the renewal of \p version with: the same type when still valid,
    /// otherwise any other valid SECC leaf (both are CPO certificates of the same PKI)
    static std::optional<opcp::IsoVersion> auth_leaf_for(opcp::IsoVersion version,
                                                         const std::vector<LeafObservation>& observations);

    /// Retry delay after \p consecutive_failures (>= 1): base doubled per failure, capped at 24 h
    static std::chrono::seconds retry_delay(std::chrono::seconds base, int consecutive_failures);
};

struct LeafConfig {
    opcp::IsoVersion version;
    bool managed{false};
    opcp::CsrSubject subject;
};

struct ManagerConfig {
    opcp::Environment environment;
    opcp::HttpClientOptions http;
    std::vector<LeafConfig> leafs;
    int renewal_threshold_days{30};
    std::chrono::seconds check_interval{43200};
    std::chrono::seconds initial_delay{60};
    std::chrono::seconds retry_backoff{600};
    bool sync_roots{true};
    std::vector<opcp::RootType> root_types{opcp::RootType::V2G, opcp::RootType::MO};
    std::chrono::seconds root_sync_interval{86400};
};

/// Runs the periodic checks on a worker thread: root synchronisation and leaf renewals through the enroller,
/// with exponential backoff after failures. All store/network access happens on that thread.
class CertificateManager {
public:
    using StatusCallback = std::function<void(const types::opcp_certificate_manager::Status&)>;

    CertificateManager(ManagerConfig config, opcp::SecurityStore& store, opcp::HttpClientInterface& http,
                       StatusCallback publish_status);
    ~CertificateManager();

    CertificateManager(const CertificateManager&) = delete;
    CertificateManager& operator=(const CertificateManager&) = delete;

    void start();
    void stop();

    /// Schedules a renewal check; returns false when an operation is currently running
    bool request_renewal(bool force);
    /// Schedules a root synchronisation; returns false when an operation is currently running
    bool request_root_sync();

    types::opcp_certificate_manager::Status current_status();

private:
    using Clock = std::chrono::steady_clock;

    struct LeafRuntime {
        LeafConfig config;
        types::opcp_certificate_manager::LeafStatus status;
        int consecutive_failures{0};
        std::optional<Clock::time_point> next_retry;
    };

    void worker();
    void run_check(bool force, bool roots_only);
    void run_root_sync();
    void run_renewals(bool force);
    std::vector<LeafObservation> observe();
    void publish();
    std::optional<Clock::time_point> next_due() const;

    ManagerConfig config;
    opcp::SecurityStore& store;
    opcp::HttpClientInterface& http;
    StatusCallback publish_status;

    opcp::EstClient est;
    opcp::RcpClient rcp;
    opcp::Enroller enroller;

    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    bool running{false};
    bool busy{false};
    bool renewal_requested{false};
    bool renewal_force{false};
    bool root_sync_requested{false};
    std::optional<Clock::time_point> next_check;
    std::optional<Clock::time_point> next_root_sync;
    int root_sync_failures{0};

    std::vector<LeafRuntime> leafs;
    types::opcp_certificate_manager::RootSyncStatus root_status;
};

/// Builds the ManagerConfig from the module configuration; \p error receives a description on failure
std::optional<ManagerConfig>
manager_config_from(const std::string& environment, const std::string& api_base_url, const std::string& est_base_url,
                    const std::string& simplereenroll_path, const std::string& cacerts_path, const std::string& ca,
                    const std::string& server_ca_bundle, bool manage_iso15118_2, bool manage_iso15118_20,
                    const std::string& common_name_iso2, const std::string& common_name_iso20,
                    const std::string& organization, const std::string& country, bool use_tpm,
                    int renewal_threshold_days, int check_interval_s, int initial_delay_s, int retry_backoff_s,
                    bool sync_root_certificates, const std::string& root_types, int root_sync_interval_s,
                    int http_timeout_ms, std::string& error);

} // namespace module
