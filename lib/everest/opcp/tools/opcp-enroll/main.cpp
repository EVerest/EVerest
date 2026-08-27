// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// opcp-enroll: initial enrollment of ISO 15118 SECC leaf certificates through an OPCP ecosystem (Hubject).
//
// Flow: OAuth2 token -> register end entity (VRA) -> sync root certificates (RCP) -> per ISO version:
// CSR (libevse-security) -> EST simpleenroll -> EST cacerts -> chain -> install into the evse_security store.

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <getopt.h>
#include <termios.h>
#include <unistd.h>

#include <everest/logging.hpp>
#include <everest/opcp/enroller.hpp>
#include <everest/opcp/environment.hpp>
#include <everest/opcp/est_client.hpp>
#include <everest/opcp/http_client.hpp>
#include <everest/opcp/identifiers.hpp>
#include <everest/opcp/oauth2.hpp>
#include <everest/opcp/rcp_client.hpp>
#include <everest/opcp/vra_client.hpp>

#include "direct_security_store.hpp"

namespace {

using opcp::IsoVersion;

struct Options {
    // environment
    std::string environment{"hubject-eu-qa"};
    std::string api_url;
    std::string est_url;
    std::string auth_url;
    std::string audience;
    std::string ca;
    std::string server_ca_bundle;
    bool yes{false};
    // credentials
    std::string client_id;
    std::string client_secret_env;
    // what to enroll
    std::vector<IsoVersion> iso_versions{IsoVersion::ISO15118_2, IsoVersion::ISO15118_20};
    std::string common_name_iso2;
    std::string common_name_iso20;
    std::string organization;
    std::string country;
    bool use_tpm{false};
    // registration
    bool skip_registration{false};
    std::string manufacturer;
    std::string device_name;
    std::string device_sw_version;
    std::string evse_serial;
    std::vector<std::string> evse_ids;
    std::string ocpp_version;
    std::string charge_box_serial;
    // roots
    bool skip_roots{false};
    std::string roots{"v2g,mo"};
    // store
    opcp::tool::StoreLayout store;
    // misc
    bool dry_run{false};
    bool print_config{false};
    bool verbose{false};
    long timeout_ms{30000};
};

void usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n\n"
              << "Enrolls ISO 15118 SECC leaf certificates via the Open Plug&Charge Protocol (OPCP) and installs\n"
              << "them together with the V2G/MO root certificates into the EVerest evse_security certificate store.\n\n"
              << "Environment:\n"
              << "  --environment NAME        hubject-{eu,us}-{test,qa,prod} or custom (default hubject-eu-qa)\n"
              << "  --api-url URL             API base URL (RCP, registration); overrides the preset\n"
              << "  --est-url URL             EST base URL; defaults to --api-url\n"
              << "  --auth-url URL            OAuth2 token endpoint; overrides the preset\n"
              << "  --audience URL            OAuth2 audience; defaults to the API base URL\n"
              << "  --ca NAME                 sub-CA identifier in the EST paths (default cpo)\n"
              << "  --server-ca-bundle FILE   PEM bundle to verify the ecosystem's TLS server certificate\n"
              << "  --yes                     do not ask for confirmation on a production environment\n\n"
              << "Credentials (OAuth2 client credentials issued by the ecosystem operator):\n"
              << "  --client-id ID            prompted for when omitted\n"
              << "  --client-secret-env VAR   read the secret from this environment variable instead of prompting\n\n"
              << "Certificate request:\n"
              << "  --iso 2|20|both           which SECC leaf certificate(s) to enroll (default both)\n"
              << "  --common-name-iso2 CN     EVSEID for the ISO 15118-2 leaf, e.g. DE*PNX*E12345\n"
              << "  --common-name-iso20 CN    SECCID for the ISO 15118-20 leaf (default: same as --common-name-iso2)\n"
              << "  --organization NAME       legal organization name (O)\n"
              << "  --country CC              two letter country code (C)\n"
              << "  --use-tpm                 keep the private keys in the TPM / custom OpenSSL provider\n\n"
              << "End entity registration (Hubject, done before enrollment):\n"
              << "  --skip-registration       the station is already registered\n"
              << "  --manufacturer NAME  --device-name NAME  --device-sw-version V  --evse-serial SN\n"
              << "  --evse-id ID              may be repeated; defaults to the common name\n"
              << "  --ocpp-version V  --charge-box-serial SN\n\n"
              << "Root certificates:\n"
              << "  --skip-roots              do not download root certificates from the RCP\n"
              << "  --roots LIST              root types to install, comma separated (default v2g,mo)\n\n"
              << "Certificate store (defaults match the EvseSecurity module):\n"
              << "  --certs-dir DIR           default <prefix>/etc/everest/certs\n"
              << "  --prefix DIR              EVerest install prefix (default " OPCP_DEFAULT_PREFIX ")\n"
              << "  --v2g-ca-bundle P  --mo-ca-bundle P  --secc-cert-dir P  --secc-key-dir P  --key-password PW\n\n"
              << "Other:\n"
              << "  --dry-run                 only fetch a token, print the decoded token claims and the CSR(s)\n"
              << "  --print-config            print an OpcpCertificateManager module config stanza and exit\n"
              << "  --timeout MS              HTTP timeout in milliseconds (default 30000)\n"
              << "  -v, --verbose             libcurl verbose output\n"
              << "  -h, --help\n";
}

