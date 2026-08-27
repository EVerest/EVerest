// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <everest/opcp/est_client.hpp>
#include <everest/opcp/http_client.hpp>
#include <everest/opcp/pkcs.hpp>
#include <everest/opcp/rcp_client.hpp>
#include <everest/opcp/security_store.hpp>
#include <everest/opcp/types.hpp>

/// \file enroller.hpp
/// Orchestrates one SECC leaf enrollment / renewal and the root certificate synchronisation against a
/// SecurityStore. Contains no I/O of its own; everything goes through the injected clients.
namespace opcp {

struct CsrSubject {
    std::string common_name;
    std::string organization;
    std::string country;
    bool use_tpm{false};
};

enum class EnrollStep {
    NotStarted,
    CheckRoots,
    GenerateCsr,
    Enroll,
    FetchCaCertificates,
    BuildChain,
    InstallLeaf,
    Done,
};
const char* enroll_step_name(EnrollStep step);

struct EnrollResult {
    bool ok{false};
    EnrollStep failed_step{EnrollStep::NotStarted};
    /// The PKI refused the credentials (bearer token or client certificate)
    bool auth_rejected{false};
    long http_status{0};
    std::string error;

    std::string csr_pem;
    std::string chain_pem;
    std::optional<CertificateSummary> leaf;
    std::size_t chain_length{0};
};

struct RootSyncResult {
    bool ok{false};
    bool auth_rejected{false};
    long http_status{0};
    std::string error;
    int installed{0};
    int skipped{0}; ///< root types the store cannot hold (OEM)
    int failed{0};
    /// One line per root: "<type> <CN>: <result>"
    std::vector<std::string> messages;
};

class Enroller {
public:
    Enroller(SecurityStore& store, EstClient& est, RcpClient& rcp);

    /// Generates a CSR for the ISO version's leaf type, enrolls it (simpleenroll, or simplereenroll when
    /// \p reenroll is set), fetches the CPO sub-CAs, assembles leaf + sub-CA chain and installs it.
    /// Requires a V2G root to be installed (sync_roots first).
    EnrollResult enroll_leaf(IsoVersion version, const CsrSubject& subject, const Auth& auth, bool reenroll);

    /// Downloads the given root types from the RCP and installs them into the matching CA bundles
    RootSyncResult sync_roots(const Auth& auth, const std::vector<RootType>& types);

private:
    SecurityStore& store;
    EstClient& est;
    RcpClient& rcp;
};

} // namespace opcp
