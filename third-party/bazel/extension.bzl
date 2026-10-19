load("@bazel_features//:features.bzl", "bazel_features")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("@bazel_tools//tools/build_defs/repo:local.bzl", "new_local_repository")

def _deps_impl(module_ctx):
    maybe(
        http_archive,
        name = "sigslot",
        url = "https://github.com/palacaze/sigslot/archive/b588b791b9cf7eb17ff0a74d8aebd4a61166c2e1.tar.gz",
        sha256 = "140f0a2a731ed7d9ebff1bad4cb506ea2aa28fd57f42b3ec79031b1505dc6680",
        strip_prefix = "sigslot-b588b791b9cf7eb17ff0a74d8aebd4a61166c2e1",
        build_file = "@everest-core//third-party/bazel:BUILD.sigslot.bazel",
    )

    # the bazel-central-registry has no catch2 3.16.1 yet, upstream ships its own BUILD.bazel
    maybe(
        http_archive,
        name = "catch2",
        url = "https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz",
        sha256 = "d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d",
        strip_prefix = "Catch2-3.16.1",
    )

    maybe(
        http_archive,
        name = "com_github_HowardHinnant_date",
        url = "https://github.com/HowardHinnant/date/archive/f94b8f36c6180be0021876c4a397a054fe50c6f2.tar.gz",
        sha256 = "8be4c3a52d99b22a4478ce3e2a23fa4b38587ea3d3bc3d1a4d68de22c2e65fb2",
        strip_prefix = "date-f94b8f36c6180be0021876c4a397a054fe50c6f2",
        build_file = "@everest-core//third-party/bazel:BUILD.date.bazel",
    )

    maybe(
        http_archive,
        name = "com_github_pboettch_json-schema-validator",
        url = "https://github.com/pboettch/json-schema-validator/archive/c780404a84dd9ba978ba26bc58d17cb43fa7bc80.tar.gz",
        sha256 = "5b6ef2fd33c7fbc38fefc851f07281699fc45add5a558c2ac3f24be3e36eb0b6",
        strip_prefix = "json-schema-validator-c780404a84dd9ba978ba26bc58d17cb43fa7bc80",
        build_file = "@everest-core//third-party/bazel:BUILD.json-schema-validator.bazel",
    )

    maybe(
        http_archive,
        name = "com_github_warmcatt_libwebsockets",
        url = "https://github.com/warmcat/libwebsockets/archive/refs/tags/v5.0.0.tar.gz",
        sha256 = "f853c6582101cfcee3a5a9e28ae92ab19d9735c5f31f0bb2e9794b5106123962",
        strip_prefix = "libwebsockets-5.0.0",
        build_file = "@everest-core//third-party/bazel:BUILD.libwebsockets.bazel",
    )

    # mosquitto 2.0.x is needed at the moment because there are some unresolved issues our usage of 2.1
    maybe(
        http_archive,
        name = "mosquitto",
        url = "https://mosquitto.org/files/source/mosquitto-2.0.22.tar.gz",
        sha256 = "2f752589ef7db40260b633fbdb536e9a04b446a315138d64a7ff3c14e2de6b68",
        strip_prefix = "mosquitto-2.0.22",
        build_file = "@everest-core//third-party/bazel:BUILD.mosquitto.bazel",
    )

    # the bazel-central-registry has no rapidyaml 0.16 yet
    maybe(
        http_archive,
        name = "rapidyaml",
        url = "https://github.com/biojppm/rapidyaml/archive/refs/tags/v0.16.0.tar.gz",
        sha256 = "ad4337d468c5f5d8624651bdc2900f7fe601c83fc3cff669dc972bf8634af4e9",
        strip_prefix = "rapidyaml-0.16.0",
        build_file = "@everest-core//third-party/bazel:BUILD.rapidyaml.bazel",
    )

    maybe(
        http_archive,
        name = "openssl_source",
        url = "https://github.com/openssl/openssl/releases/download/openssl-3.3.1/openssl-3.3.1.tar.gz",
        sha256 = "777cd596284c883375a2a7a11bf5d2786fc5413255efab20c50d6ffe6d020b7e",
        strip_prefix = "openssl-3.3.1",
        build_file = "@everest-core//third-party/bazel:BUILD.openssl-foreign-cc.bazel",
    )

    version = "0.2.15"
    maybe(
        http_archive,
        name = "pybind11_json",
        strip_prefix = "pybind11_json-%s" % version,
        urls = ["https://github.com/pybind/pybind11_json/archive/refs/tags/%s.tar.gz" % version],
        build_file_content = """
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "pybind11_json",
    hdrs = glob(["include/**/*.hpp"]),
    visibility = [
        "//visibility:public",
    ],
    includes = ["include"]
)
""",
    )

deps = module_extension(
    doc = "Non-module dependencies for everest-core",
    implementation = _deps_impl,
)