std::string prompt(const std::string& label) {
    std::cout << label << ": " << std::flush;
    std::string value;
    std::getline(std::cin, value);
    return value;
}

std::string prompt_secret(const std::string& label) {
    std::cout << label << ": " << std::flush;
    termios old_settings{};
    const bool tty = isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &old_settings) == 0;
    if (tty) {
        termios silent = old_settings;
        silent.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &silent);
    }
    std::string value;
    std::getline(std::cin, value);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_settings);
        std::cout << "\n";
    }
    return value;
}

bool confirm(const std::string& question) {
    std::cout << question << " [y/N]: " << std::flush;
    std::string answer;
    std::getline(std::cin, answer);
    return answer == "y" || answer == "Y" || answer == "yes";
}

enum LongOption {
    OPT_ENVIRONMENT = 1000,
    OPT_API_URL,
    OPT_EST_URL,
    OPT_AUTH_URL,
    OPT_AUDIENCE,
    OPT_CA,
    OPT_SERVER_CA_BUNDLE,
    OPT_YES,
    OPT_CLIENT_ID,
    OPT_CLIENT_SECRET_ENV,
    OPT_ISO,
    OPT_CN_ISO2,
    OPT_CN_ISO20,
    OPT_ORGANIZATION,
    OPT_COUNTRY,
    OPT_USE_TPM,
    OPT_SKIP_REGISTRATION,
    OPT_MANUFACTURER,
    OPT_DEVICE_NAME,
    OPT_DEVICE_SW_VERSION,
    OPT_EVSE_SERIAL,
    OPT_EVSE_ID,
    OPT_OCPP_VERSION,
    OPT_CHARGE_BOX_SERIAL,
    OPT_SKIP_ROOTS,
    OPT_ROOTS,
    OPT_CERTS_DIR,
    OPT_PREFIX,
    OPT_V2G_CA_BUNDLE,
    OPT_MO_CA_BUNDLE,
    OPT_SECC_CERT_DIR,
    OPT_SECC_KEY_DIR,
    OPT_KEY_PASSWORD,
    OPT_DRY_RUN,
    OPT_PRINT_CONFIG,
    OPT_TIMEOUT,
};

