// SPDX-License-Identifier: Apache-2.0
// Copyright Contributors to the EVerest Project.

#include "evse_security/utils/enforce_certificate_rules.hpp"
#include <algorithm>
#include <cctype>
#include <everest/logging.hpp>
#include <evse_security/certificate/x509_wrapper.hpp>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <string>
#include <vector>

namespace {

inline std::string node_val(ryml::ConstNodeRef n) {
    if (!n.has_val() || n.val().str == nullptr) return {};
    return std::string(n.val().str, n.val().len);
}

inline std::string node_key(ryml::ConstNodeRef n) {
    if (!n.has_key() || n.key().str == nullptr) return {};
    return std::string(n.key().str, n.key().len);
}

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline bool parse_bool(const std::string& s, bool default_v) {
    if (s.empty()) return default_v;
    std::string v = to_lower(s);
    if (v == "true"  || v == "1" || v == "yes" || v == "on")  return true;
    if (v == "false" || v == "0" || v == "no"  || v == "off") return false;
    return default_v;
}

enum class Presence { Required, Optional, Forbidden };

inline std::optional<Presence> parse_presence(const std::string& s) {
    if (s.empty()) return std::nullopt;
    std::string v = to_lower(s);
    if (v == "required"  || v == "must")       return Presence::Required;
    if (v == "optional"  || v == "may")        return Presence::Optional;
    if (v == "forbidden" || v == "must_not")   return Presence::Forbidden;
    if (v == "true"  || v == "1" || v == "yes") return Presence::Required;
    if (v == "false" || v == "0" || v == "no")  return Presence::Forbidden;
    return std::nullopt;
}

inline bool is_issuer_target(const std::string& t) {
    std::string v = to_lower(t);
    return v == "issuer" || v == "issuer_cert" || v == "issuercertificate";
}

inline bool is_extension_field(const std::string& field) {
    return field == "basicConstraints"          ||
           field == "keyUsage"                  ||
           field == "extendedKeyUsage"          ||
           field == "subjectKeyIdentifier"      ||
           field == "authorityKeyIdentifier"    ||
           field == "crlDistributionPointName"  ||
           field == "crlDistributionPoints"     ||
           field == "certificatePolicies"       ||
           field == "authorityInfoAccessMethod";
}

inline void parse_stand_rule(ryml::ConstNodeRef ruleNode,
                             std::string& field,
                             std::string& mustExist,
                             std::string& critical,
                             std::string& val,
                             std::string& target) {
    field.clear(); mustExist.clear(); critical.clear(); val.clear(); target.clear();

    for (auto child : ruleNode.children()) {
        std::string k = node_key(child);
        std::string v = node_val(child);

        if (k == "field" || k == "field_name" || k == "name" || k == "key") {
            field = v;
        } else if (k == "mustExist" || k == "must" || k == "required") {
            mustExist = v;
        } else if (k == "critical") {
            critical = v;
        } else if (k == "val" || k == "value" || k == "expected") {
            val = v;
        } else if (k == "target") {
            target = v;
        } else if (field.empty()) {
            field = k;
            if (child.is_map()) {
                for (auto attr : child.children()) {
                    std::string ak = node_key(attr);
                    std::string av = node_val(attr);
                    if (ak == "mustExist" || ak == "must" || ak == "required") {
                        mustExist = av;
                    } else if (ak == "critical") {
                        critical = av;
                    } else if (ak == "val" || ak == "value" || ak == "expected") {
                        val = av;
                    } else if (ak == "target") {
                        target = av;
                    }
                }
            } else if (!v.empty()) {
                val = v;
            }
        }
    }
}

enum class FieldResolve {
    Value,
    SkipIssuer,
    Unknown,
};

FieldResolve resolve_field(const evse_security::X509Wrapper& w,
                           const std::string& field,
                           bool is_issuer,
                           std::string& out) {
    if (field == "commonName") {
        out = is_issuer ? w.get_issuer_common_name() : w.get_common_name();
        return FieldResolve::Value;
    }
    if (field == "domainComponent") {
        out = is_issuer ? w.get_issuer_domain_component() : w.get_domain_component();
        return FieldResolve::Value;
    }
    if (field == "organization") {
        out = is_issuer ? w.get_issuer_organization() : w.get_organization();
        return FieldResolve::Value;
    }
    if (field == "organizationalUnit") {
        out = is_issuer ? w.get_issuer_organizational_unit() : w.get_organizational_unit();
        return FieldResolve::Value;
    }
    if (field == "country") {
        out = is_issuer ? w.get_issuer_country() : w.get_country();
        return FieldResolve::Value;
    }
    if (field == "state") {
        out = is_issuer ? w.get_issuer_state() : w.get_state();
        return FieldResolve::Value;
    }
    if (field == "locality") {
        out = is_issuer ? w.get_issuer_locality() : w.get_locality();
        return FieldResolve::Value;
    }
    if (field == "serialNumber") {
        if (is_issuer) return FieldResolve::SkipIssuer;
        out = w.get_serial_number();
        return FieldResolve::Value;
    }
    if (field == "crlDistributionPointName" || field == "crlDistributionPoints") {
        out = w.get_crl_distribution_points();
        return FieldResolve::Value;
    }

    if (field == "issuerNameHash") { out = w.get_issuer_name_hash(); return FieldResolve::Value; }
    if (field == "keyHash")        { out = w.get_key_hash();        return FieldResolve::Value; }

    if (is_issuer) return FieldResolve::SkipIssuer;

    if (field == "keyUsage")               { out = w.get_key_usage();               return FieldResolve::Value; }
    if (field == "basicConstraints")       { out = w.get_basic_constraints();       return FieldResolve::Value; }
    if (field == "subjectKeyIdentifier")   { out = w.get_subject_key_identifier();  return FieldResolve::Value; }
    if (field == "authorityKeyIdentifier") { out = w.get_authority_key_identifier(); return FieldResolve::Value; }

    if (field == "notBefore")                 { out = w.get_not_before();             return FieldResolve::Value; }
    if (field == "notAfter")                  { out = w.get_not_after();              return FieldResolve::Value; }
    if (field == "subjectPublicKeyAlgorithm") { out = w.get_public_key_algorithm();   return FieldResolve::Value; }
    if (field == "subjectPublicKey")          { out = w.get_public_key_bits();        return FieldResolve::Value; }
    if (field == "extendedKeyUsage")          { out = w.get_extended_key_usage();     return FieldResolve::Value; }
    if (field == "certificatePolicies")       { out = w.get_certificate_policies();   return FieldResolve::Value; }
    if (field == "authorityInfoAccessMethod") { out = w.get_authority_info_access();  return FieldResolve::Value; }

    return FieldResolve::Unknown;
}

} // namespace


