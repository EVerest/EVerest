import asyncio

import pytest

from everest.testing.core_utils.controller.test_controller_interface import (
    TestController,
)
from everest.testing.ocpp_utils.charge_point_utils import (
    wait_for_and_validate,
    wait_for_payload,
    TestUtility,
)
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.ocpp_utils.central_system import CentralSystem, ChargePoint16
from everest.testing.ocpp_utils.fixtures import test_utility, charge_point_v16
from everest.testing.core_utils._configuration.libocpp_configuration_helper import GenericOCPP16ConfigAdjustment
from everest_test_utils import get_everest_config_path_str, OcppTestConfiguration
from everest_test_utils_probe_modules import implement_ocpp16_probe_commands

from ocpp.v16 import call, call_result

from copy import deepcopy
from typing import Dict

from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import \
    EverestConfigAdjustmentStrategy
from everest.testing.core_utils._configuration.everest_configuration_strategies.yeti_simulator_disable_meter_transaction_start_strategy import \
    YetiSimulatorDisableMeterTransactionStartStrategy


@pytest.mark.asyncio
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-two-connectors.yaml")
)
@pytest.mark.ocpp_config_adaptions(
    GenericOCPP16ConfigAdjustment(
        [("Internal", "MeterPublicKeys", ["TESTPUBLICKEY1", "TESTPUBLICKEY2"])]
    )
)
async def test_meter_public_key(
    charge_point_v16: ChargePoint16, test_utility: TestUtility
):
    await charge_point_v16.get_configuration_req(key=["MeterPublicKey1"])
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "GetConfiguration",
        call_result.GetConfiguration(
            [{"key": "MeterPublicKey1", "readonly": True, "value": "TESTPUBLICKEY1"}]
        ),
    )

    await charge_point_v16.get_configuration_req(key=["MeterPublicKey2"])
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "GetConfiguration",
        call_result.GetConfiguration(
            [{"key": "MeterPublicKey2", "readonly": True, "value": "TESTPUBLICKEY2"}]
        ),
    )

    await charge_point_v16.get_configuration_req(key=["MeterPublicKey3"])

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "GetConfiguration",
        {"unknownKey": ["MeterPublicKey3"]}
    )

    test_utility.messages.clear()

    response : call_result.GetConfiguration = await charge_point_v16.get_configuration_req()

    assert any(
        entry['key'] == "MeterPublicKey1" and entry['value'] == "TESTPUBLICKEY1"
        for entry in response.configuration_key)

    assert any(
        entry['key'] == "MeterPublicKey2" and entry['value'] == "TESTPUBLICKEY2"
        for entry in response.configuration_key)

    await charge_point_v16.get_configuration_req(key=["MeterPublicKey"])

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "GetConfiguration",
        {"unknownKey": ["MeterPublicKey"]}
    )

    await charge_point_v16.get_configuration_req(key=["MeterPublicKeyMeterPublicKey1"])

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "GetConfiguration",
        {"unknownKey": ["MeterPublicKeyMeterPublicKey1"]}
    )

    await charge_point_v16.get_configuration_req(key=["MeterPublicKey1X"])

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "GetConfiguration",
        {"unknownKey": ["MeterPublicKey1X"]}
    )

    await charge_point_v16.get_configuration_req(key=["MeterPublicKey1X"])

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "GetConfiguration",
        {"unknownKey": ["MeterPublicKey1X"]}
    )

    await charge_point_v16.get_configuration_req(key=["MeterPublicKeybanana"])

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "GetConfiguration",
        {"unknownKey": ["MeterPublicKeybanana"]}
    )

    await charge_point_v16.get_configuration_req(key=["MeterPublicKey0"])

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "GetConfiguration",
        {"unknownKey": ["MeterPublicKey0"]}
    )

    await charge_point_v16.change_configuration_req(
        key="MeterPublicKey1", value="TEST"
    )
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "ChangeConfiguration",
        {"status": "Rejected"}
    )


