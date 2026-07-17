// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"
#include <iso15118/d2/config.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/limits.hpp>
#include <iso15118/io/poll_manager.hpp>
#include <iso15118/io/sdp_server.hpp>
#include <iso15118/io/time.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/session/config.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/iso.hpp>

namespace iso15118 {

struct TbdConfig {
    config::SSLConfig ssl{config::CertificateBackend::EVEREST_LAYOUT, {}, {}, {}, {}, {}, {}};
    std::string interface_name;
    config::TlsNegotiationStrategy tls_negotiation_strategy{config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER};
    bool enable_sdp_server{true};
};

class TbdController {
public:
    TbdController(TbdConfig, session::feedback::Callbacks, session::EvseSetupConfig);
    ~TbdController();

    void loop();

    void shutdown();

    void send_control_event(const d20::ControlEvent&);

    void update_authorization_services(const std::vector<message_20::datatypes::Authorization>& services,
                                       bool cert_install_service);
    void update_dc_limits(const d20::DcTransferLimits&);
    void update_powersupply_limits(const d20::DcTransferLimits&);
    // ISO 15118-2: request a (signed) MeteringReceipt from the EV (ReceiptRequired in the charge loop).
    void update_receipt_required(bool);
    void update_energy_modes(const std::vector<message_20::datatypes::ServiceCategory>&);
    void update_ac_limits(const d20::AcTransferLimits&);

    void update_supported_vas_services(const std::vector<uint16_t>& vas_services);

    void set_dlink_ready(bool ready);

    void update_supported_der_functions(iec::DERControlName der_control, const iec::DERControlFunction& function);
    void update_unsupported_der_functions(iec::DERControlName der_control);

private:
    io::PollManager poll_manager;
    std::unique_ptr<io::SdpServer> sdp_server;

    std::unique_ptr<Session> session;

    std::atomic_bool shutdown_active{false};
    // Set on set_dlink_ready(false): the data link is gone, so a running
    // session must be terminated (handled on the loop() thread).
    std::atomic_bool kill_session_requested{false};

    // callbacks for sdp server
    void handle_sdp_server_input();

    const TbdConfig config;
    const session::feedback::Callbacks callbacks;

    session::EvseSetupConfig evse_setup;

    std::string interface_name;

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    // ISO 15118-2 pause context, owned here so it outlives the per-session D2SeccEngine (mirrors pause_ctx).
    std::optional<d2::PauseContext> d2_pause_ctx{std::nullopt};

    static constexpr uint32_t V2G_COMMUNICATION_SETUP_TIMEOUT_MS{18000};
    std::optional<Timeout> communication_setup_timeout;
};

} // namespace iso15118