const option LONG_OPTIONS[] = {
    {"environment", required_argument, nullptr, OPT_ENVIRONMENT},
    {"api-url", required_argument, nullptr, OPT_API_URL},
    {"est-url", required_argument, nullptr, OPT_EST_URL},
    {"auth-url", required_argument, nullptr, OPT_AUTH_URL},
    {"audience", required_argument, nullptr, OPT_AUDIENCE},
    {"ca", required_argument, nullptr, OPT_CA},
    {"server-ca-bundle", required_argument, nullptr, OPT_SERVER_CA_BUNDLE},
    {"yes", no_argument, nullptr, OPT_YES},
    {"client-id", required_argument, nullptr, OPT_CLIENT_ID},
    {"client-secret-env", required_argument, nullptr, OPT_CLIENT_SECRET_ENV},
    {"iso", required_argument, nullptr, OPT_ISO},
    {"common-name-iso2", required_argument, nullptr, OPT_CN_ISO2},
    {"common-name-iso20", required_argument, nullptr, OPT_CN_ISO20},
    {"organization", required_argument, nullptr, OPT_ORGANIZATION},
    {"country", required_argument, nullptr, OPT_COUNTRY},
    {"use-tpm", no_argument, nullptr, OPT_USE_TPM},
    {"skip-registration", no_argument, nullptr, OPT_SKIP_REGISTRATION},
    {"manufacturer", required_argument, nullptr, OPT_MANUFACTURER},
    {"device-name", required_argument, nullptr, OPT_DEVICE_NAME},
    {"device-sw-version", required_argument, nullptr, OPT_DEVICE_SW_VERSION},
    {"evse-serial", required_argument, nullptr, OPT_EVSE_SERIAL},
    {"evse-id", required_argument, nullptr, OPT_EVSE_ID},
    {"ocpp-version", required_argument, nullptr, OPT_OCPP_VERSION},
    {"charge-box-serial", required_argument, nullptr, OPT_CHARGE_BOX_SERIAL},
    {"skip-roots", no_argument, nullptr, OPT_SKIP_ROOTS},
    {"roots", required_argument, nullptr, OPT_ROOTS},
    {"certs-dir", required_argument, nullptr, OPT_CERTS_DIR},
    {"prefix", required_argument, nullptr, OPT_PREFIX},
    {"v2g-ca-bundle", required_argument, nullptr, OPT_V2G_CA_BUNDLE},
    {"mo-ca-bundle", required_argument, nullptr, OPT_MO_CA_BUNDLE},
    {"secc-cert-dir", required_argument, nullptr, OPT_SECC_CERT_DIR},
    {"secc-key-dir", required_argument, nullptr, OPT_SECC_KEY_DIR},
    {"key-password", required_argument, nullptr, OPT_KEY_PASSWORD},
    {"dry-run", no_argument, nullptr, OPT_DRY_RUN},
    {"print-config", no_argument, nullptr, OPT_PRINT_CONFIG},
    {"timeout", required_argument, nullptr, OPT_TIMEOUT},
    {"verbose", no_argument, nullptr, 'v'},
    {"help", no_argument, nullptr, 'h'},
    {nullptr, 0, nullptr, 0},
};

bool parse_iso(const std::string& value, std::vector<IsoVersion>& out) {
    if (value == "both") {
        out = {IsoVersion::ISO15118_2, IsoVersion::ISO15118_20};
        return true;
    }
    const auto version = opcp::iso_version_from_string(value);
    if (!version.has_value()) {
        return false;
    }
    out = {*version};
    return true;
}

