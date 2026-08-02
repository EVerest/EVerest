# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
from dataclasses import dataclass
from unittest.mock import Mock, call as mock_call, ANY

import pytest
import pytest_asyncio
from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule
from everest.testing.ocpp_utils.central_system import CentralSystem
from everest.testing.ocpp_utils.charge_point_v16 import ChargePoint16
from ocpp.v16.call import SetChargingProfile

from everest.testing.core_utils._configuration.libocpp_configuration_helper import (
    GenericOCPP16ConfigAdjustment,
)
from everest_test_utils_probe_modules import implement_ocpp16_probe_commands


@dataclass
class _OCPP16GenericInterfaceIntegrationEnvironment:
    csms_mock: Mock
    central_system: CentralSystem
    everest_core: EverestCore
    probe_module: ProbeModule
    probe_module_command_mocks: dict[str, dict[str, Mock]]
    charge_point: ChargePoint16


@pytest_asyncio.fixture
async def _env(
    everest_core,
    test_controller,
    central_system: CentralSystem,
    skip_implementation,
    overwrite_implementation,
):
    test_controller.start()
    csms_mock = central_system.mock

    probe_module = ProbeModule(everest_core.get_runtime_session())
    probe_module_command_mocks = implement_ocpp16_probe_commands(
        probe_module, skip_implementation, overwrite_implementation
    )

    probe_module.start()
    await probe_module.wait_to_be_ready()
    for evse_manager in ["evse_manager", "evse_manager_b"]:
        probe_module.publish_variable(evse_manager, "ready", True)

    await central_system.wait_for_chargepoint()

    yield _OCPP16GenericInterfaceIntegrationEnvironment(
        csms_mock,
        central_system,
        everest_core,
        probe_module,
        probe_module_command_mocks,
        central_system.chargepoint,
    )
    test_controller.stop()


class CSMSConnectionUtils:
    def __init__(self, central_system: CentralSystem):
        self._central_system = central_system

    @property
    def is_connected(self) -> bool:
        if not self._central_system.ws_server.websockets:
            return False
        assert len(self._central_system.ws_server.websockets) == 1
        connection = next(iter(self._central_system.ws_server.websockets))
        return connection.open


async def wait_for_mock_called(mock, call=None, timeout=10):
    async def _await_called():
        while not mock.call_count or (call and call not in mock.mock_calls):
            await asyncio.sleep(0.1)

    await asyncio.wait_for(_await_called(), timeout=timeout)


async def wait_for_connection_state(csms_connection, connected, timeout=15):
    async def _await_state():
        while csms_connection.is_connected != connected:
            await asyncio.sleep(0.1)

    await asyncio.wait_for(_await_state(), timeout=timeout)


