// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <stdexcept>
#include <string>

namespace charge_bridge {

// Thrown by a bridge constructor when the configuration cannot be satisfied on this host and never
// will be, so retrying is pointless: a kernel capability the running kernel does not implement, for
// example. Ordinary construction failures (a device that needs CAP_NET_ADMIN, a symlink target that
// is not writable yet) must keep using plain exceptions - those are retried on purpose, because the
// cause can disappear while the process runs.
//
// create_bridge() latches this and disables the bridge for the lifetime of the process, so a host
// that can never satisfy the config does not create and destroy its host-local device on every
// retry cadence forever.
class permanent_bridge_failure : public std::runtime_error {
public:
    explicit permanent_bridge_failure(std::string const& what) : std::runtime_error(what) {
    }
};

} // namespace charge_bridge