std::optional<Options> parse(int argc, char** argv) {
    Options options;
    std::string prefix = OPCP_DEFAULT_PREFIX;
    int c = 0;
    while ((c = getopt_long(argc, argv, "vh", LONG_OPTIONS, nullptr)) != -1) {
        const std::string arg = optarg != nullptr ? optarg : "";
        switch (c) {
        case OPT_ENVIRONMENT:
            options.environment = arg;
            break;
        case OPT_API_URL:
            options.api_url = arg;
            break;
        case OPT_EST_URL:
            options.est_url = arg;
            break;
        case OPT_AUTH_URL:
            options.auth_url = arg;
            break;
        case OPT_AUDIENCE:
            options.audience = arg;
            break;
        case OPT_CA:
            options.ca = arg;
            break;
        case OPT_SERVER_CA_BUNDLE:
            options.server_ca_bundle = arg;
            break;
        case OPT_YES:
            options.yes = true;
            break;
        case OPT_CLIENT_ID:
            options.client_id = arg;
            break;
        case OPT_CLIENT_SECRET_ENV:
            options.client_secret_env = arg;
            break;
        case OPT_ISO:
            if (!parse_iso(arg, options.iso_versions)) {
                std::cerr << "invalid --iso value '" << arg << "' (2, 20 or both)\n";
                return std::nullopt;
            }
            break;
        case OPT_CN_ISO2:
            options.common_name_iso2 = arg;
            break;
        case OPT_CN_ISO20:
            options.common_name_iso20 = arg;
            break;
        case OPT_ORGANIZATION:
            options.organization = arg;
            break;
        case OPT_COUNTRY:
            options.country = arg;
            break;
        case OPT_USE_TPM:
            options.use_tpm = true;
            break;
        case OPT_SKIP_REGISTRATION:
            options.skip_registration = true;
            break;
        case OPT_MANUFACTURER:
            options.manufacturer = arg;
            break;
        case OPT_DEVICE_NAME:
            options.device_name = arg;
            break;
        case OPT_DEVICE_SW_VERSION:
            options.device_sw_version = arg;
            break;
        case OPT_EVSE_SERIAL:
            options.evse_serial = arg;
            break;
        case OPT_EVSE_ID:
            options.evse_ids.push_back(arg);
            break;
        case OPT_OCPP_VERSION:
            options.ocpp_version = arg;
            break;
        case OPT_CHARGE_BOX_SERIAL:
            options.charge_box_serial = arg;
            break;
        case OPT_SKIP_ROOTS:
            options.skip_roots = true;
            break;
        case OPT_ROOTS:
            options.roots = arg;
            break;
        case OPT_CERTS_DIR:
            options.store.certs_dir = arg;
            break;
        case OPT_PREFIX:
            prefix = arg;
            break;
        case OPT_V2G_CA_BUNDLE:
            options.store.v2g_ca_bundle = arg;
            break;
        case OPT_MO_CA_BUNDLE:
            options.store.mo_ca_bundle = arg;
            break;
        case OPT_SECC_CERT_DIR:
            options.store.secc_leaf_cert_directory = arg;
            break;
        case OPT_SECC_KEY_DIR:
            options.store.secc_leaf_key_directory = arg;
            break;
        case OPT_KEY_PASSWORD:
            options.store.private_key_password = arg;
            break;
        case OPT_DRY_RUN:
            options.dry_run = true;
            break;
        case OPT_PRINT_CONFIG:
            options.print_config = true;
            break;
        case OPT_TIMEOUT:
            options.timeout_ms = std::strtol(arg.c_str(), nullptr, 10);
            break;
        case 'v':
            options.verbose = true;
            break;
        case 'h':
            usage(argv[0]);
            std::exit(0);
        default:
            usage(argv[0]);
            return std::nullopt;
        }
    }
    if (options.store.certs_dir.empty()) {
        options.store.certs_dir = prefix + "/etc/everest/certs";
    }
    if (options.common_name_iso20.empty()) {
        options.common_name_iso20 = options.common_name_iso2;
    }
    return options;
}

std::optional<opcp::Environment> build_environment(const Options& options) {
    auto env = opcp::Environment::preset(options.environment);
    if (!env.has_value()) {
        std::cerr << "unknown --environment '" << options.environment << "'; known:";
        for (const auto& name : opcp::Environment::preset_names()) {
            std::cerr << " " << name;
        }
        std::cerr << "\n";
        return std::nullopt;
    }
    if (!options.api_url.empty()) {
        env->api_base_url = options.api_url;
        if (options.est_url.empty()) {
            env->est_base_url = options.api_url;
        }
        if (options.audience.empty()) {
            env->audience = options.api_url;
        }
    }
    if (!options.est_url.empty()) {
        env->est_base_url = options.est_url;
    }
    if (!options.auth_url.empty()) {
        env->auth_url = options.auth_url;
    }
    if (!options.audience.empty()) {
        env->audience = options.audience;
    }
    if (!options.ca.empty()) {
        env->ca = options.ca;
    }
    const std::string missing = env->validate_and_complete();
    if (!missing.empty()) {
        std::cerr << "environment '" << options.environment << "' is incomplete, missing: " << missing
                  << "(use --api-url / --auth-url)\n";
        return std::nullopt;
    }
    return env;
}

