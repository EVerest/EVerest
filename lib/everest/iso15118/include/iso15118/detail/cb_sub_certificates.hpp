// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>

#include <iso15118/detail/cb_exi.hpp>

// The SubCertificates of a cbv2g certificate chain, in both directions. ISO 15118-2 and ISO 15118-20
// generate the same shape, so one pair serves every chain type of either schema. The C++ side is any
// container of DER byte vectors with clear() and emplace_back(); the decoder bounds arrayLen by the
// generated array size, which matches the schema's maxOccurs and the fixed_vector capacity of the -20 types.
namespace iso15118 {

template <typename cb_SubCertificatesType, typename SubCertificates>
void sub_certificates_from_cb(const cb_SubCertificatesType& in, SubCertificates& out) {
    out.clear();
    for (uint16_t i = 0; i < in.Certificate.arrayLen; ++i) {
        const auto& cert = in.Certificate.array[i];
        out.emplace_back(cert.bytes, cert.bytes + cert.bytesLen);
    }
}

template <typename SubCertificates, typename cb_SubCertificatesType>
void sub_certificates_to_cb(const SubCertificates& in, cb_SubCertificatesType& out) {
    CPP2CB_ARRAY_SIZE_CHECK(in.size(), out.Certificate.array);
    out.Certificate.arrayLen = static_cast<uint16_t>(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        CPP2CB_BYTES(in[i], out.Certificate.array[i]);
    }
}

} // namespace iso15118
