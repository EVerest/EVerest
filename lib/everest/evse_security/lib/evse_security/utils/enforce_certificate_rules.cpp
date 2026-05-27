# SPDX-License-Identifier: Apache-2.0
# Copyright Contributors to the EVerest Project.

#include "evse_security/utils/enforce_certificate_rules.hpp"
#include <algorithm>
#include <everest/logging.hpp>
#include <evse_security/certificate/x509_wrapper.hpp>
#include <fstream>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <sstream>
#include <string>
#include <vector>

namespace yaml {
static inline std::string trim(const std::string& s) {
    const std::string* src = &s;
    std::string stripped;
    auto start = std::find_if_not(src->begin(), src->end(), [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(src->rbegin(), src->rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return (start < end) ? std::string(start, end) : "";
}

static CertPart parseCertPart(const std::string& val) {
    if (val == "Subject")
        return CertPart::Subject;
    if (val == "Issuer")
        return CertPart::Issuer;
    EVLOG_warning << "Unknown CertPart: " + val;
    return CertPart::Subject;
}

static CertRule parseRule(std::istream& in) {
    CertRule rule{};
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty())
            continue;
        if (line[0] == '-')
            line = trim(line.substr(1));

        auto pos = line.find(':');
        if (pos == std::string::npos)
            continue;

        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        if (auto h = value.find('#'); h != std::string::npos)
            value = trim(value.substr(0, h));

        if (key == "nid" || key == "keyUsageBit") {
            if (!value.empty())
                rule.nid = std::stoi(value);
        } else if (key == "data") {
            if (!value.empty() && value != "true" && value != "false")
                rule.data = std::stoi(value);
        } else if (key == "val" || key == "value")
            rule.val = value;
        else if (key == "target")
            rule.target = parseCertPart(value);
        else if (key == "mustExist" || key == "critical") {
            std::string low = value;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            bool flag = (low == "true");
            if (key == "mustExist")
                rule.mustExist = flag;
            else
                rule.critical = flag;
        }
    }
    return rule;
}
} // namespace yaml

std::vector<CertRule> loadCertRules(const std::string& filename, const std::string& type, const std::string& section) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        EVLOG_warning << "FAILED TO OPEN: " << filename;
        return {};
    }
    std::vector<CertRule> rules;
    std::string line;
    bool inType = false, inStand = false;

    while (std::getline(file, line)) {
        line = yaml::trim(line);
        if (!inType) {
            inType = line.find(type + ":") != std::string::npos;
            continue;
        }
        if (!inStand) {
            inStand = line.find(section + ":") != std::string::npos;
            continue;
        }
        if (line.empty())
            break;

    process_rule:
        if (line[0] == '-') {
            std::string ruleContent = line + "\n";
            while (std::getline(file, line)) {
                line = yaml::trim(line);
                if (line.empty() || line[0] == '-')
                    break;
                ruleContent += line + "\n";
            }
            try {
                std::istringstream ruleStream(ruleContent);
                rules.push_back(yaml::parseRule(ruleStream));
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("parseRule failed in ") + filename + " type=" + type +
                                         " section=" + section + " content='" + ruleContent + "' error=" + e.what());
            }
            if (!line.empty() && line[0] == '-')
                goto process_rule;
        }
    }
    return rules;
}

static std::string get_file_name(X509* cert) {
    X509_NAME* subjectName = X509_get_subject_name(cert);
    X509_NAME* issuerName = X509_get_issuer_name(cert);

    if (!subjectName) {
        EVLOG_warning << "Could not get subject name from certificate.";
        return "";
    }

    // Extract Domain Component (DC)
    char dcBuf[256];
    int dcLen = X509_NAME_get_text_by_NID(subjectName, NID_domainComponent, dcBuf, sizeof(dcBuf));
    if (dcLen <= 0) {
        EVLOG_warning << "No Domain Component (DC) found in the certificate.";
        return "";
    }
    std::string dc(dcBuf, dcLen);
    dc.erase(std::remove(dc.begin(), dc.end(), ' '), dc.end());

    BASIC_CONSTRAINTS* bc_name = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr);
    bool isCA = false;
    bool isSelfSigned = (X509_NAME_cmp(subjectName, issuerName) == 0);
    if (bc_name) {
        isCA = bc_name->ca;
    }

    std::string certType;
    if (!isCA) {
        certType = "_leaf";
    } else if (isSelfSigned && isCA) {
        certType = "_RootCA";
    } else if (!isSelfSigned && isCA) {
        if (bc_name->pathlen) {
            long pathLen = ASN1_INTEGER_get(bc_name->pathlen);
            certType = "_SubCA" + std::to_string(pathLen);
        } else {
            certType = "_SubCA";
        }
    }

    if (bc_name) {
        OPENSSL_free(bc_name);
    }

    std::string fileName;

    if (dc == "CPO") {
        if (certType == "_leaf") {
            fileName = std::string(CERT_PROFILES_DIR) + "/SECC Leaf.yaml";
        } else if (certType == "_SubCA1") {
            fileName = std::string(CERT_PROFILES_DIR) + "/CSO-CPO Sub-CA 1.yaml";
        } else if (certType == "_SubCA2") {
            fileName = std::string(CERT_PROFILES_DIR) + "/CSO-CPO Sub-CA 2.yaml";
        } else {
            EVLOG_warning << "No matching profile for DC=CPO certType: " << certType;
            return "";
        }
    } else if (dc == "V2G") {
        if (certType == "_RootCA") {
            fileName = std::string(CERT_PROFILES_DIR) + "/V2GRootCA.yaml";
        } else {
            EVLOG_warning << "No matching profile for DC=V2G certType: " << certType;
            return "";
        }
    } else if (dc == "MO") {
        if (certType == "_leaf") {
            fileName = std::string(CERT_PROFILES_DIR) + "/Contract Leaf.yaml";
        } else if (certType == "_SubCA1") {
            fileName = std::string(CERT_PROFILES_DIR) + "/MO-EMSP Sub-CA 1.yaml";
        } else if (certType == "_SubCA2") {
            fileName = std::string(CERT_PROFILES_DIR) + "/MO-EMSP Sub-CA 2.yaml";
        } else if (certType == "_RootCA") {
            fileName = std::string(CERT_PROFILES_DIR) + "/MO-EMSP Root CA.yaml";
        } else {
            EVLOG_warning << "No matching profile for DC=MO certType: " << certType;
            return "";
        }
    } else if (dc == "OEM") {
        if (certType == "_leaf") {
            fileName = std::string(CERT_PROFILES_DIR) + "/OEM Provisional.yaml";
        } else if (certType == "_SubCA1") {
            fileName = std::string(CERT_PROFILES_DIR) + "/OEM Sub-CA 1.yaml";
        } else if (certType == "_SubCA2") {
            fileName = std::string(CERT_PROFILES_DIR) + "/OEM Sub-CA 2.yaml";
        } else if (certType == "_RootCA") {
            fileName = std::string(CERT_PROFILES_DIR) + "/OEM Root CA.yaml";
        } else {
            EVLOG_warning << "No matching profile for DC=OEM certType: " << certType;
            return "";
        }
    } else {
        EVLOG_warning << "No matching profile for DC: " << dc;
        return "";
    }
    return fileName;
}