const std::string& common_name_for(const Options& options, IsoVersion version) {
    return version == IsoVersion::ISO15118_2 ? options.common_name_iso2 : options.common_name_iso20;
}

void warn_common_name(const Options& options, IsoVersion version) {
    const std::string& cn = common_name_for(options, version);
    const bool ok = version == IsoVersion::ISO15118_2 ? (opcp::is_valid_evseid(cn) || opcp::is_valid_seccid(cn))
                                                      : opcp::is_valid_seccid(cn);
    if (!ok) {
        std::cerr << "warning: common name '" << cn << "' for " << opcp::info(version).name
                  << " matches neither the EVSEID nor the SECCID syntax; the PKI may refuse it\n";
    }
}

void print_module_config(const Options& options, const opcp::Environment& env) {
    const bool custom =
        env.name == "custom" || !options.api_url.empty() || !options.est_url.empty() || !options.auth_url.empty();
    std::cout << "  opcp_certificate_manager:\n"
              << "    module: OpcpCertificateManager\n"
              << "    config_module:\n"
              << "      environment: " << (custom ? "custom" : env.name) << "\n";
    if (custom) {
        std::cout << "      api_base_url: " << env.api_base_url << "\n"
                  << "      est_base_url: " << env.est_base_url << "\n";
    }
    if (env.ca != "cpo") {
        std::cout << "      ca: " << env.ca << "\n";
    }
    if (!options.server_ca_bundle.empty()) {
        std::cout << "      server_ca_bundle: " << options.server_ca_bundle << "\n";
    }
    bool iso2 = false;
    bool iso20 = false;
    for (const auto version : options.iso_versions) {
        iso2 |= version == IsoVersion::ISO15118_2;
        iso20 |= version == IsoVersion::ISO15118_20;
    }
    std::cout << "      manage_iso15118_2: " << (iso2 ? "true" : "false") << "\n"
              << "      manage_iso15118_20: " << (iso20 ? "true" : "false") << "\n"
              << "      common_name_iso2: \"" << options.common_name_iso2 << "\"\n"
              << "      common_name_iso20: \"" << options.common_name_iso20 << "\"\n"
              << "      organization: \"" << options.organization << "\"\n"
              << "      country: " << options.country << "\n"
              << "      use_tpm: " << (options.use_tpm ? "true" : "false") << "\n"
              << "      root_types: " << options.roots << "\n"
              << "    connections:\n"
              << "      security:\n"
              << "        - module_id: evse_security\n"
              << "          implementation_id: main\n";
}

void print_leaf(const opcp::EnrollResult& result) {
    if (!result.leaf.has_value()) {
        return;
    }
    std::cout << "    subject:    " << result.leaf->subject << "\n"
              << "    issuer:     " << result.leaf->issuer << "\n"
              << "    serial:     " << result.leaf->serial_number << "\n"
              << "    valid:      " << result.leaf->not_before << " .. " << result.leaf->not_after << "\n"
              << "    key:        " << result.leaf->public_key_algorithm << "\n"
              << "    chain:      " << result.chain_length << " certificate(s) (leaf + sub-CAs)\n";
}

} // namespace

