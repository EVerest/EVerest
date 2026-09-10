// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Custom GoogleTest main for everest_io_tls_test.
//
// The TLS tests reuse the libtls test PKI fixtures, produced by ./pki.sh in the libtls test build
// directory. libtls's own tls_test runs ./pki.sh from its main(); this binary must not rely on that
// having happened first under ctest, so it regenerates the fixtures itself.

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <linux/limits.h>
#include <unistd.h>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    // OpenSSL drives its socket BIO through write() without MSG_NOSIGNAL, so a write to a
    // peer-reset connection raises SIGPIPE and kills the process. Without this the RST-teardown
    // tests would abort.
    std::signal(SIGPIPE, SIG_IGN);
    if (std::system("./pki.sh") != 0) {
        std::cerr << "Problem creating test certificates and keys" << std::endl;
        char buf[PATH_MAX];
        if (::getcwd(&buf[0], sizeof(buf)) != nullptr) {
            std::cerr << "./pki.sh not found in " << buf << std::endl;
        }
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