int enforce_certificate_rules(const evse_security::X509Wrapper& wrapper, const std::string& manualCertProfile) {
    const std::string dc = wrapper.get_domain_component();
    const std::string basicConstraints = wrapper.get_basic_constraints();
    std::string certType;

    const bool isCA = (basicConstraints.find("CA:TRUE") != std::string::npos);
    if (isCA) {
        certType = wrapper.is_selfsigned() ? "root" : "sub_ca";
    } else {
        certType = "leaf";
    }

    int pathLength = -1;
    const std::string marker = "pathlen:";
    size_t pos = basicConstraints.find(marker);
    if (pos != std::string::npos) {
        std::string lenStr = basicConstraints.substr(pos + marker.length());
        try {
            pathLength = std::stoi(lenStr);
        } catch (...) {
            pathLength = -1;
        }
    }

    if (dc.empty() && manualCertProfile.empty()) {
        EVLOG_warning << "No Domain Component (DC) found in the certificate, "
                         "manually setting the certificate type is required";
        return 0;
    }

    std::string profile;
    const std::string profiles_dir = CERT_PROFILES_DIR;
    if (!std::filesystem::exists(profiles_dir) || !std::filesystem::is_directory(profiles_dir)) {
        EVLOG_warning << "Profiles directory does not exist: " << profiles_dir;
        return 0;
    }

    if (!manualCertProfile.empty()) {
        profile = profiles_dir + "/" + manualCertProfile + ".yaml";
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(profiles_dir)) {
            if (entry.path().extension() != ".yaml") continue;

            try {
                std::ifstream f(entry.path());
                std::string content((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
                ryml::Tree t = ryml::parse_in_arena(ryml::to_csubstr(content));
                ryml::NodeRef header = t.rootref().find_child("header");
                if (!header.readable()) continue;

                ryml::NodeRef dc_node = header.find_child("domain_component");
                if (dc_node.readable()) {
                    if (node_val(dc_node) != dc) continue;
                }

                ryml::NodeRef role_node = header.find_child("role");
                if (role_node.readable()) {
                    if (node_val(role_node) != certType) continue;
                }

                if (certType == "sub_ca" && pathLength >= 0) {
                    ryml::NodeRef path_node = header.find_child("path_length");
                    if (path_node.readable()) {
                        int path_val = -1;
                        try { path_val = std::stoi(node_val(path_node)); } catch (...) {}
                        if (path_val != pathLength) continue;
                    }
                }

                profile = entry.path().string();
                break;
            } catch (const std::exception& e) {
                EVLOG_warning << "Error parsing " << entry.path() << ": " << e.what();
                continue;
            }
        }
    }

    if (profile.empty()) {
        EVLOG_info << "No matching security profile found for DC=" << dc
                   << ", role=" << certType
                   << (pathLength >= 0 ? ", path_length=" + std::to_string(pathLength) : "");
        return 0;
    }

    std::ifstream profile_file(profile);
    if (!profile_file.is_open()) {
        EVLOG_warning << "Failed to open: " << profile;
        return -1;
    }
    std::string profile_content((std::istreambuf_iterator<char>(profile_file)),
                                 std::istreambuf_iterator<char>());
    profile_file.close();

    ryml::Tree tree;
    try {
        tree = ryml::parse_in_arena(ryml::to_csubstr(profile_content));
    } catch (const std::exception& e) {
        EVLOG_error << "YAML parsing failed: " << e.what();
        return -1;
    }

    ryml::NodeRef root = tree.rootref();
    if (!root.readable()) {
        EVLOG_warning << "Invalid YAML root";
        return -1;
    }

    ryml::NodeRef profileNode;
    for (auto child : root.children()) {
        if (node_key(child) == "header") continue;
        if (child.has_child("stand")) { profileNode = child; break; }
    }
    if (!profileNode.readable()) {
        EVLOG_warning << "No profile section with 'stand' found in: " << profile;
        return -1;
    }

    ryml::NodeRef standNode = profileNode.find_child("stand");
    if (!standNode.readable()) {
        EVLOG_warning << "No stand rules found in: " << profile;
        return -1;
    }

    std::size_t effective_rule_count = 0;
    for (auto rule : standNode.children()) {
        std::string f, m, c, v, t;
        parse_stand_rule(rule, f, m, c, v, t);
        if (!f.empty()) ++effective_rule_count;
    }
    if (effective_rule_count == 0) {
        EVLOG_error << "Profile contains no effective rules: " << profile;
        return -1;
    }

    int is_valid = 1;

    for (auto ruleNode : standNode.children()) {
        std::string field_str, mustExist_str, critical_str, expected_val, target_str;
        parse_stand_rule(ruleNode, field_str, mustExist_str, critical_str, expected_val, target_str);

        if (field_str.empty()) continue;

        const bool is_issuer = is_issuer_target(target_str);

        std::string cert_value;
        FieldResolve res = resolve_field(wrapper, field_str, is_issuer, cert_value);
        if (res == FieldResolve::SkipIssuer) {
            EVLOG_warning << "Rule on issuer field '" << field_str << "' is not supported";
            is_valid = 0;
            continue;
        }
        if (res == FieldResolve::Unknown) {
            EVLOG_warning << "Unsupported field in profile: '" << field_str << "'";
            is_valid = 0;
            continue;
        }

        if (!critical_str.empty()) {
            if (!is_extension_field(field_str)) {
                EVLOG_warning << "Field '" << field_str
                              << "' is not an extension; 'critical' has no meaning";
                is_valid = 0;
            } else {
                const bool expected_critical = parse_bool(critical_str, false);
                if (!wrapper.has_extension(field_str)) {
                    EVLOG_warning << "Extension " << field_str
                                  << " required for criticality check but missing";
                    is_valid = 0;
                } else if (wrapper.is_extension_critical(field_str) != expected_critical) {
                    EVLOG_warning << "Extension " << field_str
                                  << " critical flag mismatch (expected "
                                  << (expected_critical ? "true" : "false") << ")";
                    is_valid = 0;
                }
            }
        }

        auto presence_opt = parse_presence(mustExist_str);
        if (!presence_opt.has_value()) {
            continue;
        }
        const Presence presence = *presence_opt;

        const bool present = !cert_value.empty();

        switch (presence) {
            case Presence::Required:
                if (!present) {
                    EVLOG_warning << "Field " << field_str << " required but missing";
                    is_valid = 0;
                } else if (!expected_val.empty() && cert_value != expected_val) {
                    EVLOG_warning << "Field " << field_str << " value mismatch: expected "
                                  << expected_val << ", got " << cert_value;
                    is_valid = 0;
                }
                break;

            case Presence::Optional:
                if (present && !expected_val.empty() && cert_value != expected_val) {
                    EVLOG_warning << "Field " << field_str << " value mismatch: expected "
                                  << expected_val << ", got " << cert_value;
                    is_valid = 0;
                }
                break;

            case Presence::Forbidden:
                if (present) {
                    EVLOG_warning << "Field " << field_str << " forbidden but present: "
                                  << cert_value;
                    is_valid = 0;
                }
                break;
        }
    }

    {
        static const char* ku_names[] = {
            "digitalSignature", "nonRepudiation", "keyEncipherment",
            "dataEncipherment", "keyAgreement", "keyCertSign",
            "cRLSign", "encipherOnly", "decipherOnly"
        };
        ryml::NodeRef kuNode = profileNode.find_child("key_usage");
        if (kuNode.readable()) {
            const std::string ku_value = wrapper.get_key_usage();
            for (auto kuRule : kuNode.children()) {
                int bit = -1;
                std::string must_str;
                std::string target;
                for (auto child : kuRule.children()) {
                    std::string k = node_key(child);
                    std::string v = node_val(child);
                    if (k == "keyUsageBit" || k == "bit" || k == "index" || k == "usage_bit") {
                        try { bit = std::stoi(v); } catch (...) { bit = -1; }
                    } else if (k == "value" || k == "mustExist" || k == "must"
                               || k == "required" || k == "present" || k == "data") {
                        must_str = v;
                    } else if (k == "target") {
                        target = v;
                    }
                }
                if (bit < 0 || bit > 8) continue;

                if (is_issuer_target(target)) {
                    continue;
                }

                auto presence_opt = parse_presence(must_str);
                if (!presence_opt.has_value()) {
                    continue;
                }
                const Presence presence = *presence_opt;
                const bool present = ku_value.find(ku_names[bit]) != std::string::npos;

                switch (presence) {
                    case Presence::Required:
                        if (!present) {
                            EVLOG_warning << "keyUsage bit " << bit << " (" << ku_names[bit] << ") missing";
                            is_valid = 0;
                        }
                        break;
                    case Presence::Optional:
                        break;
                    case Presence::Forbidden:
                        if (present) {
                            EVLOG_warning << "keyUsage bit " << bit << " (" << ku_names[bit] << ") unexpected";
                            is_valid = 0;
                        }
                        break;
                }
            }
        }
    }

    {
        ryml::NodeRef bcNode = profileNode.find_child("basic_constraints");
        if (bcNode.readable()) {
            const std::string bc_value = wrapper.get_basic_constraints();
            for (auto bcRule : bcNode.children()) {
                std::string value_str, must_str, data_str, target;
                for (auto child : bcRule.children()) {
                    std::string k = node_key(child);
                    std::string v = node_val(child);
                    if      (k == "value" || k == "name")                    value_str = v;
                    else if (k == "mustExist" || k == "must")                must_str  = v;
                    else if (k == "data" || k == "val" || k == "expected")   data_str  = v;
                    else if (k == "target")                                  target    = v;
                }

                if (is_issuer_target(target)) {
                    continue;
                }

                auto presence_opt = parse_presence(must_str);
                if (!presence_opt.has_value()) {
                    continue;
                }
                const Presence presence = *presence_opt;

                if (value_str == "CA") {
                    const bool has_ca      = bc_value.find("CA:TRUE") != std::string::npos;
                    const bool expected_ca = parse_bool(data_str, false);

                    switch (presence) {
                        case Presence::Required:
                        case Presence::Optional:
                            if (has_ca != expected_ca) {
                                EVLOG_warning << "basicConstraints CA mismatch: expected "
                                              << (expected_ca ? "TRUE" : "FALSE")
                                              << ", got " << (has_ca ? "TRUE" : "FALSE");
                                is_valid = 0;
                            }
                            break;
                        case Presence::Forbidden:
                            EVLOG_warning << "basicConstraints CA: 'forbidden' is not meaningful";
                            is_valid = 0;
                            break;
                    }
                } else if (value_str == "path_length" || value_str == "pathlen") {
                    int actual = -1;
                    const std::string path_marker = "pathlen:";
                    auto ppos = bc_value.find(path_marker);
                    const bool present = (ppos != std::string::npos);
                    if (present) {
                        try {
                            actual = std::stoi(bc_value.substr(ppos + path_marker.length()));
                        } catch (...) {
                            actual = -1;
                        }
                    }

                    switch (presence) {
                        case Presence::Required:
                            if (!present) {
                                EVLOG_warning << "basicConstraints path_length missing";
                                is_valid = 0;
                            } else if (!data_str.empty()) {
                                int expected = -1;
                                try {
                                    expected = std::stoi(data_str);
                                } catch (...) {
                                    EVLOG_error << "basicConstraints path_length rule has non-numeric value '"
                                                << data_str << "'";
                                    is_valid = 0;
                                    break;
                                }
                                if (actual != expected) {
                                    EVLOG_warning << "basicConstraints path_length mismatch: expected "
                                                  << expected << ", got " << actual;
                                    is_valid = 0;
                                }
                            }
                            break;
                        case Presence::Optional:
                            if (present && !data_str.empty()) {
                                int expected = -1;
                                try {
                                    expected = std::stoi(data_str);
                                } catch (...) {
                                    EVLOG_error << "basicConstraints path_length rule has non-numeric value '"
                                                << data_str << "'";
                                    is_valid = 0;
                                    break;
                                }
                                if (actual != expected) {
                                    EVLOG_warning << "basicConstraints path_length mismatch: expected "
                                                  << expected << ", got " << actual;
                                    is_valid = 0;
                                }
                            }
                            break;
                        case Presence::Forbidden:
                            if (present) {
                                EVLOG_warning << "basicConstraints path_length unexpected";
                                is_valid = 0;
                            }
                            break;
                    }
                }
            }
        }
    }

    return is_valid;
}