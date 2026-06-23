// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <net/ethernet.h>
#include <tuple>

#include <everest/slac/slac_defs.hpp>

namespace everest::lib::slac {

using MacAddress = std::array<std::uint8_t, ETH_ALEN>;
using StationId = std::array<std::uint8_t, defs::STATION_ID_LEN>;
using RunId = std::array<std::uint8_t, defs::RUN_ID_LEN>;
using Nmk = std::array<std::uint8_t, defs::NMK_LEN>;
using Nid = std::array<std::uint8_t, defs::NID_LEN>;

template <typename ByteArray> ByteArray byte_array_from_wire(const std::uint8_t* wire) {
    ByteArray out{};
    std::copy_n(wire, out.size(), out.begin());
    return out;
}

template <std::size_t WireSize, typename ByteArray>
void copy_to_wire(std::uint8_t (&wire)[WireSize], ByteArray const& bytes) {
    static_assert(std::tuple_size<ByteArray>::value == WireSize, "wire array and byte array sizes differ");
    std::copy(bytes.begin(), bytes.end(), wire);
}

template <std::size_t WireSize, std::size_t SourceSize>
void copy_wire(std::uint8_t (&wire)[WireSize], const std::uint8_t (&source)[SourceSize]) {
    static_assert(WireSize == SourceSize, "wire array sizes differ");
    std::copy_n(source, WireSize, wire);
}

template <std::size_t WireSize> void zero_wire(std::uint8_t (&wire)[WireSize]) {
    std::fill_n(wire, WireSize, std::uint8_t{0});
}

template <std::size_t WireSize, typename ByteArray>
bool wire_equal(const std::uint8_t (&wire)[WireSize], ByteArray const& bytes) {
    static_assert(std::tuple_size<ByteArray>::value == WireSize, "wire array and byte array sizes differ");
    return std::equal(bytes.begin(), bytes.end(), wire);
}

template <std::size_t WireSize, std::size_t SourceSize>
bool wire_equal(const std::uint8_t (&wire)[WireSize], const std::uint8_t (&source)[SourceSize]) {
    static_assert(WireSize == SourceSize, "wire array sizes differ");
    return std::equal(wire, wire + WireSize, source);
}

template <typename ByteArray> bool wire_pointer_equal(const std::uint8_t* wire, ByteArray const& bytes) {
    return std::equal(bytes.begin(), bytes.end(), wire);
}

} // namespace everest::lib::slac
