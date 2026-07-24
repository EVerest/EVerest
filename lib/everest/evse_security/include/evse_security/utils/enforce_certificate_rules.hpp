#include <vector>
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <evse_security/evse_security.hpp>
#include <iostream>

#pragma once

#ifndef EONTI_ADDONS
#define EONTI_ADDONS
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
