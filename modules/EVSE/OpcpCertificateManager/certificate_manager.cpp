// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "certificate_manager.hpp"

#include <algorithm>

#include <everest/logging.hpp>
#include <utils/date.hpp>

namespace module {

using types::opcp_certificate_manager::OperationResult;

namespace {

std::string now_rfc3339() {
    return Everest::Date::to_rfc3339(date::utc_clock::now());
}

std::string rfc3339_in(std::chrono::steady_clock::duration delta) {
    return Everest::Date::to_rfc3339(date::utc_clock::now() + std::chrono::duration_cast<std::chrono::seconds>(delta));
}

const char* iso_name(opcp::IsoVersion version) {
    return opcp::info(version).est_segment;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// RenewalPlanner

std::vector<opcp::IsoVersion> RenewalPlanner::leafs_due(const std::vector<LeafObservation>& observations,
                                                        int threshold_days, bool force) {
    std::vector<opcp::IsoVersion> due;
    for (const auto version : {opcp::IsoVersion::ISO15118_2, opcp::IsoVersion::ISO15118_20}) {
        for (const auto& observation : observations) {
            if (observation.version != version || !observation.managed) {
                continue;
            }
            if (force || observation.days_until_expiry < threshold_days) {
                due.push_back(version);
            }
        }
    }
    return due;
}

std::optional<opcp::IsoVersion> RenewalPlanner::auth_leaf_for(opcp::IsoVersion version,
                                                              const std::vector<LeafObservation>& observations) {
    for (const auto& observation : observations) {
        if (observation.version == version && observation.has_valid_leaf) {
            return version;
        }
    }
    for (const auto& observation : observations) {
        if (observation.version != version && observation.has_valid_leaf) {
            return observation.version;
        }
    }
    return std::nullopt;
}

std::chrono::seconds RenewalPlanner::retry_delay(std::chrono::seconds base, int consecutive_failures) {
    constexpr std::chrono::seconds cap(24 * 60 * 60);
    const int exponent = std::clamp(consecutive_failures - 1, 0, 16);
    const auto delay = base * (1L << exponent);
    return std::min<std::chrono::seconds>(delay, cap);
}

// ---------------------------------------------------------------------------------------------------------
// CertificateManager

CertificateManager::CertificateManager(ManagerConfig config_, opcp::SecurityStore& store_,
                                       opcp::HttpClientInterface& http_, StatusCallback publish_status_) :
    config(std::move(config_)),
    store(store_),
    http(http_),
    publish_status(std::move(publish_status_)),
    est(http, config.environment),
    rcp(http, config.environment),
    enroller(store, est, rcp) {
    for (const auto& leaf : config.leafs) {
        LeafRuntime runtime;
        runtime.config = leaf;
        runtime.status.iso_version = iso_name(leaf.version);
        runtime.status.managed = leaf.managed;
        runtime.status.days_until_expiry = 0;
        runtime.status.last_result = OperationResult::NotYetRun;
        leafs.push_back(std::move(runtime));
    }
    root_status.enabled = config.sync_roots;
    root_status.last_result = OperationResult::NotYetRun;
}

CertificateManager::~CertificateManager() {
    stop();
}

void CertificateManager::start() {
    std::lock_guard<std::mutex> lock(mutex);
    if (running) {
        return;
    }
    running = true;
    next_check = Clock::now() + config.initial_delay;
    if (config.sync_roots) {
        next_root_sync = Clock::now() + config.initial_delay;
    }
    thread = std::thread([this] { worker(); });
}

void CertificateManager::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!running) {
            return;
        }
        running = false;
    }
    cv.notify_all();
    if (thread.joinable()) {
        thread.join();
    }
}

bool CertificateManager::request_renewal(bool force) {
    std::lock_guard<std::mutex> lock(mutex);
    if (busy) {
        return false;
    }
    renewal_requested = true;
    renewal_force = renewal_force || force;
    cv.notify_all();
    return true;
}

bool CertificateManager::request_root_sync() {
    std::lock_guard<std::mutex> lock(mutex);
    if (busy) {
        return false;
    }
    root_sync_requested = true;
    cv.notify_all();
    return true;
}