int main(int argc, char** argv) {
    const auto parsed = parse(argc, argv);
    if (!parsed.has_value()) {
        return 2;
    }
    const Options& options = *parsed;

    const auto env = build_environment(options);
    if (!env.has_value()) {
        return 2;
    }

    if (options.print_config) {
        print_module_config(options, *env);
        return 0;
    }

    if (options.iso_versions.empty()) {
        std::cerr << "nothing to enroll\n";
        return 2;
    }
    if (options.common_name_iso2.empty() && options.common_name_iso20.empty()) {
        std::cerr << "--common-name-iso2 (and/or --common-name-iso20) is required\n";
        return 2;
    }
    if (options.organization.empty() || options.country.empty()) {
        std::cerr << "--organization and --country are required\n";
        return 2;
    }
    std::vector<std::string> rejected_roots;
    const auto root_types = opcp::parse_root_type_list(options.roots, rejected_roots);
    if (!rejected_roots.empty()) {
        std::cerr << "unknown root type in --roots: " << rejected_roots.front() << " (v2g, mo, oem)\n";
        return 2;
    }

    if (env->is_production() && !options.yes) {
        if (!confirm("You are about to enroll against the PRODUCTION ecosystem " + env->api_base_url + ". Continue?")) {
            std::cerr << "aborted\n";
            return 1;
        }
    }

    // Keep libevse-security's own logging (EVLOG) quiet unless asked for
    Everest::Logging::init();

    std::cout << "Environment:  " << env->name << "\n"
              << "  API:        " << env->api_base_url << "\n"
              << "  EST:        " << env->est_base_url << " (ca=" << env->ca << ")\n"
              << "  token:      " << env->auth_url << "\n"
              << "Store:        " << options.store.certs_dir << "\n";
    for (const auto version : options.iso_versions) {
        std::cout << "  " << opcp::info(version).name << " leaf CN: " << common_name_for(options, version) << "\n";
        warn_common_name(options, version);
    }

    // Credentials
    std::string client_id = options.client_id;
    if (client_id.empty()) {
        client_id = prompt("OAuth2 client_id");
    }
    std::string client_secret;
    if (!options.client_secret_env.empty()) {
        const char* value = std::getenv(options.client_secret_env.c_str());
        if (value == nullptr) {
            std::cerr << "environment variable " << options.client_secret_env << " is not set\n";
            return 2;
        }
        client_secret = value;
    } else {
        client_secret = prompt_secret("OAuth2 client_secret");
    }
    if (client_id.empty() || client_secret.empty()) {
        std::cerr << "client_id and client_secret are required\n";
        return 2;
    }

    opcp::HttpClientOptions http_options;
    if (!options.server_ca_bundle.empty()) {
        http_options.server_ca_bundle = options.server_ca_bundle;
    }
    http_options.timeout = std::chrono::milliseconds(options.timeout_ms);
    http_options.verbose = options.verbose;
    opcp::CurlHttpClient http(http_options);

    // 1. Token
    std::cout << "\n[1/4] Requesting access token ... " << std::flush;
    const auto token = opcp::fetch_access_token(http, *env, client_id, client_secret);
    if (!token.ok) {
        std::cout << "failed\n";
        std::cerr << "  " << token.error << "\n";
        return 1;
    }
    std::cout << "ok (valid " << token.expires_in / 3600 << " h)\n";
    const auto claims = opcp::decode_jwt_payload(token.access_token);
    const auto roles = opcp::jwt_roles(claims);
    if (!roles.empty()) {
        std::cout << "  roles:";
        for (const auto& role : roles) {
            std::cout << " " << role;
        }
        std::cout << "\n";
        bool cpo = false;
        for (const auto& role : roles) {
            cpo |= role == "CPO";
        }
        if (!cpo) {
            std::cerr << "  error: the account has no CPO role; SECC leaf enrollment will be refused\n";
            return 1;
        }
    }
    if (claims.contains("permissions") && claims["permissions"].is_array()) {
        std::cout << "  permissions: " << claims["permissions"].dump() << "\n";
    }
    if (options.verbose) {
        std::cout << "  claims: " << claims.dump(2) << "\n";
    }
    const opcp::Auth auth = token.bearer();

    // 2. Registration
    opcp::VraClient vra(http, *env);
    if (options.skip_registration || options.dry_run) {
        std::cout << "[2/4] End entity registration skipped\n";
    } else {
        for (const auto version : options.iso_versions) {
            opcp::EndEntity entity;
            entity.common_name = common_name_for(options, version);
            entity.manufacturer = options.manufacturer;
            entity.device_name = options.device_name;
            entity.device_sw_version = options.device_sw_version;
            entity.evse_serial_number = options.evse_serial;
            entity.evse_ids = options.evse_ids;
            entity.iso_version = version;
            entity.ocpp_version = options.ocpp_version;
            entity.charge_box_serial_number = options.charge_box_serial;
            std::cout << "[2/4] Registering end entity '" << entity.common_name << "' (" << opcp::info(version).name
                      << ") ... " << std::flush;
            const auto registered = vra.register_end_entity(entity, auth);
            if (!registered.ok) {
                std::cout << "failed\n";
                std::cerr << "  " << registered.url << ": " << registered.error << "\n";
                if (registered.auth_rejected) {
                    std::cerr << "  The EVSE Operator ID '" << opcp::operator_id_of(entity.common_name)
                              << "' may not be provisioned for this account - ask the ecosystem operator.\n";
                }
                return 1;
            }
            std::cout << "ok (HTTP " << registered.http_status << ")\n";
        }
    }

    // 3. Roots
    opcp::tool::DirectSecurityStore store(options.store);
    opcp::EstClient est(http, *env);
    opcp::RcpClient rcp(http, *env);
    opcp::Enroller enroller(store, est, rcp);

    if (options.skip_roots) {
        std::cout << "[3/4] Root certificate download skipped\n";
    } else {
        std::cout << "[3/4] Synchronising root certificates (" << options.roots << ") ... " << std::flush;
        const auto roots = enroller.sync_roots(auth, root_types);
        if (!roots.ok) {
            std::cout << "failed\n";
            std::cerr << "  " << roots.error << "\n";
            for (const auto& line : roots.messages) {
                std::cerr << "    " << line << "\n";
            }
            return 1;
        }
        std::cout << "ok (" << roots.installed << " installed";
        if (roots.skipped > 0) {
            std::cout << ", " << roots.skipped << " skipped";
        }
        std::cout << ")\n";
        for (const auto& line : roots.messages) {
            std::cout << "    " << line << "\n";
        }
    }
    if (!store.is_ca_certificate_installed(evse_security::CaCertificateType::V2G)) {
        std::cerr << "  error: no V2G root certificate installed in " << options.store.certs_dir
                  << "; cannot validate enrolled certificates (run without --skip-roots)\n";
        return 1;
    }

    // 4. Leaf certificates
    int failures = 0;
    for (const auto version : options.iso_versions) {
        const auto& iso = opcp::info(version);
        const opcp::CsrSubject subject{common_name_for(options, version), options.organization, options.country,
                                       options.use_tpm};
        if (options.dry_run) {
            const auto csr = store.generate_certificate_signing_request(
                iso.leaf_type, subject.country, subject.organization, subject.common_name, subject.use_tpm);
            std::cout << "[4/4] " << iso.name << " CSR (dry run, key discarded):\n"
                      << csr.csr.value_or("<generation failed>") << "\n";
            if (csr.csr.has_value()) {
                store.certificate_signing_request_failed(*csr.csr, iso.leaf_type);
            }
            continue;
        }
        std::cout << "[4/4] Enrolling " << iso.name << " leaf for '" << subject.common_name << "' ... " << std::flush;
        const auto result = enroller.enroll_leaf(version, subject, auth, false);
        if (!result.ok) {
            std::cout << "failed\n";
            std::cerr << "  step '" << opcp::enroll_step_name(result.failed_step) << "': " << result.error << "\n";
            if (result.auth_rejected) {
                std::cerr << "  The PKI refused the request (HTTP " << result.http_status
                          << "). Check that the EVSE Operator ID '" << opcp::operator_id_of(subject.common_name)
                          << "' is provisioned for this account and the end entity is registered.\n";
            }
            ++failures;
            continue;
        }
        std::cout << "ok\n";
        print_leaf(result);
        const auto installed = store.get_leaf_certificate_info(iso.leaf_type);
        if (installed.has_value()) {
            std::cout << "    files:      " << installed->certificate_path << "\n"
                      << "                " << installed->key_path << "\n";
        }
    }

    if (failures > 0) {
        std::cerr << "\n" << failures << " enrollment(s) failed\n";
        return 1;
    }
    if (!options.dry_run) {
        std::cout << "\nDone. Matching OpcpCertificateManager configuration:\n";
        print_module_config(options, *env);
    }
    return 0;
}
