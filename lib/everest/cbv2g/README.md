# libcbV2G - The V2GTP EXI codec library

libcbv2g is a library to encode and decode EXI messages and is able to process DIN70121, ISO15118-2 and ISO15118-20 messages. The library is based on the generated code output of the [cbExiGen](https://github.com/Everest/cbexigen) generator.

All documentation and the issue tracking can be found in our main repository here: https://github.com/EVerest/everest

## Getting started

libcbv2g is built from the everest-core root as part of the `iso15118` package:

```
# Configure from the everest-core root, only the packages libcbv2g needs, with its tests
cmake -S . -B build -G Ninja -DEVEREST_PACKAGES="base;iso15118" -DBUILD_TESTING=ON

# Build
ninja -C build

# Running tests
ninja -C build test
```
