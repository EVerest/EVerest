// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <csignal>
#include <signal.h>

#include <connection.hpp>
#include <v2g_ctx.hpp>

#include "ISO15118_chargerImplStub.hpp"
#include "ModuleAdapterStub.hpp"
#include "evse_securityIntfStub.hpp"
#include "iso15118_extensionsImplStub.hpp"
#include "iso15118_vasIntfStub.hpp"

#include <gtest/gtest.h>

// connection.cpp hands accepted connections to this; the test never accepts one
int v2g_handle_connection(struct v2g_connection*) {
    return -1;
}

namespace {

TEST(SignalTest, connection_init_ignores_sigpipe) {
    module::stub::QuietModuleAdapterStub adapter;
    module::stub::ISO15118_chargerImplStub charger(adapter);
    module::stub::evse_securityIntfStub security(adapter);
    module::stub::iso15118_extensionsImplStub extensions;
    module::stub::iso15118_vasIntfStub vas_item(adapter);

    v2g_context* ctx = v2g_ctx_create(&charger, &extensions, &security, {&vas_item});
    ASSERT_NE(ctx, nullptr);
    ctx->if_name = "lo";
    ctx->tls_security = TLS_SECURITY_PROHIBIT;
    ctx->tcp_port = 0;

    std::signal(SIGPIPE, SIG_DFL);
    ASSERT_EQ(connection_init(ctx, nullptr), 0);

    struct sigaction disposition = {};
    ASSERT_EQ(sigaction(SIGPIPE, nullptr, &disposition), 0);
    EXPECT_EQ(disposition.sa_handler, SIG_IGN);

    connection_cleanup(ctx);
    v2g_ctx_free(ctx);
}

} // namespace
