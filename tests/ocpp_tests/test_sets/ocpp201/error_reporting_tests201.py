# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import json
import logging

import pytest

from everest.testing.core_utils.controller.test_controller_interface import TestController
from everest.testing.ocpp_utils.fixtures import charge_point_v201
from everest.testing.ocpp_utils.charge_point_v201 import ChargePoint201
from everest.testing.ocpp_utils.charge_point_utils import TestUtility, wait_for_and_validate
from everest_test_utils import get_everest_config_path_str
from everest.testing.core_utils._configuration.everest_configuration_strategies.evse_board_support_api_configuration_strategy import \
    EvseBoardSupportApiConfigAdjustment

MREC_MODULE_ID = "bsp_1"
MREC_ERROR_TYPE = "MREC2GroundFailure"


def _raise_error_topic(test_controller: TestController) -> str:
    return f"{test_controller._mqtt_external_prefix}everest_api/1/evse_board_support/{MREC_MODULE_ID}/m2e/raise_error"


def _clear_error_topic(test_controller: TestController) -> str:
    return f"{test_controller._mqtt_external_prefix}everest_api/1/evse_board_support/{MREC_MODULE_ID}/m2e/clear_error"


def _validate_notify_event_tech_info(expected_tech_info, expected_cleared):
    """Returns a validate_payload_func for wait_for_and_validate that inspects the raw
    NotifyEvent.req `eventData` entries for a `techInfo`/`cleared` match, ignoring all
    other fields (e.g. eventId/timestamp/component/variable) that are not relevant to
    this test."""

    def _validate(meta_data, msg, exp_payload):
        if msg.action != "NotifyEvent":
            return False
        for event_data in msg.payload.get("eventData", []):
            if (
                event_data.get("techInfo") == expected_tech_info
                and event_data.get("cleared") == expected_cleared
            ):
                return True
        return False

    return _validate


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-ocpp201.yaml")
)
@pytest.mark.everest_config_adaptions(EvseBoardSupportApiConfigAdjustment())
async def test_error_with_message_sets_notify_event_tech_info_and_echoes_on_clear(
    charge_point_v201: ChargePoint201,
    test_utility: TestUtility,
    test_controller: TestController,
):
    logging.info(
        "######### test_error_with_message_sets_notify_event_tech_info_and_echoes_on_clear #########"
    )

    # An error that has a `message` value carries it in `techInfo` when sent to the CSMS
    message = "test error message"
    test_controller.publish(
        _raise_error_topic(test_controller),
        json.dumps({"type": MREC_ERROR_TYPE, "message": message}),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "NotifyEvent",
        {},
        validate_payload_func=_validate_notify_event_tech_info(message, False),
    )

    test_controller.publish(
        _clear_error_topic(test_controller),
        json.dumps({"type": MREC_ERROR_TYPE}),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "NotifyEvent",
        {},
        validate_payload_func=_validate_notify_event_tech_info(message, True),
    )


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp2.0.1")
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-ocpp201.yaml")
)
@pytest.mark.everest_config_adaptions(EvseBoardSupportApiConfigAdjustment())
async def test_error_without_message_sets_notify_event_tech_info_to_description_and_echoes_on_clear(
    charge_point_v201: ChargePoint201,
    test_utility: TestUtility,
    test_controller: TestController,
):
    logging.info(
        "######### test_error_without_message_sets_notify_event_tech_info_to_description_and_echoes_on_clear #########"
    )

    # An error without a `message` value carries EVerests default description for this error type in `techInfo`
    expected_tech_info = "Ground fault circuit interrupter has been activated."
    test_controller.publish(
        _raise_error_topic(test_controller),
        json.dumps({"type": MREC_ERROR_TYPE}),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "NotifyEvent",
        {},
        validate_payload_func=_validate_notify_event_tech_info(expected_tech_info, False),
    )

    test_controller.publish(
        _clear_error_topic(test_controller),
        json.dumps({"type": MREC_ERROR_TYPE}),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v201,
        "NotifyEvent",
        {},
        validate_payload_func=_validate_notify_event_tech_info(expected_tech_info, True),
    )
