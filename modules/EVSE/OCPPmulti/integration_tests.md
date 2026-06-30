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
================================================================================
 1 failed, 202 passed, 9 skipped, 4 warnings, 2 errors in 1945.98s (0:32:25)
================================================================================

### OCPP 2.0.1 failing tests

==========================================================================================
 211 passed, 16 skipped, 1 warning in 1643.68s (0:27:23)
==========================================================================================

# TODO

## integrate changes from:

- 4ad65bef4cef3e0d41a6373cca910dcf2214fc29
- 5f1d5e06ff0856c6bcf2be367f1640f33b5b3ef0
