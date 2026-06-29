# Currently failing integration tests

## OCPP 1.6
### booting.py
#### test_initiate_message_in_pending

This test uses security profile 2 and OCPPmulti is not successfully connecting
to the CSMS.

### OCPP 1.6 failing tests

ERROR ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_chargepoint_update_certificate[multi] - TimeoutError: Timeout while waiting for the chargepoint to connect.
ERROR ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_chargepoint_install_certificate[multi] - TimeoutError: Timeout while waiting for the chargepoint to connect.
FAILED ocpp_tests/test_sets/ocpp16/booting.py::test_initiate_message_in_pending[multi] - TimeoutError: Timeout while waiting for the chargepoint to connect.
FAILED ocpp_tests/test_sets/ocpp16/california_pricing_ocpp16.py::TestOcpp16CostAndPrice::test_cost_and_price_running_cost_trigger_time[multi] - AssertionError: expected call not found.
FAILED ocpp_tests/test_sets/ocpp16/california_pricing_ocpp16.py::TestOcpp16CostAndPrice::test_cost_and_price_running_cost_trigger_energy[multi] - AssertionError: expected call not found.
FAILED ocpp_tests/test_sets/ocpp16/california_pricing_ocpp16.py::TestOcpp16CostAndPrice::test_cost_and_price_running_cost_trigger_power[multi] - AssertionError: expected call not found.
FAILED ocpp_tests/test_sets/ocpp16/california_pricing_ocpp16.py::TestOcpp16CostAndPrice::test_cost_and_price_running_cost_trigger_cp_status[multi] - AssertionError: expected call not found.
FAILED ocpp_tests/test_sets/ocpp16/california_pricing_ocpp16.py::TestOcpp16CostAndPrice::test_cost_and_price_set_user_price_timeout[multi] - AssertionError: expected call not found.
================================================================================
 6 failed, 197 passed, 9 skipped, 4 warnings, 2 errors in 1908.58s (0:31:48)
================================================================================

### OCPP 2.0.1 failing tests

FAILED ocpp_tests/test_sets/ocpp201/california_pricing.py::TestOcpp201CostAndPrice::test_tariff_fallback_message_multilanguage_on_authorize[multi] - AssertionError: assert 1 == 2
FAILED ocpp_tests/test_sets/ocpp201/data_transfer.py::TestOcpp201DataTransferIntegration::test_p1_no_callback[multi] - AssertionError: assert DataTransfer(...tom_data=None) == DataTransfer(...tom_data=None)
=====================================================================================
 2 failed, 209 passed, 16 skipped, 1 warning in 1646.93s (0:27:26)
=====================================================================================


# TODO

## integrate changes from:

- 4ad65bef4cef3e0d41a6373cca910dcf2214fc29
- 5f1d5e06ff0856c6bcf2be367f1640f33b5b3ef0