def _meter_public_key_entry(connector_id: int, value: str) -> Dict:
    return {"key": f"MeterPublicKey{connector_id}", "readonly": True, "value": value}


async def _reported_meter_public_keys(charge_point: ChargePoint16) -> list:
    response: call_result.GetConfiguration = await charge_point.get_configuration_req()
    return sorted(
        (entry for entry in response.configuration_key if entry["key"].startswith("MeterPublicKey")),
        key=lambda entry: entry["key"],
    )


async def _wait_for_reported_meter_public_keys(charge_point: ChargePoint16, expected: list, timeout=10):
    async def _await_reported():
        while await _reported_meter_public_keys(charge_point) != expected:
            await asyncio.sleep(0.1)

    await asyncio.wait_for(_await_reported(), timeout=timeout)


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp1.6")
@pytest.mark.everest_core_config("everest-config-ocpp16-probe-module.yaml")
@pytest.mark.ocpp_config_adaptions(
    GenericOCPP16ConfigAdjustment([("Core", "NumberOfConnectors", 2)])
)
@pytest.mark.probe_module
async def test_meter_public_key_published_by_powermeter(
    started_test_controller, everest_core, central_system: CentralSystem
):
    """The keys are not configured: a connector reports its key once its powermeter has published it."""
    probe_module = ProbeModule(everest_core.get_runtime_session())
    implement_ocpp16_probe_commands(probe_module)
    probe_module.start()
    await probe_module.wait_to_be_ready()
    probe_module.publish_variable("evse_manager", "ready", True)
    probe_module.publish_variable("evse_manager_b", "ready", True)
    charge_point: ChargePoint16 = await central_system.wait_for_chargepoint()

    assert await _reported_meter_public_keys(charge_point) == []
    response: call_result.GetConfiguration = await charge_point.get_configuration_req(
        key=["MeterPublicKey1", "MeterPublicKey2"]
    )
    assert not response.configuration_key
    assert response.unknown_key == ["MeterPublicKey1", "MeterPublicKey2"]

    key_1 = _meter_public_key_entry(1, "PROBEPUBLICKEY1")
    key_2 = _meter_public_key_entry(2, "PROBEPUBLICKEY2")

    probe_module.publish_variable("evse_manager", "powermeter_public_key_ocmf", key_1["value"])
    probe_module.publish_variable("evse_manager_b", "powermeter_public_key_ocmf", key_2["value"])
    await _wait_for_reported_meter_public_keys(charge_point, [key_1, key_2])

    response = await charge_point.get_configuration_req(key=["MeterPublicKey1"])
    assert response.configuration_key == [key_1]
    response = await charge_point.get_configuration_req(key=["MeterPublicKey2"])
    assert response.configuration_key == [key_2]


