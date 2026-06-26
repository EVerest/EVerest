# Currently failing integration tests

## OCPP 1.6
### booting.py
#### test_initiate_message_in_pending

This test uses security profile 2 and OCPPmulti is not successfully connecting
to the CSMS.

### failing tests

ERROR ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_chargepoint_update_certificate - TimeoutError: Timeout while waiting for the chargepoint to connect.
ERROR ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_chargepoint_install_certificate - TimeoutError: Timeout while waiting for the chargepoint to connect.
FAILED ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_reservation_local_start_tx - assert False
FAILED ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_reservation_remote_start_tx - assert False
FAILED ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_reservation_connector_expire - assert False
FAILED ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_reservation_connector_faulted - assert False
FAILED ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_reservation_connector_zero_supported - assert False
FAILED ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_reservation_faulted_state - assert False
FAILED ocpp_tests/test_sets/ocpp16/ocpp_compliance_tests.py::test_data_transfer_to_chargepoint - assert False
================================================================================
 9 failed, 189 passed, 9 skipped, 4 warnings, 2 errors in 2155.72s (0:35:55)
================================================================================


# TODO

## integrate changes from:

- 4ad65bef4cef3e0d41a6373cca910dcf2214fc29
- 5f1d5e06ff0856c6bcf2be367f1640f33b5b3ef0