@pytest.mark.ocpp_version("ocpp1.6")
@pytest.mark.everest_core_config("everest-config-ocpp16-probe-module.yaml")
@pytest.mark.inject_csms_mock
@pytest.mark.probe_module(connections={"ocpp": [Requirement("ocpp", "ocpp_generic")]})
@pytest.mark.asyncio
class TestOCPP16GenericInterfaceIntegration:

    async def test_command_stop(self, _env):
        csms_connection = CSMSConnectionUtils(_env.central_system)
        assert csms_connection.is_connected
        res = await _env.probe_module.call_command("ocpp", "stop", None)
        assert res is True
        await wait_for_connection_state(csms_connection, connected=False)
        assert not csms_connection.is_connected

    async def test_command_restart(self, _env):
        csms_connection = CSMSConnectionUtils(_env.central_system)
        await _env.probe_module.call_command("ocpp", "stop", None)
        await wait_for_connection_state(csms_connection, connected=False)
        assert not csms_connection.is_connected
        res = await _env.probe_module.call_command("ocpp", "restart", None)
        await wait_for_connection_state(csms_connection, connected=True)
        assert res is True
        assert csms_connection.is_connected

    async def test_command_restart_denied(self, _env):
        csms_connection = CSMSConnectionUtils(_env.central_system)
        res = await _env.probe_module.call_command("ocpp", "restart", None)
        assert res is False
        assert csms_connection.is_connected

    async def test_command_security_event(self, _env):
        res = await _env.probe_module.call_command(
            "ocpp",
            "security_event",
            {
                "event": {
                    "type": "SecurityLogWasCleared",
                    "info": "integration_test_security_info",
                    "critical": True,
                    "timestamp": "2024-01-01T12:00:00",
                }
            },
        )
        assert res is None
        await wait_for_mock_called(
            _env.csms_mock.on_security_event_notification,
            mock_call(
                tech_info="integration_test_security_info",
                timestamp=ANY,
                type="SecurityLogWasCleared",
            ),
        )

        string_too_long = "WAYTOOLONG"*255
        res = await _env.probe_module.call_command(
            "ocpp",
            "security_event",
            {
                "event": {
                    "type": string_too_long,
                    "info": string_too_long,
                    "critical": True,
                    "timestamp": "2024-01-01T12:00:00",
                }
            },
        )
        await wait_for_mock_called(
            _env.csms_mock.on_security_event_notification,
            mock_call(
                # truncated to 255 characters
                tech_info=string_too_long[0:255],
                timestamp=ANY,
                # truncated to 50 characters
                type=string_too_long[0:50],
            ),
        )

        assert (
            len(_env.csms_mock.on_security_event_notification.mock_calls) == 3
        )  # we expect 3 because of the StartupOfTheDevice, SecurityLogWasCleared, StringTooLong

    @pytest.mark.ocpp_legacy_only
    @pytest.mark.ocpp_config_adaptions(
        GenericOCPP16ConfigAdjustment(
            [("Custom", "ExampleConfigurationKey", "test_value")]
        )
    )
    async def test_command_get_variables_legacy(self, _env):
        """The legacy OCPP module ignores the component name and variable
        instance; variable.name is always treated as a configuration key."""
        res = await _env.probe_module.call_command(
            "ocpp",
            "get_variables",
            {
                "requests": [
                    {
                        "component_variable": {
                            "component": {"name": "IGNORED"},
                            "variable": {"name": "ChargePointId"},
                        }
                    },
                    {
                        "component_variable": {
                            "component": {"name": ""},
                            "variable": {"name": "UNKNOWN"},
                        },
                        "attribute_type": "Target",
                    },
                    {
                        "component_variable": {
                            "component": {"name": ""},
                            "variable": {
                                "name": "ExampleConfigurationKey",
                                "instance": "TO_BE_IGNORED",
                            },
                        },
                        "attribute_type": "Target",  # ignored
                    },
                ]
            },
        )

        assert res == [
            {
                "attribute_type": "Actual",
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {"name": "ChargePointId"},
                },
                "status": "Accepted",
                "value": "cp001",
            },
            {
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {"name": "UNKNOWN"},
                },
                "status": "UnknownVariable",
            },
            {
                "attribute_type": "Actual",
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {"name": "ExampleConfigurationKey"},
                },
                "status": "Accepted",
                "value": "test_value",
            },
        ]

    @pytest.mark.ocpp_multi_only
    @pytest.mark.ocpp_config_adaptions(
        GenericOCPP16ConfigAdjustment(
            [("Custom", "ExampleConfigurationKey", "test_value")]
        )
    )
    async def test_command_get_variables_multi(self, _env):
        """OCPPmulti never reinterprets a non-empty component name as a
        configuration key (UnknownComponent); the deprecated empty-component
        form still routes to keys, ignoring the variable instance."""
        res = await _env.probe_module.call_command(
            "ocpp",
            "get_variables",
            {
                "requests": [
                    {
                        "component_variable": {
                            "component": {"name": "NOT_A_COMPONENT"},
                            "variable": {"name": "ChargePointId"},
                        }
                    },
                    {
                        "component_variable": {
                            "component": {"name": ""},
                            "variable": {"name": "UNKNOWN"},
                        },
                        "attribute_type": "Target",
                    },
                    {
                        "component_variable": {
                            "component": {"name": ""},
                            "variable": {
                                "name": "ExampleConfigurationKey",
                                "instance": "TO_BE_IGNORED",
                            },
                        },
                        "attribute_type": "Target",  # ignored on the key path
                    },
                ]
            },
        )

        assert res == [
            {
                "component_variable": {
                    "component": {"name": "NOT_A_COMPONENT"},
                    "variable": {"name": "ChargePointId"},
                },
                "status": "UnknownComponent",
            },
            {
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {"name": "UNKNOWN"},
                },
                "status": "UnknownVariable",
            },
            {
                "attribute_type": "Actual",
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {"name": "ExampleConfigurationKey"},
                },
                "status": "Accepted",
                "value": "test_value",
            },
        ]

    @pytest.mark.ocpp_legacy_only
    @pytest.mark.ocpp_config_adaptions(
        GenericOCPP16ConfigAdjustment(
            [("Custom", "ExampleConfigurationKey", "test_value")]
        )
    )
    async def test_command_set_variables_legacy(self, _env):
        """The legacy OCPP module ignores the component name and variable
        instance; variable.name is always treated as a configuration key."""
        res = await _env.probe_module.call_command(
            "ocpp",
            "set_variables",
            {
                "requests": [
                    {
                        "component_variable": {
                            "component": {"name": "IGNORED"},
                            "variable": {"name": "RetryBackoffRandomRange"},
                        },
                        # standard (non-custom), writable key; will be Accepted.
                        "value": "99",
                    },
                    {
                        "component_variable": {
                            "component": {"name": ""},
                            "variable": {"name": "UNKNOWN"},
                        },
                        # does not exist - will be UnknownVariable
                        "attribute_type": "Target",
                        "value": "test_value",
                    },
                    {
                        "component_variable": {
                            "component": {"name": ""},
                            "variable": {
                                "name": "ExampleConfigurationKey",
                                "instance": "TO_BE_IGNORED",
                            },
                        },
                        "attribute_type": "Target",
                        "value": "unittest changed value",
                    },
                ],
                "source": "testcase",
            },
        )

        assert res
        assert isinstance(res, list) and len(res) == 3
        assert res == [
            {
                "component_variable": {
                    "component": {"name": "IGNORED"},
                    "variable": {"name": "RetryBackoffRandomRange"},
                },
                "status": "Accepted",
            },
            {
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {"name": "UNKNOWN"},
                },
                "status": "UnknownVariable",
            },
            {
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {
                        "instance": "TO_BE_IGNORED",
                        "name": "ExampleConfigurationKey",
                    },
                },
                "status": "Accepted",
            },
        ]

        # Verify value changed
        check = await _env.probe_module.call_command(
            "ocpp",
            "get_variables",
            {
                "requests": [
                    {
                        "component_variable": {
                            "component": {"name": ""},
                            "variable": {"name": "ExampleConfigurationKey"},
                        }
                    }
                ]
            },
        )
        assert check == [
            {
                "attribute_type": "Actual",
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {"name": "ExampleConfigurationKey"},
                },
                "status": "Accepted",
                "value": "unittest changed value",
            }
        ]

    @pytest.mark.ocpp_multi_only
    @pytest.mark.ocpp_config_adaptions(
        GenericOCPP16ConfigAdjustment(
            [("Custom", "ExampleConfigurationKey", "test_value")]
        )
    )
    async def test_command_set_variables_multi(self, _env):
        """OCPPmulti never reinterprets a non-empty component name as a
        configuration key (UnknownComponent, nothing written); the deprecated
        empty-component form still routes to keys, ignoring the variable
        instance."""
        res = await _env.probe_module.call_command(
            "ocpp",
            "set_variables",
            {
                "requests": [
                    {
                        "component_variable": {
                            "component": {"name": "NOT_A_COMPONENT"},
                            "variable": {"name": "RetryBackoffRandomRange"},
                        },
                        # writable key, but the component does not resolve -
                        # will be UnknownComponent and not written
                        "value": "99",
                    },
                    {
                        "component_variable": {
                            "component": {"name": ""},
                            "variable": {"name": "UNKNOWN"},
                        },
                        # does not exist - will be UnknownVariable
                        "attribute_type": "Target",
                        "value": "test_value",
                    },
                    {
                        "component_variable": {
                            "component": {"name": ""},
                            "variable": {
                                "name": "ExampleConfigurationKey",
                                "instance": "TO_BE_IGNORED",
                            },
                        },
                        "attribute_type": "Target",
                        "value": "unittest changed value",
                    },
                ],
                "source": "testcase",
            },
        )

        assert res
        assert isinstance(res, list) and len(res) == 3
        assert res == [
            {
                "component_variable": {
                    "component": {"name": "NOT_A_COMPONENT"},
                    "variable": {"name": "RetryBackoffRandomRange"},
                },
                "status": "UnknownComponent",
            },
            {
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {"name": "UNKNOWN"},
                },
                "status": "UnknownVariable",
            },
            {
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {
                        "instance": "TO_BE_IGNORED",
                        "name": "ExampleConfigurationKey",
                    },
                },
                "status": "Accepted",
            },
        ]

        # Verify value changed
        check = await _env.probe_module.call_command(
            "ocpp",
            "get_variables",
            {
                "requests": [
                    {
                        "component_variable": {
                            "component": {"name": ""},
                            "variable": {"name": "ExampleConfigurationKey"},
                        }
                    }
                ]
            },
        )
        assert check == [
            {
                "attribute_type": "Actual",
                "component_variable": {
                    "component": {"name": ""},
                    "variable": {"name": "ExampleConfigurationKey"},
                },
                "status": "Accepted",
                "value": "unittest changed value",
            }
        ]

    @pytest.mark.ocpp_legacy_only
    async def test_command_monitor_variables_legacy(self, _env):
        """Test monitoring a configuration variable as well as an event_data
        subscription. The legacy OCPP module ignores the component name and
        registers the variable name as a configuration key."""

        async def change_var(key: str, value: str):
            res = await _env.charge_point.change_configuration_req(key=key, value=value)
            assert res.status == "Accepted"

        event_data_subscription_mock = Mock()
        _env.probe_module.subscribe_variable(
            "ocpp", "event_data", event_data_subscription_mock
        )

        await change_var("HeartbeatInterval", "1")

        # assert no event before monitoring is enabled
        await asyncio.sleep(0.1)
        event_data_subscription_mock.assert_not_called()

        # enable monitoring
        res = await _env.probe_module.call_command(
            "ocpp",
            "monitor_variables",
            {
                "component_variables": [
                    {
                        "component": {"name": "IGNORED"},
                        "variable": {"name": "HeartbeatInterval"},
                    },
                    {
                        "component": {"name": ""},
                        "variable": {"name": "MeterValuesAlignedData"},
                    },
                    {
                        "component": {"name": ""},
                        "variable": {"name": "UNKNOWN"},
                    },
                ]
            },
        )
        assert res is None

        # verify event is triggered
        await change_var("HeartbeatInterval", "42")
        await wait_for_mock_called(
            event_data_subscription_mock,
            mock_call(
                {
                    "actual_value": "42",
                    "component_variable": {
                        "component": {"name": "IGNORED"},
                        "variable": {"name": "HeartbeatInterval"},
                    },
                    "event_id": ANY,
                    "event_notification_type": "CustomMonitor",
                    "timestamp": ANY,
                    "trigger": "Alerting",
                }
            ),
        )

    @pytest.mark.ocpp_multi_only
    async def test_command_monitor_variables_multi(self, _env):
        """Test monitoring a configuration variable as well as an event_data
        subscription. OCPPmulti resolves the component, so the canonical
        address must be used; events echo the registered form."""

        async def change_var(key: str, value: str):
            res = await _env.charge_point.change_configuration_req(key=key, value=value)
            assert res.status == "Accepted"

        event_data_subscription_mock = Mock()
        _env.probe_module.subscribe_variable(
            "ocpp", "event_data", event_data_subscription_mock
        )

        await change_var("HeartbeatInterval", "1")

        # assert no event before monitoring is enabled
        await asyncio.sleep(0.1)
        event_data_subscription_mock.assert_not_called()

        # enable monitoring; the deprecated empty-component key form is still
        # accepted, unknown keys are skipped
        res = await _env.probe_module.call_command(
            "ocpp",
            "monitor_variables",
            {
                "component_variables": [
                    {
                        "component": {"name": "OCPPCommCtrlr"},
                        "variable": {"name": "HeartbeatInterval"},
                    },
                    {
                        "component": {"name": ""},
                        "variable": {"name": "MeterValuesAlignedData"},
                    },
                    {
                        "component": {"name": ""},
                        "variable": {"name": "UNKNOWN"},
                    },
                ]
            },
        )
        assert res is None

        # verify event is triggered
        await change_var("HeartbeatInterval", "42")
        await wait_for_mock_called(
            event_data_subscription_mock,
            mock_call(
                {
                    "actual_value": "42",
                    "component_variable": {
                        "component": {"name": "OCPPCommCtrlr"},
                        "variable": {"name": "HeartbeatInterval"},
                    },
                    "event_id": ANY,
                    "event_notification_type": "CustomMonitor",
                    "timestamp": ANY,
                    "trigger": "Alerting",
                }
            ),
        )

    async def test_subscribe_charging_schedules(self, _env):
        subscription_mock = Mock()
        _env.probe_module.subscribe_variable(
            "ocpp", "charging_schedules", subscription_mock
        )

        await _env.charge_point.set_charging_profile_req(
            SetChargingProfile(
                connector_id=0,
                cs_charging_profiles={
                    "chargingProfileId": 0,
                    "stackLevel": 1,
                    "chargingProfilePurpose": "TxDefaultProfile",
                    "chargingProfileKind": "Relative",
                    "chargingSchedule": {
                        "chargingRateUnit": "A",
                        "chargingSchedulePeriod": [{"limit": 32.0, "startPeriod": 0}],
                    },
                },
            )
        )
        await wait_for_mock_called(
            subscription_mock,
            mock_call(
                {
                    "schedules": [
                        {
                            "charging_rate_unit": "A",
                            "charging_schedule_period": [
                                {
                                    "limit": 64,
                                    "stack_level": 0,
                                    "start_period": 0,
                                }
                            ],
                            "evse": 0,
                            "duration": ANY,
                            "start_schedule": ANY,
                        },
                        {
                            "charging_rate_unit": "A",
                            "charging_schedule_period": [
                                {
                                    "limit": 32,
                                    "stack_level": 1,
                                    "start_period": 0,
                                }
                            ],
                            "evse": 1,
                            "duration": ANY,
                            "start_schedule": ANY,
                        },
                        {
                            "charging_rate_unit": "A",
                            "charging_schedule_period": [
                                {
                                    "limit": 32,
                                    "stack_level": 1,
                                    "start_period": 0,
                                }
                            ],
                            "evse": 2,
                            "duration": ANY,
                            "start_schedule": ANY,
                        },
                    ]
                }
            ),
        )

    async def test_subscribe_is_connected(self, _env):
        subscription_mock = Mock()
        _env.probe_module.subscribe_variable(
            "ocpp", "is_connected", subscription_mock)

        assert await _env.probe_module.call_command("ocpp", "stop", None)
        assert await _env.probe_module.call_command("ocpp", "restart", None)

        await wait_for_mock_called(subscription_mock, mock_call(False))
        await wait_for_mock_called(subscription_mock, mock_call(True))

    @pytest.mark.parametrize(
        "overwrite_implementation",
        [{"security": {"update_leaf_certificate": "InvalidSignature"}}],
    )
    async def test_subscribe_security_event(self, _env):
        subscription_mock = Mock()
        _env.probe_module.subscribe_variable(
            "ocpp", "security_event", subscription_mock
        )
        # trigger security event by invalid certificate signed request
        await _env.charge_point.certificate_signed_req(certificate_chain="somechain")

        await wait_for_mock_called(
            subscription_mock,
            mock_call(
                {"info": "InvalidSignature", "type": "InvalidChargePointCertificate"}
            ),
        )

    async def test_change_availability_request_connector(self, _env):
        _env.probe_module_command_mocks["evse_manager"]["enable_disable"].reset_mock(
        )

        res = await _env.probe_module.call_command(
            "ocpp",
            "change_availability",
            {
                "request": {
                    "operational_status": "Inoperative",
                    "evse": {
                        "id": 1,
                        "connector_id": 1,
                    },
                }
            },
        )
        assert res == {"status": "Accepted"}

        await wait_for_mock_called(
            _env.probe_module_command_mocks["evse_manager"]["enable_disable"],
            call=mock_call(
                {
                    "cmd_source": {
                        "enable_priority": 5000,
                        "enable_source": "CSMS",
                        "enable_state": "Disable",
                    },
                    "connector_id": 0,
                }
            ),
        )  # as currently implemented in disable_evse callback in OCPP module

        _env.probe_module_command_mocks["evse_manager"]["enable_disable"].reset_mock(
        )
        _env.probe_module_command_mocks["evse_manager_b"]["enable_disable"].reset_mock(
        )

        res = await _env.probe_module.call_command(
            "ocpp",
            "change_availability",
            {
                "request": {
                    "operational_status": "Inoperative",
                    "evse": {
                        "id": 2,
                        "connector_id": 1,
                    },
                }
            },
        )
        assert res == {"status": "Accepted"}

        await wait_for_mock_called(
            _env.probe_module_command_mocks["evse_manager_b"]["enable_disable"],
            call=mock_call(
                {
                    "cmd_source": {
                        "enable_priority": 5000,
                        "enable_source": "CSMS",
                        "enable_state": "Disable",
                    },
                    "connector_id": 0,
                }
            ),
        )  # as currently implemented in disable_evse callback in OCPP module

    async def test_change_availability_request_evse(self, _env):
        _env.probe_module_command_mocks["evse_manager"]["enable_disable"].reset_mock(
        )

        res = await _env.probe_module.call_command(
            "ocpp",
            "change_availability",
            {"request": {"operational_status": "Inoperative"}},
        )
        assert res == {"status": "Accepted"}
        await wait_for_mock_called(
            _env.probe_module_command_mocks["evse_manager"]["enable_disable"],
            call=mock_call(
                {
                    "cmd_source": {
                        "enable_priority": 5000,
                        "enable_source": "CSMS",
                        "enable_state": "Disable",
                    },
                    "connector_id": 0,
                }
            ),
        )
        assert (
            len(
                _env.probe_module_command_mocks["evse_manager"][
                    "enable_disable"
                ].mock_calls
            )
            == 1
        )

    async def test_change_availability_request_failed(self, _env):
        # Failed request: no connector id
        res = await _env.probe_module.call_command(
            "ocpp",
            "change_availability",
            {
                "request": {
                    "operational_status": "Inoperative",
                    "evse": {
                        "id": 1,
                    },
                }
            },
        )
        assert res == {
            "status": "Rejected",
            "status_info": {
                "additional_info": ANY,  # No connector id specified;
                "reason_code": "InvalidInput",
            },
        }

        res = await _env.probe_module.call_command(
            "ocpp",
            "change_availability",
            {
                "request": {
                    "operational_status": "Inoperative",
                    "evse": {"id": 2, "connector_id": 2},
                }
            },
        )
        assert res == {
            "status": "Rejected",
            "status_info": {
                "additional_info": ANY,  # Invalid connector id specified
                "reason_code": "InvalidInput",
            },
        }
