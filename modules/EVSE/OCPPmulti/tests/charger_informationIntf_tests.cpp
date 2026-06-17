// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   get_charger_information:

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

TEST_F(GenericOcppRequiresTester, subscribeGetChargerInformation) {
    // call_get_charger_information() used in
    // OCPP ready()

    std::vector<json> received;
    interfaces->subscribe_var("auth", "call_set_connection_timeout",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    // TODO(james-ctc): write test once get_charger_information() is used
}

} // namespace
