# syntax=docker/dockerfile:1
ARG BASE_IMAGE_TAG=latest
FROM ghcr.io/everest/everest-ci/build-kit-base:${BASE_IMAGE_TAG}

# Can be used to use an other version of everest-cmake
# ENV EVEREST_CMAKE_PATH=/usr/lib/cmake/everest-cmake
# ENV EVEREST_CMAKE_VERSION=<everest-cmake-verion>
# RUN rm -rf ${EVEREST_CMAKE_PATH} \
#     && git clone https://github.com/EVerest/everest-cmake.git ${EVEREST_CMAKE_PATH} \
#     && cd ${EVEREST_CMAKE_PATH} \
#     && git checkout ${EVEREST_CMAKE_VERSION} \
#     && rm -r .git

# FIXME(kai): Install libsystemd-dev as a dependency of sdbus-c++
RUN apt-get update && apt-get -y install libsystemd-dev

# FIXME: move this snippet to everest-ci Dockerfiles
# Make sure we use a recent enough npm
RUN npm install -g corepack@0.33.0 \
    && apt-get -y remove npm \
    && corepack enable npm --install-directory /usr/bin \
    && corepack prepare npm@10.9.8 --activate

# Rust toolchain for EVEREST_ENABLE_RS_SUPPORT. Pinned rather than floating, and
# kept in sync with RUST_VERSION in MODULE.bazel and
# .devcontainer/general-devcontainer/Dockerfile so all three agree;
# Debian's packaged rustc is 1.63 and far too old. rustfmt is not part of the
# minimal profile, and everestrs-build ignores a failing rustfmt, so leaving it
# out ships unformatted generated code without saying so.
ARG RUST_VERSION=1.86.0
# Must be kept in sync with `everestrs/CMakeLists.txt`.
ARG CXXBRIDGE_VERSION=1.0.194
ENV RUSTUP_HOME=/usr/local/rustup \
    CARGO_HOME=/usr/local/cargo \
    PATH=/usr/local/cargo/bin:$PATH
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
      | sh -s -- -y --no-modify-path --profile minimal \
           --component rustfmt --default-toolchain ${RUST_VERSION} \
 && cargo install cxxbridge-cmd --locked --version ${CXXBRIDGE_VERSION} \
 && rm -rf ${CARGO_HOME}/registry ${CARGO_HOME}/git \
 && chmod -R a+w ${CARGO_HOME} ${RUSTUP_HOME}
