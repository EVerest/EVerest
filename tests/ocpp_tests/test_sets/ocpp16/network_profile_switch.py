# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Registration after the OCPP 1.6 connection moves to another Central System without a reboot.

As OCPP 2.0.1 Ed4 / 2.1 Ed2 B10.FR.10 require, the charge point re-registers with a BootNotification by default
after it connects through a network configuration slot whose Central System URL differs from the one it is registered
with. OCPP-J 1.6 section 5.4 advises against a BootNotification on reconnect, so another slot with the same URL is a
plain reconnect and BootNotificationAfterNetworkProfileSwitch can turn the behaviour off altogether.

The test framework points every configured NetworkConfiguration_N slot at the central_system fixture, so slot 2 is
the central_system fixture and slot 1, which comes from the migrated Internal.CentralSystemURI, is the second_csms
fixture.
"""

# fmt: off
import asyncio
import logging
from typing import Optional

import pytest

from everest.testing.core_utils.controller.test_controller_interface import TestController
from everest.testing.core_utils._configuration.libocpp_configuration_helper import (
    GenericOCPP2XConfigAdjustment,
    OCPP2XConfigVariableIdentifier,
)
from everest.testing.ocpp_utils.central_system import CentralSystem
from everest.testing.ocpp_utils.charge_point_utils import wait_for_and_validate, TestUtility, OcppTestConfiguration
from everest_test_utils import (
    assert_registered_before,
    received_calls,
    second_csms,
    second_csms_central_system_uri,
    stop_csms,
)
# fmt: on

log = logging.getLogger("networkProfileSwitch16")

SLOT_2_ADAPTIONS = GenericOCPP2XConfigAdjustment(
    [
        # The placeholder OcppCsmsUrl is rewritten to the central_system fixture by the everest-testing defaults.
        (OCPP2XConfigVariableIdentifier("NetworkConfiguration_2", "OcppCsmsUrl"), "ws://replaced.invalid/cp"),
        (OCPP2XConfigVariableIdentifier("NetworkConfiguration_2", "SecurityProfile"), 0),
        (OCPP2XConfigVariableIdentifier("NetworkConfiguration_2", "OcppTransport"), "JSON"),
        (OCPP2XConfigVariableIdentifier("NetworkConfiguration_2", "MessageTimeout"), 30),
        (OCPP2XConfigVariableIdentifier("NetworkConfiguration_2", "OcppInterface"), "Wired0"),
        (OCPP2XConfigVariableIdentifier("OCPPCommCtrlr", "NetworkConfigurationPriority"), "1,2"),
        (OCPP2XConfigVariableIdentifier("OCPPCommCtrlr", "NetworkProfileConnectionAttempts"), 1),
    ]
)


async def _switch_to_slot_2_with_offline_transaction(
    second_csms: CentralSystem,
    central_system: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    test_config: OcppTestConfiguration,
    boot_notification_after_switch: Optional[str],
):
    """Registers on slot 1, loses that CSMS, starts a transaction while offline and returns the charge point
    connected through slot 2 together with whether the StartTransaction.req arrived there."""
    test_controller.start()
    charge_point_slot_1 = await second_csms.wait_for_chargepoint()

    if boot_notification_after_switch is not None:
        await charge_point_slot_1.change_configuration_req(
            key="BootNotificationAfterNetworkProfileSwitch", value=boot_notification_after_switch
        )
    await charge_point_slot_1.change_configuration_req(key="AllowOfflineTxForUnknownId", value="true")
    await charge_point_slot_1.change_configuration_req(key="LocalAuthorizeOffline", value="true")

    await stop_csms(second_csms)
    test_controller.plug_in()
    test_controller.swipe(test_config.authorization_info.valid_id_tag_1)

    charge_point_slot_2 = await central_system.wait_for_chargepoint(timeout=120, wait_for_bootnotification=False)
    transaction_received = await wait_for_and_validate(
        test_utility, charge_point_slot_2, "StartTransaction", {"connectorId": 1}, timeout=60
    )
    return charge_point_slot_2, transaction_received


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp1.6")
@pytest.mark.ocpp_config_adaptions(second_csms_central_system_uri())
@pytest.mark.ocpp16_component_config_adaptions(SLOT_2_ADAPTIONS)
async def test_boot_notification_after_profile_switch_by_default(
    second_csms: CentralSystem,
    central_system: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    test_config: OcppTestConfiguration,
):
    """By default the charge point re-registers with a BootNotification before anything else after it connects through
    a slot with another Central System URL, and the StartTransaction.req queued while offline follows only once that
    BootNotification was answered."""
    charge_point_slot_2, transaction_received = await _switch_to_slot_2_with_offline_transaction(
        second_csms, central_system, test_controller, test_utility, test_config, boot_notification_after_switch=None
    )

    assert_registered_before(charge_point_slot_2, ["StartTransaction"])
    assert transaction_received, "the transaction started while offline was not sent after the switch"


@pytest.mark.asyncio
@pytest.mark.ocpp_version("ocpp1.6")
@pytest.mark.ocpp_config_adaptions(second_csms_central_system_uri())
@pytest.mark.ocpp16_component_config_adaptions(SLOT_2_ADAPTIONS)
async def test_no_boot_notification_after_profile_switch_when_disabled(
    second_csms: CentralSystem,
    central_system: CentralSystem,
    test_controller: TestController,
    test_utility: TestUtility,
    test_config: OcppTestConfiguration,
):
    """With BootNotificationAfterNetworkProfileSwitch disabled, a switch to a slot with another Central System URL is
    a plain reconnect, as OCPP-J 1.6 section 5.4 advises."""
    charge_point_slot_2, transaction_received = await _switch_to_slot_2_with_offline_transaction(
        second_csms, central_system, test_controller, test_utility, test_config, boot_notification_after_switch="false"
    )

    assert transaction_received, "the transaction started while offline was not sent after the switch"
    assert received_calls(charge_point_slot_2, "BootNotification") == []
