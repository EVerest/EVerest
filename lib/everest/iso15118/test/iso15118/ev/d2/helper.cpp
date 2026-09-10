// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "helper.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>

#include <iso15118/io/stream_view.hpp>

ev::d2::Context& D2StateHelper::get_context() {
    return ctx;
}

DecodedRequests take_all_requests(ev::d2::MessageExchange& msg_exch) {
    DecodedRequests decoded;
    while (msg_exch.has_request()) {
        auto taken = msg_exch.take_request();
        if (not taken.has_value()) {
            break;
        }
        const auto& bytes = taken->first;
        decoded.add(std::make_unique<message_2::Variant>(io::StreamInputView{bytes.data(), bytes.size()}));
    }
    return decoded;
}

std::string make_test_ec_key_pem() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1);
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    std::string pem(data, static_cast<size_t>(n));
    BIO_free(bio);
    EVP_PKEY_free(pkey);
    return pem;
}
