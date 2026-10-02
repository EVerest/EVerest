// SPDX-License-Identifier: Apache-2.0
// Copyright Contributors to the EVerest Project.

#include "evse_security/utils/enforce_certificate_rules.hpp"
#include <algorithm>
#include <cctype>
#include <everest/logging.hpp>
#include <evse_security/certificate/x509_wrapper.hpp>
#include <filesystem>
#include <fstream>
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

inline bool is_issuer_target(const std::string& t) {
    std::string v = to_lower(t);
    return v == "issuer" || v == "issuer_cert" || v == "issuercertificate";
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
                if (mustExist.empty()) mustExist = "true";
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

    int is_valid = 1;

    auto log = [](bool critical, const std::string& msg) {
        if (critical) EVLOG_error << msg;
        else          EVLOG_warning << msg;
    };

    for (auto ruleNode : standNode.children()) {
        std::string field_str, mustExist_str, critical_str, expected_val, target_str;
        parse_stand_rule(ruleNode, field_str, mustExist_str, critical_str, expected_val, target_str);

        if (field_str.empty()) continue;

        const bool is_issuer = is_issuer_target(target_str);

        std::string cert_value;
        FieldResolve res = resolve_field(wrapper, field_str, is_issuer, cert_value);
        if (res == FieldResolve::SkipIssuer) {
            continue;
        }
        if (res == FieldResolve::Unknown) {
            EVLOG_debug << "Unknown field: " << field_str;
            continue;
        }

        const bool mustExist = parse_bool(mustExist_str, true);
        const bool critical  = parse_bool(critical_str, false);

        if (mustExist && cert_value.empty()) {
            log(critical, "Field " + field_str + " must exist but is missing");
            is_valid = 0;
        } else if (!mustExist && !cert_value.empty()) {
            log(critical, "Field " + field_str + " must NOT exist but has value: " + cert_value);
            is_valid = 0;
        } else if (mustExist && !expected_val.empty() && cert_value != expected_val) {
            log(critical, "Field " + field_str + " value mismatch: expected "
                          + expected_val + ", got " + cert_value);
            is_valid = 0;
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

                const bool must = parse_bool(must_str, true);
                const bool present = ku_value.find(ku_names[bit]) != std::string::npos;

                if (must && !present) {
                    EVLOG_warning << "keyUsage bit " << bit << " (" << ku_names[bit] << ") missing";
                    is_valid = 0;
                } else if (!must && present) {
                    EVLOG_warning << "keyUsage bit " << bit << " (" << ku_names[bit] << ") unexpected";
                    is_valid = 0;
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

                const bool must = parse_bool(must_str, false);

                if (value_str == "CA") {
                    const bool has_ca      = bc_value.find("CA:TRUE") != std::string::npos;
                    const bool expected_ca = parse_bool(data_str, false);
                    if (must && has_ca != expected_ca) {
                        EVLOG_warning << "basicConstraints CA mismatch: expected "
                                      << (expected_ca ? "TRUE" : "FALSE")
                                      << ", got " << (has_ca ? "TRUE" : "FALSE");
                        is_valid = 0;
                    }
                } else if (value_str == "path_length" || value_str == "pathlen") {
                    const bool present = bc_value.find("pathlen:") != std::string::npos;
                    if (must && !present) {
                        EVLOG_warning << "basicConstraints path_length missing";
                        is_valid = 0;
                    } else if (!must && present) {
                        EVLOG_warning << "basicConstraints path_length unexpected";
                        is_valid = 0;
                    }
                }
            }
        }
    }

    return is_valid;
}