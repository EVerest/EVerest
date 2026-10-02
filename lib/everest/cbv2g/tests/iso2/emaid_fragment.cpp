/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Contributors to EVerest */
//
// The eMAID element fragment, pinned against an independent EXI codec.
//
// ISO 15118-2 declares two elements whose qualified names differ only in case, EMAIDType and
// eMAIDType, so in a fragment grammar eMAID is ambiguous and must be coded generically. Getting
// this wrong is invisible from inside: a codec that encodes the fragment its own way and then
// verifies its own output agrees with itself whatever bytes it produced. The only way to see the
// error is to compare with someone else's bytes.
//
// The expected streams below were produced by Exificient, through the reference EVCC stack's
// codec, for the same Id and eMAID value. They cover Id lengths one through six, which is what
// moves the payload on and off a byte boundary, and several eMAID values. A CertificateInstallationRes
// signature is computed over this fragment, so a single wrong bit here rejects every Plug and Charge
// contract installation from a conformant charger.

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace {

std::string encode_emaid_fragment(const std::string& id, const std::string& value) {
    struct iso2_exiFragment fragment;
    init_iso2_exiFragment(&fragment);
    fragment.eMAID_isUsed = 1;
    fragment.eMAID.Id_isUsed = 1;
    std::memcpy(fragment.eMAID.Id.characters, id.data(), id.size());
    fragment.eMAID.Id.charactersLen = static_cast<uint16_t>(id.size());
    fragment.eMAID.CONTENT_isUsed = 1;
    std::memcpy(fragment.eMAID.CONTENT.characters, value.data(), value.size());
    fragment.eMAID.CONTENT.charactersLen = static_cast<uint16_t>(value.size());

    uint8_t buffer[256] = {};
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, buffer, sizeof(buffer), 0, nullptr);
    REQUIRE(encode_iso2_exiFragment(&stream, &fragment) == 0);

    static constexpr char HEX[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < exi_bitstream_get_length(&stream); ++i) {
        out.push_back(HEX[buffer[i] >> 4]);
        out.push_back(HEX[buffer[i] & 0x0F]);
    }
    return out;
}

struct Vector {
    const char* id;
    const char* value;
    const char* expected;
};

// Reference streams, Exificient, ISO 15118-2 MsgDef namespace.
constexpr Vector VECTORS[] = {
    {"i", "UKSWI123456789A", "80ec0201b4c0045552d4d5d24c4c8ccd0d4d8dce0e507d3d00"},
    {"id", "UKSWI123456789A", "80ec020234b240045552d4d5d24c4c8ccd0d4d8dce0e507d3d00"},
    {"id4", "UKSWI123456789A", "80ec0202b4b21a40045552d4d5d24c4c8ccd0d4d8dce0e507d3d00"},
    {"id44", "UKSWI123456789A", "80ec020334b21a1a40045552d4d5d24c4c8ccd0d4d8dce0e507d3d00"},
    {"id445", "UKSWI123456789A", "80ec0203b4b21a1a1ac0045552d4d5d24c4c8ccd0d4d8dce0e507d3d00"},
    {"id4456", "UKSWI123456789A", "80ec020434b21a1a1a9b40045552d4d5d24c4c8ccd0d4d8dce0e507d3d00"},
    {"id4", "DE12345678901A", "80ec0202b4b21a400411114c4c8ccd0d4d8dce0e4c0c507d3d00"},
    {"id4", "UKSWI123456789B", "80ec0202b4b21a40045552d4d5d24c4c8ccd0d4d8dce0e50bd3d00"},
    {"id1", "AB123456789012Z", "80ec0202b4b218c00450508c4c8ccd0d4d8dce0e4c0c4c96bd3d00"},
    {"id4", "FRA12345678901", "80ec0202b4b21a40041194904c4c8ccd0d4d8dce0e4c0c7d3d00"},
    {"id2", "UK012345678901", "80ec0202b4b21940041552cc0c4c8ccd0d4d8dce0e4c0c7d3d00"},
    {"id3", "DEPNXCONTRACT1", "80ec0202b4b219c004111154139610d3d395149050d50c7d3d00"},
};

} // namespace

SCENARIO("The eMAID element fragment encodes as the reference codec does") {
    for (const auto& vector : VECTORS) {
        INFO("Id=" << vector.id << " eMAID=" << vector.value);
        REQUIRE(encode_emaid_fragment(vector.id, vector.value) == vector.expected);
    }
}