std::optional<CertificateManager::Clock::time_point> CertificateManager::next_due() const {
    std::optional<Clock::time_point> due = next_check;
    auto consider = [&due](const std::optional<Clock::time_point>& candidate) {
        if (candidate.has_value() && (!due.has_value() || *candidate < *due)) {
            due = candidate;
        }
    };
    consider(next_root_sync);
    for (const auto& leaf : leafs) {
        consider(leaf.next_retry);
    }
    return due;
}

void CertificateManager::worker() {
    while (true) {
        bool force = false;
        bool roots_only = false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            while (running) {
                const auto now = Clock::now();
                const auto due = next_due();
                if (renewal_requested || root_sync_requested || (due.has_value() && *due <= now)) {
                    break;
                }
                if (due.has_value()) {
                    cv.wait_until(lock, *due);
                } else {
                    cv.wait(lock);
                }
            }
            if (!running) {
                return;
            }
            const auto now = Clock::now();
            const bool check_due = next_check.has_value() && *next_check <= now;
            bool retry_due = false;
            for (const auto& leaf : leafs) {
                retry_due |= leaf.next_retry.has_value() && *leaf.next_retry <= now;
            }
            const bool roots_due = next_root_sync.has_value() && *next_root_sync <= now;
            force = renewal_force;
            roots_only = !renewal_requested && !check_due && !retry_due && (root_sync_requested || roots_due);
            renewal_requested = false;
            renewal_force = false;
            root_sync_requested = false;
            busy = true;
        }
        run_check(force, roots_only);
        {
            std::lock_guard<std::mutex> lock(mutex);
            busy = false;
        }
        publish();
    }
}

void CertificateManager::run_check(bool force, bool roots_only) {
    const auto now = Clock::now();
    bool roots_due = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        roots_due = config.sync_roots && (roots_only || (next_root_sync.has_value() && *next_root_sync <= now));
    }
    if (roots_due) {
        run_root_sync();
    }
    if (!roots_only) {
        run_renewals(force);
    }
}

std::vector<LeafObservation> CertificateManager::observe() {
    std::vector<LeafObservation> observations;
    for (auto& leaf : leafs) {
        LeafObservation observation;
        observation.version = leaf.config.version;
        observation.managed = leaf.config.managed;
        const auto type = opcp::info(leaf.config.version).leaf_type;
        observation.days_until_expiry = store.get_leaf_expiry_days_count(type);
        observation.has_valid_leaf = store.get_leaf_certificate_info(type).has_value();
        {
            std::lock_guard<std::mutex> lock(mutex);
            leaf.status.days_until_expiry = observation.days_until_expiry;
        }
        observations.push_back(observation);
    }
    // An unmanaged leaf can still authenticate a renewal, so both are always observed
    return observations;
}

