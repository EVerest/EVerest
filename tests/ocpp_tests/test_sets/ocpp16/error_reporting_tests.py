# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import json
import logging

import pytest

from everest.testing.core_utils.controller.test_controller_interface import TestController
from everest.testing.ocpp_utils.fixtures import charge_point_v16, central_system_v16
from everest.testing.ocpp_utils.charge_point_v16 import ChargePoint16
from everest.testing.ocpp_utils.charge_point_utils import TestUtility, wait_for_and_validate
from everest_test_utils import get_everest_config_path_str
from everest.testing.core_utils._configuration.everest_configuration_strategies.evse_board_support_api_configuration_strategy import \
    EvseBoardSupportApiConfigAdjustment

from ocpp.v16.enums import ChargePointErrorCode

MREC_MODULE_ID = "bsp_1"
MREC_ERROR_TYPE = "MREC2GroundFailure"
MREC_VENDOR_ID = "https://chargex.inl.gov"
MREC_VENDOR_ERROR_CODE = "CX002"

# The `info` field on a cleared error is generated from `vendorErrorCode`
MREC_CLEARED_INFO = f"Error {MREC_VENDOR_ERROR_CODE} resolved"

NON_MREC_ORIGIN = f"{MREC_MODULE_ID}->main"
NON_MREC_ERROR_TYPE = "VendorError"

# vendorId used by the evse_manager/Inoperative error sent by the EVSEManager
# to mark the connector as Faulted
INOPERATIVE_VENDOR_ID = "EVerest"


def _raise_error_topic(test_controller: TestController) -> str:
    return f"{test_controller._mqtt_external_prefix}everest_api/1/evse_board_support/{MREC_MODULE_ID}/m2e/raise_error"


def _clear_error_topic(test_controller: TestController) -> str:
    return f"{test_controller._mqtt_external_prefix}everest_api/1/evse_board_support/{MREC_MODULE_ID}/m2e/clear_error"


async def _validate_inoperative_raised(
    test_utility: TestUtility, charge_point_v16: ChargePoint16, caused_by: str, vendor_error_code: str
):
    """The board-support error drives EvseManager to raise evse_manager/Inoperative (a fault)."""
    return await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {
            "connector_id": 1,
            "error_code": ChargePointErrorCode.other_error,
            "info": f"caused_by:{caused_by}",
            "vendor_id": INOPERATIVE_VENDOR_ID,
            "vendor_error_code": vendor_error_code,
        },
    )


async def _validate_inoperative_cleared(
    test_utility: TestUtility, charge_point_v16: ChargePoint16, vendor_error_code: str
):
    """Clearing the underlying error clears Inoperative too; the connector returns to NoError."""
    return await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {
            "connector_id": 1,
            "error_code": ChargePointErrorCode.no_error,
            "info": f"Error {vendor_error_code} resolved",
            "vendor_id": INOPERATIVE_VENDOR_ID,
            "vendor_error_code": vendor_error_code,
        },
    )


@pytest.mark.asyncio
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-sil-ocpp.yaml")
)
@pytest.mark.everest_config_adaptions(EvseBoardSupportApiConfigAdjustment())
async def test_mrec_error_with_message_sets_status_notification_info(
    charge_point_v16: ChargePoint16,
    test_utility: TestUtility,
    test_controller: TestController,
):
    logging.info("######### test_mrec_error_with_message_sets_status_notification_info #########")

    # The `message` on an MREC error is sent to the CSMS in the `info` field
    message = "test error message"
    test_controller.publish(
        _raise_error_topic(test_controller),
        json.dumps({"type": MREC_ERROR_TYPE, "message": message}),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {
            "connector_id": 1,
            "error_code": ChargePointErrorCode.ground_failure,
            "info": message,
            "vendor_id": MREC_VENDOR_ID,
            "vendor_error_code": MREC_VENDOR_ERROR_CODE,
        },
    )

    # The ground fault also renders the EVSE inoperative
    assert await _validate_inoperative_raised(
        test_utility, charge_point_v16, f"evse_board_support/{MREC_ERROR_TYPE}", MREC_ERROR_TYPE
    )

    test_controller.publish(
        _clear_error_topic(test_controller),
        json.dumps({"type": MREC_ERROR_TYPE}),
    )

    # The MREC error is cleared first, leaving the EVSEManager's Inoperative error
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {
            "connector_id": 1,
            "info": MREC_CLEARED_INFO,
            "vendor_id": MREC_VENDOR_ID,
            "vendor_error_code": MREC_VENDOR_ERROR_CODE,
        },
    )

    # Inoperative clears too and the connector returns to NoError
    assert await _validate_inoperative_cleared(test_utility, charge_point_v16, MREC_ERROR_TYPE)


