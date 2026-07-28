#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <evse_security/evse_security.hpp>
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



int enforce_certificate_rules(evse_security::X509Handle* ctx); //enforces standards rules
#endif
