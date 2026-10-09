// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Pins how OCPP 1.6 connector ids are derived when an EVSE has more than one connector. Two numberings
// are in use and they disagree:
// - session events are translated with the connector mapping handed to the chargepoint, which numbers
//   every connector of every EVSE consecutively
// - errors carry the EVSE id, which the 1.6 chargepoint passes to libocpp as the connector id, and
//   NumberOfConnectors is set to the number of EVSEs
// With one connector per EVSE both give the same numbers. The legacy OCPP module behaves the same way.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"
#include "stubs/interfaces_stub.hpp"

namespace {

using EventInfo = ocpp_multi::GenericChargePointInterface::EventInfo;
using ::testing::_;

types::evse_manager::Evse make_evse(int id, int number_of_connectors) {
    types::evse_manager::Evse evse;
    evse.id = id;
    for (int connector_id = 1; connector_id <= number_of_connectors; ++connector_id) {
        types::evse_manager::Connector connector;
        connector.id = connector_id;
        connector.charge_mode = types::evse_manager::ChargeMode::AC;
        connector.hlc_capable = false;
        evse.connectors.push_back(connector);
    }
    return evse;
}

Everest::error::Error make_error(const Mapping& mapping) {
    Everest::error::Error error;
    error.type = "evse_board_support/MREC2GroundFailure";
    error.origin = ImplementationIdentifier("bsp_1", "main", mapping);
    return error;
}

// EVSE 1 has two connectors, EVSE 2 has one
class GenericOcppV16ConnectorNumbering : public stubs::GenericOcppNotStartedTester {
protected:
    void SetUp() override {
        GenericOcppNotStartedTester::SetUp();
        // answers the get_evse call ready() makes to each EVSE manager, in order
        interfaces->add_cmd_result(json(make_evse(1, 2)));
        interfaces->add_cmd_result(json(make_evse(2, 1)));
        start();
    }

    std::int32_t mapped_v16_connector(std::int32_t evse_id, std::int32_t connector_id) {
        return init_connector_mapping.at(evse_id).at(connector_id);
    }

    std::int32_t error_evse_id(const Mapping& mapping) {
        std::optional<EventInfo> event;
        EXPECT_CALL(chargepoint, on_event(_)).WillOnce([&event](const EventInfo& arg) { event = arg; });
        ocpp->cb_error_handler(make_error(mapping));
        testing::Mock::VerifyAndClearExpectations(&chargepoint);
        return event.has_value() ? event->evse_id : -1;
    }
};

TEST_F(GenericOcppV16ConnectorNumbering, ConnectorMappingNumbersEveryConnector) {
    const ocpp_multi::GenericChargePointInterface::ConnectorStructureV16 expected{{1, {{1, 1}, {2, 2}}}, {2, {{1, 3}}}};
    EXPECT_EQ(init_connector_mapping, expected);
}

// ChargePointV16::init sets NumberOfConnectors to the number of entries: the EVSE count, not the
// connector count
TEST_F(GenericOcppV16ConnectorNumbering, ConnectorStructureCountsEvses) {
    const ocpp_multi::GenericChargePointInterface::ConnectorStructure expected{{1, 2}, {2, 1}};
    EXPECT_EQ(init_evse_connector_structure, expected);
    EXPECT_EQ(init_evse_connector_structure.size(), 2U);
}

TEST_F(GenericOcppV16ConnectorNumbering, ErrorsAndSessionEventsUseDifferentConnectorIds) {
    // EVSE 1, connector 2
    EXPECT_EQ(error_evse_id(Mapping(1, 2)), 1);
    EXPECT_EQ(mapped_v16_connector(1, 2), 2);

    // EVSE 2, connector 1
    EXPECT_EQ(error_evse_id(Mapping(2, 1)), 2);
    EXPECT_EQ(mapped_v16_connector(2, 1), 3);
}

// the one case where both numberings agree
TEST_F(GenericOcppV16ConnectorNumbering, FirstConnectorOfFirstEvseAgrees) {
    EXPECT_EQ(error_evse_id(Mapping(1, 1)), 1);
    EXPECT_EQ(mapped_v16_connector(1, 1), 1);
}

} // namespace
