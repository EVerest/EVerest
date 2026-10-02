#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <evse_security/evse_security.hpp>
#include <evse_security/certificate/x509_wrapper.hpp>
#include <string>

#pragma once

#ifndef ENFORCE_CERTIFICATE_RULES
#define ENFORCE_CERTIFICATE_RULES
enum class CertPart { Subject, Issuer };

struct CertRule {
    int nid;        
    bool mustExist;
    bool critical;    
    CertPart target;
    std::string val;
    int data;
};



int enforce_certificate_rules(const evse_security::X509Wrapper& wrapper, const std::string& manualCertType = ""); //enforces standards rules
#endif