@pytest.mark.asyncio
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-sil-ocpp.yaml")
)
@pytest.mark.everest_config_adaptions(EvseBoardSupportApiConfigAdjustment())
async def test_mrec_error_without_message_is_unchanged_from_baseline(
    charge_point_v16: ChargePoint16,
    test_utility: TestUtility,
    test_controller: TestController,
):
    logging.info("######### test_mrec_error_without_message_is_unchanged_from_baseline #########")

    # Raising an MREC error with an empty message results in no `info` value being sent
    test_controller.publish(
        _raise_error_topic(test_controller),
        json.dumps({"type": MREC_ERROR_TYPE}),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {
            "connector_id": 1,
            "error_code": ChargePointErrorCode.ground_failure,
            "info": None,
            "vendor_id": MREC_VENDOR_ID,
            "vendor_error_code": MREC_VENDOR_ERROR_CODE,
        },
    )

    assert await _validate_inoperative_raised(
        test_utility, charge_point_v16, f"evse_board_support/{MREC_ERROR_TYPE}", MREC_ERROR_TYPE
    )

    test_controller.publish(
        _clear_error_topic(test_controller),
        json.dumps({"type": MREC_ERROR_TYPE}),
    )

    # Even when there is no `message` on the original error, the `info` value that is generated from
    # the `vendor_error_code` should still be present
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {
            "connector_id": 1,
            "info": MREC_CLEARED_INFO,
            "vendor_id": MREC_VENDOR_ID,
            "vendor_error_code": MREC_VENDOR_ERROR_CODE,
        },
    )

    assert await _validate_inoperative_cleared(test_utility, charge_point_v16, MREC_ERROR_TYPE)


@pytest.mark.asyncio
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-sil-ocpp.yaml")
)
@pytest.mark.everest_config_adaptions(EvseBoardSupportApiConfigAdjustment())
async def test_non_mrec_error_with_message_is_unchanged_from_baseline(
    charge_point_v16: ChargePoint16,
    test_utility: TestUtility,
    test_controller: TestController,
):
    logging.info("######### test_non_mrec_error_with_message_is_unchanged_from_baseline #########")

    # On non-MREC errors, the `message` gets sent in the `vendorId` field, `info` carries the error origin
    sub_type = "some_subtype"
    message = "some vendor diagnostic text"
    non_mrec_vendor_error_code = f"{NON_MREC_ERROR_TYPE}/{sub_type}"
    test_controller.publish(
        _raise_error_topic(test_controller),
        json.dumps({"type": NON_MREC_ERROR_TYPE, "sub_type": sub_type, "message": message}),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {
            "connector_id": 1,
            "error_code": ChargePointErrorCode.other_error,
            "info": NON_MREC_ORIGIN,
            "vendor_id": message,
            "vendor_error_code": non_mrec_vendor_error_code,
        },
    )

    assert await _validate_inoperative_raised(
        test_utility, charge_point_v16, f"evse_board_support/{NON_MREC_ERROR_TYPE}", non_mrec_vendor_error_code
    )

    test_controller.publish(
        _clear_error_topic(test_controller),
        json.dumps({"type": NON_MREC_ERROR_TYPE, "sub_type": sub_type}),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {
            "connector_id": 1,
            "info": f"Error {non_mrec_vendor_error_code} resolved",
            "vendor_id": message,
            "vendor_error_code": non_mrec_vendor_error_code,
        },
    )

    assert await _validate_inoperative_cleared(test_utility, charge_point_v16, non_mrec_vendor_error_code)


@pytest.mark.asyncio
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-sil-ocpp.yaml")
)
@pytest.mark.everest_config_adaptions(EvseBoardSupportApiConfigAdjustment())
async def test_mrec_error_with_overlong_message_is_truncated(
    charge_point_v16: ChargePoint16,
    test_utility: TestUtility,
    test_controller: TestController,
):
    logging.info("######### test_mrec_error_with_overlong_message_is_truncated #########")

    # Too long `message` values on MREC errors are truncated, but still sent
    long_message = "x" * 60
    test_controller.publish(
        _raise_error_topic(test_controller),
        json.dumps({"type": MREC_ERROR_TYPE, "message": long_message}),
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {
            "connector_id": 1,
            "error_code": ChargePointErrorCode.ground_failure,
            "info": long_message[:50],
            "vendor_id": MREC_VENDOR_ID,
            "vendor_error_code": MREC_VENDOR_ERROR_CODE,
        },
    )

    assert await _validate_inoperative_raised(
        test_utility, charge_point_v16, f"evse_board_support/{MREC_ERROR_TYPE}", MREC_ERROR_TYPE
    )

    test_controller.publish(
        _clear_error_topic(test_controller),
        json.dumps({"type": MREC_ERROR_TYPE}),
    )

    # The synthesized clear `info` is derived from the vendorErrorCode, unaffected by the truncated message
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {
            "connector_id": 1,
            "info": MREC_CLEARED_INFO,
            "vendor_id": MREC_VENDOR_ID,
            "vendor_error_code": MREC_VENDOR_ERROR_CODE,
        },
    )

    assert await _validate_inoperative_cleared(test_utility, charge_point_v16, MREC_ERROR_TYPE)