void CertificateManager::run_root_sync() {
    // Authenticate with any valid SECC leaf
    const auto observations = observe();
    std::optional<opcp::IsoVersion> auth_leaf;
    for (const auto& observation : observations) {
        if (observation.has_valid_leaf) {
            auth_leaf = observation.version;
            break;
        }
    }
    types::opcp_certificate_manager::RootSyncStatus status;
    status.enabled = true;
    std::chrono::seconds next_interval = config.root_sync_interval;

    if (!auth_leaf.has_value()) {
        status.last_result = OperationResult::NoClientCertificate;
        status.last_error = "no valid SECC leaf certificate available for TLS client authentication";
        EVLOG_warning << "Root certificate synchronisation skipped: " << *status.last_error;
    } else {
        const auto info = store.get_leaf_certificate_info(opcp::info(*auth_leaf).leaf_type);
        const opcp::ClientCertificate auth{info->certificate_path, info->key_path, info->key_password};
        const auto result = enroller.sync_roots(auth, config.root_types);
        if (result.ok) {
            status.last_result = OperationResult::Success;
            status.last_sync = now_rfc3339();
            status.installed_count = result.installed;
            root_sync_failures = 0;
            EVLOG_info << "Root certificates synchronised: " << result.installed << " installed, " << result.skipped
                       << " skipped";
            for (const auto& line : result.messages) {
                EVLOG_debug << "  " << line;
            }
        } else {
            status.last_result = result.auth_rejected ? OperationResult::AuthRejected : OperationResult::Failed;
            status.last_error = result.error;
            ++root_sync_failures;
            if (result.auth_rejected) {
                // The pool does not accept certificate authentication: no point in hammering it
                EVLOG_warning << "Root certificate pool rejected TLS client certificate authentication ("
                              << result.error << "); next attempt in " << config.root_sync_interval.count() << " s";
            } else {
                next_interval = RenewalPlanner::retry_delay(config.retry_backoff, root_sync_failures);
                EVLOG_warning << "Root certificate synchronisation failed: " << result.error << "; retry in "
                              << next_interval.count() << " s";
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex);
    const auto previous_sync = root_status.last_sync;
    root_status = status;
    if (!root_status.last_sync.has_value()) {
        root_status.last_sync = previous_sync;
    }
    next_root_sync = Clock::now() + next_interval;
}

void CertificateManager::run_renewals(bool force) {
    auto observations = observe();
    const auto due = RenewalPlanner::leafs_due(observations, config.renewal_threshold_days, force);

    for (auto& leaf : leafs) {
        const bool is_due = std::find(due.begin(), due.end(), leaf.config.version) != due.end();
        if (!is_due) {
            std::lock_guard<std::mutex> lock(mutex);
            leaf.next_retry.reset();
            if (leaf.config.managed && leaf.status.last_result == OperationResult::NotYetRun) {
                leaf.status.last_result = OperationResult::Skipped;
                leaf.status.last_error = "certificate still valid";
            }
            continue;
        }

        const char* name = opcp::info(leaf.config.version).name;
        const auto auth_version = RenewalPlanner::auth_leaf_for(leaf.config.version, observations);
        if (!auth_version.has_value()) {
            std::lock_guard<std::mutex> lock(mutex);
            leaf.status.last_result = OperationResult::NoClientCertificate;
            leaf.status.last_error =
                "no valid SECC leaf certificate for TLS client authentication - enroll with opcp-enroll";
            leaf.next_retry.reset(); // nothing will change until someone enrolls; the periodic check re-evaluates
            EVLOG_error << name << " leaf needs renewal but " << *leaf.status.last_error;
            continue;
        }

        const auto info = store.get_leaf_certificate_info(opcp::info(*auth_version).leaf_type);
        const opcp::ClientCertificate auth{info->certificate_path, info->key_path, info->key_password};
        EVLOG_info << "Renewing the " << name << " SECC leaf (" << leaf.status.days_until_expiry
                   << " days left), authenticating with the " << opcp::info(*auth_version).name << " leaf";

        const auto result = enroller.enroll_leaf(leaf.config.version, leaf.config.subject, auth, true);
        const int days_after = result.ok ? store.get_leaf_expiry_days_count(opcp::info(leaf.config.version).leaf_type)
                                         : leaf.status.days_until_expiry;

        std::unique_lock<std::mutex> lock(mutex);
        if (result.ok) {
            leaf.consecutive_failures = 0;
            leaf.next_retry.reset();
            leaf.status.last_result = OperationResult::Success;
            leaf.status.last_error.reset();
            leaf.status.last_renewal = now_rfc3339();
            leaf.status.next_retry.reset();
            leaf.status.days_until_expiry = days_after;
            EVLOG_info << name << " SECC leaf renewed: " << (result.leaf ? result.leaf->subject : std::string("?"))
                       << ", valid until " << (result.leaf ? result.leaf->not_after : std::string("?")) << ", chain of "
                       << result.chain_length;
            // A freshly renewed leaf is the best client certificate for the next one (observe() locks itself)
            lock.unlock();
            observations = observe();
        } else {
            ++leaf.consecutive_failures;
            const auto delay = RenewalPlanner::retry_delay(config.retry_backoff, leaf.consecutive_failures);
            leaf.next_retry = Clock::now() + delay;
            leaf.status.last_result = result.auth_rejected ? OperationResult::AuthRejected : OperationResult::Failed;
            leaf.status.last_error = std::string(opcp::enroll_step_name(result.failed_step)) + ": " + result.error;
            leaf.status.next_retry = rfc3339_in(delay);
            EVLOG_error << name << " SECC leaf renewal failed at " << opcp::enroll_step_name(result.failed_step) << ": "
                        << result.error << "; retry in " << delay.count() << " s";
        }
    }

    std::lock_guard<std::mutex> lock(mutex);
    next_check = Clock::now() + config.check_interval;
}

types::opcp_certificate_manager::Status CertificateManager::current_status() {
    std::lock_guard<std::mutex> lock(mutex);
    types::opcp_certificate_manager::Status status;
    status.timestamp = now_rfc3339();
    status.busy = busy;
    for (const auto& leaf : leafs) {
        status.leafs.push_back(leaf.status);
    }
    status.roots = root_status;
    return status;
}

void CertificateManager::publish() {
    if (publish_status) {
        publish_status(current_status());
    }
}

// ---------------------------------------------------------------------------------------------------------

std::optional<ManagerConfig>
manager_config_from(const std::string& environment, const std::string& api_base_url, const std::string& est_base_url,
                    const std::string& simplereenroll_path, const std::string& cacerts_path, const std::string& ca,
                    const std::string& server_ca_bundle, bool manage_iso15118_2, bool manage_iso15118_20,
                    const std::string& common_name_iso2, const std::string& common_name_iso20,
                    const std::string& organization, const std::string& country, bool use_tpm,
                    int renewal_threshold_days, int check_interval_s, int initial_delay_s, int retry_backoff_s,
                    bool sync_root_certificates, const std::string& root_types, int root_sync_interval_s,
                    int http_timeout_ms, std::string& error) {
    ManagerConfig config;

    auto env = opcp::Environment::preset(environment);
    if (!env.has_value()) {
        error = "unknown environment '" + environment + "'";
        return std::nullopt;
    }
    if (!api_base_url.empty()) {
        env->api_base_url = api_base_url;
        env->est_base_url = api_base_url;
        env->audience = api_base_url;
    }
    if (!est_base_url.empty()) {
        env->est_base_url = est_base_url;
    }
    if (!simplereenroll_path.empty()) {
        env->simplereenroll_path = simplereenroll_path;
    }
    if (!cacerts_path.empty()) {
        env->cacerts_path = cacerts_path;
    }
    if (!ca.empty()) {
        env->ca = ca;
    }
    // The module never needs the token endpoint; do not fail on a custom environment without one
    if (env->auth_url.empty()) {
        env->auth_url = "unused://";
    }
    const std::string missing = env->validate_and_complete();
    if (!missing.empty()) {
        error = "environment '" + environment + "' is incomplete, missing: " + missing;
        return std::nullopt;
    }
    config.environment = *env;

    if (!server_ca_bundle.empty()) {
        config.http.server_ca_bundle = server_ca_bundle;
    }
    config.http.timeout = std::chrono::milliseconds(http_timeout_ms);

    const std::string cn20 = common_name_iso20.empty() ? common_name_iso2 : common_name_iso20;
    if (manage_iso15118_2 && common_name_iso2.empty()) {
        error = "common_name_iso2 is required when manage_iso15118_2 is set";
        return std::nullopt;
    }
    if (manage_iso15118_20 && cn20.empty()) {
        error = "common_name_iso20 (or common_name_iso2) is required when manage_iso15118_20 is set";
        return std::nullopt;
    }
    if ((manage_iso15118_2 || manage_iso15118_20) && (organization.empty() || country.empty())) {
        error = "organization and country are required for certificate signing requests";
        return std::nullopt;
    }
    config.leafs.push_back(LeafConfig{
        opcp::IsoVersion::ISO15118_2, manage_iso15118_2, {common_name_iso2, organization, country, use_tpm}});
    config.leafs.push_back(
        LeafConfig{opcp::IsoVersion::ISO15118_20, manage_iso15118_20, {cn20, organization, country, use_tpm}});

    config.renewal_threshold_days = renewal_threshold_days;
    config.check_interval = std::chrono::seconds(check_interval_s);
    config.initial_delay = std::chrono::seconds(initial_delay_s);
    config.retry_backoff = std::chrono::seconds(retry_backoff_s);
    config.sync_roots = sync_root_certificates;
    std::vector<std::string> rejected;
    config.root_types = opcp::parse_root_type_list(root_types, rejected);
    if (!rejected.empty()) {
        error = "unknown root type '" + rejected.front() + "' in root_types";
        return std::nullopt;
    }
    config.root_sync_interval = std::chrono::seconds(root_sync_interval_s);
    return config;
}

} // namespace module
