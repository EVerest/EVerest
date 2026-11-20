Implemented #2496’s mapping layer using the ConfigService functionality from #2412 and the current runtime registry architecture.

### Changes
- Added explicit YAML configuration-to-device-model mappings for OCPP 1.6 and 2.x.
- Kept ConfigService as the sole persistent value owner, with cached reads and loop-free updates.
- Added configurable exposure of unmapped parameters; mapped parameters have no duplicate writable view.
- Preserved `RebootRequired` behavior and the shared monitoring path required by #2639.
- Added validation, verbose startup diagnostics, tests, and documentation.
- Left Auth/EVSE Manager command removal for the separate follow-up requested in the issue discussion.

### Validation
- Built `OCPPmulti` and both test targets.
- Both targeted CTest suites passed together with `-j 2`.
- All **235 common-library tests** passed.
- OCPP 1.6 legacy-key mapping regression passed.
- Formatting, manifest schema, and whitespace checks passed for this change.

Bazel definitions were updated but not executed. No commits were created.

An unrelated whitespace-only change in `/home/nosm/development/upstream_everest/EVerest/lib/everest/ocpp_module_common/src/device_model/everest_device_model.cpp` was left untouched.