@pytest.mark.asyncio
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-two-connectors.yaml")
)
@pytest.mark.ocpp_config_adaptions(
    GenericOCPP16ConfigAdjustment([("Core", "ClockAlignedDataInterval", 0)])
)
async def test_meter_signed_meter_values(
    charge_point_v16: ChargePoint16, test_utility: TestUtility, test_controller: TestController, test_config: OcppTestConfiguration,
):
    test_controller.plug_in(1)

    assert test_config.authorization_info is not None
    test_controller.swipe(test_config.authorization_info.valid_id_tag_1)
    # expect authorize.req
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "Authorize",
        {}
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StartTransaction",
        {},
    )

    # expect StatusNotification with status charging
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {}
    )

    # Because the StartTransaction message can not contain a signed meter value, it is sent in a separate MeterValues message
    meter_values_msg: call.MeterValues = call.MeterValues(
        **await wait_for_payload(
            test_utility,
            charge_point_v16,
            "MeterValues",
            {},
        )
    )

    assert meter_values_msg is not None
    start_sv = None
    for mv in meter_values_msg.meter_value:
        for sv in mv['sampled_value']:
            if sv['context'] == 'Transaction.Begin':
                assert start_sv is None, "MeterValues message contains multiple transaction start signed meter values"
                start_sv = sv
    assert start_sv is not None, "MeterValues message does not contain a transaction start signed meter value"
    assert start_sv['value'] == "test start value"
    assert start_sv['format'] == 'SignedData'

    assert any(
        sv.get('context') == 'Transaction.Begin'
        and sv.get('format') == 'SignedData'
        and sv.get('value') == 'test start value'
        for mv in meter_values_msg.meter_value
        for sv in mv['sampled_value']
    ), "Initial signed meter value not found in MeterValues"

    test_controller.plug_out()

    # expect StopTransaction.req
    stop_transaction_msg: call.StopTransaction = call.StopTransaction(
        **await wait_for_payload(
            test_utility,
            charge_point_v16,
            "StopTransaction",
            {},
        )
    )

    # verify start and stop signed meter values (order independent)
    assert stop_transaction_msg is not None
    assert stop_transaction_msg.transaction_data is not None
    start_sv = None
    stop_sv = None
    for mv in stop_transaction_msg.transaction_data:
        for sv in mv['sampled_value']:
            if sv['context'] == 'Transaction.Begin':
                assert start_sv is None, "transaction_data contains multiple start signed meter values"
                start_sv = sv
            elif sv['context'] == 'Transaction.End':
                assert stop_sv is None, "transaction_data contains multiple stop signed meter values"
                stop_sv = sv

    assert start_sv is not None, "Start signed meter value not found"
    assert start_sv['value'] == "test start value"
    assert start_sv['format'] == 'SignedData'

    assert stop_sv is not None, "Stop signed meter value not found"
    assert stop_sv['value'] == "test stop value"
    assert stop_sv['format'] == 'SignedData'


@pytest.mark.asyncio
@pytest.mark.everest_core_config(
    get_everest_config_path_str("everest-config-two-connectors.yaml")
)
@pytest.mark.ocpp_config_adaptions(
    GenericOCPP16ConfigAdjustment([("Core", "ClockAlignedDataInterval", 0)])
)
@pytest.mark.everest_config_adaptions(YetiSimulatorDisableMeterTransactionStartStrategy())
async def test_meter_signed_meter_values_no_start(
    charge_point_v16: ChargePoint16, test_utility: TestUtility, test_controller: TestController, test_config: OcppTestConfiguration,
):
    test_controller.plug_in(1)

    assert test_config.authorization_info is not None
    test_controller.swipe(test_config.authorization_info.valid_id_tag_1)
    # expect authorize.req
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "Authorize",
        {}
    )

    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StartTransaction",
        {},
    )

    # expect StatusNotification with status charging
    assert await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "StatusNotification",
        {}
    )

    # Assert that no MeterValues message is sent when the meter transaction starts
    assert not await wait_for_and_validate(
        test_utility,
        charge_point_v16,
        "MeterValues",
        {},
        timeout=5,
    ), "No MeterValues message should be sent at transaction start when dummy_meter_value_send_on_transaction_start is disabled"

    test_controller.plug_out()

    # expect StopTransaction.req
    stop_transaction_msg: call.StopTransaction = call.StopTransaction(
        **await wait_for_payload(
            test_utility,
            charge_point_v16,
            "StopTransaction",
            {},
        )
    )

    # verify start and stop signed meter values (order independent)
    assert stop_transaction_msg is not None
    assert stop_transaction_msg.transaction_data is not None
    start_sv = None
    stop_sv = None
    for mv in stop_transaction_msg.transaction_data:
        for sv in mv['sampled_value']:
            if sv['context'] == 'Transaction.Begin':
                assert start_sv is None, "transaction_data contains multiple start signed meter values"
                start_sv = sv
            elif sv['context'] == 'Transaction.End':
                assert stop_sv is None, "transaction_data contains multiple stop signed meter values"
                stop_sv = sv

    assert start_sv is not None, "Start signed meter value not found"
    assert start_sv['value'] == "test start value"
    assert start_sv['format'] == 'SignedData'

    assert stop_sv is not None, "Stop signed meter value not found"
    assert stop_sv['value'] == "test stop value"
    assert stop_sv['format'] == 'SignedData'