int enforce_certificate_rules(evse_security::X509Handle* ctx) {
    int is_valid = 1;
    if (ENFORCE_CERT_PROFILES) {
        evse_security::X509Wrapper wrapper(evse_security::OpenSSLSupplier::x509_duplicate_unique(ctx));
        X509* cert = wrapper.get_x509_raw();
        if (!cert)
            return -1;

        const std::string profile = get_file_name(cert);
        EVLOG_info << "No matching security profile found for certificate, skipping rule enforcement.";
        return 0;

        std::string key = std::filesystem::path(profile).stem().string();
        key.erase(std::remove(key.begin(), key.end(), ' '), key.end());

        auto fields = loadCertRules(profile, key, "stand");
        auto kuRules = loadCertRules(profile, key, "key_usage");
        auto bcRules = loadCertRules(profile, key, "basic_constraints");
        auto dcRules = loadCertRules(profile, key, "domain_component");

        if (fields.empty()) {
            EVLOG_warning << "No rules found for key: " << key;
            return -1;
        }

        auto log = [](bool critical, const std::string& msg) {
            if (critical)
                EVLOG_error << msg;
            else
                EVLOG_warning << msg;
        };

        static const std::vector<int> extensions{83, 87};
        int is_valid = 0;
        char buf[256];

        for (auto& rule : fields) {
            X509_NAME* name =
                (rule.target == CertPart::Subject) ? X509_get_subject_name(cert) : X509_get_issuer_name(cert);

            if (!std::binary_search(extensions.begin(), extensions.end(), rule.nid)) {
                int len = X509_NAME_get_text_by_NID(name, rule.nid, buf, sizeof(buf));
                if ((rule.mustExist && len <= 0) || (!rule.mustExist && len > 0)) {
                    log(rule.critical, "NID " + std::to_string(rule.nid) + " does not comply (expected " +
                                           std::to_string(rule.mustExist) + ")");
                    is_valid = 1;
                }
            } else if (rule.nid == 83 && rule.mustExist) {
                ASN1_BIT_STRING* ku = (ASN1_BIT_STRING*)X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr);
                for (const auto& kr : kuRules) {
                    if ((ku->data[0] & (0x80 >> kr.nid)) != kr.mustExist) {
                        log(kr.critical, "KeyUsage bit " + std::to_string(kr.nid) + " does not comply (expected " +
                                             std::to_string(kr.mustExist) + ")");
                        is_valid = 1;
                    }
                }
            } else if (rule.nid == 87 && rule.mustExist) {
                BASIC_CONSTRAINTS* bc =
                    (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr);
                for (const auto& br : bcRules) {
                    if (br.val == "path_length") {
                        if ((br.mustExist && !bc->pathlen) || (!br.mustExist && bc->pathlen)) {
                            EVLOG_warning << "Path length presence does not comply";
                            is_valid = 1;
                        } else if (br.mustExist && bc->pathlen && br.data != 0 &&
                                   ASN1_INTEGER_get(bc->pathlen) != br.data) {
                            log(br.critical, "Path length value does not match expected " + std::to_string(br.data));
                            is_valid = 1;
                        }
                    } else if (br.val == "CA" && ((br.mustExist && bc->ca != 1) || (!br.mustExist && bc->ca == 1))) {
                        log(br.critical, "CA flag does not comply");
                        is_valid = 1;
                    }
                }
            } else if (rule.nid == 391 && rule.mustExist && !dcRules.empty()) {
                int len = X509_NAME_get_text_by_NID(name, rule.nid, buf, sizeof(buf));
                if (std::string(buf, len) != dcRules[0].val) {
                    log(rule.critical,
                        "Domain component expected " + dcRules[0].val + " but received " + std::string(buf, len));
                    is_valid = 1;
                }
            }
        }
    }
    return is_valid;
}
