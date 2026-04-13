# Use as target_compatible_with for targets that require a Python toolchain,
# which is not available for cross-compilation platforms.
CROSS_PYTHON_INCOMPATIBLE = select({
    "@platforms//cpu:armv7": ["@platforms//:incompatible"],
    "@platforms//cpu:aarch64": ["@platforms//:incompatible"],
    "//conditions:default": [],
})

# Use as target_compatible_with for test targets that cannot be
# cross-compiled (e.g. tests linking host-only libraries like Catch2/GTest).
CROSS_TEST_INCOMPATIBLE = select({
    "@platforms//cpu:armv7": ["@platforms//:incompatible"],
    "@platforms//cpu:aarch64": ["@platforms//:incompatible"],
    "//conditions:default": [],
})

