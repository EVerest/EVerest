// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <charge_bridge/utilities/filesystem.hpp>
#include <charge_bridge/utilities/sync_udp_client.hpp>

#include <fstream>
#include <functional>

namespace charge_bridge::firmware_update {

/// Request/reply budget every request of the firmware management socket uses unless it passes its
/// own: the reply is awaited for default_udp_timeout_ms and the datagram is retransmitted
/// default_udp_retries times, so a request to an unreachable device takes retries * timeout to fail.
/// Sized for the slow, one-shot firmware operations (a device in bootloader mode is answering while
/// it erases flash); periodic callers pass a short budget of their own instead.
constexpr std::uint16_t default_udp_retries = 3;
constexpr std::uint16_t default_udp_timeout_ms = 3000;

struct fw_update_config {
    std::string cb;
    std::uint16_t cb_port;
    std::string cb_remote;
    std::string fw_path;
    bool fw_update_on_start;
};

class sync_fw_updater {
public:
    /// @param abort_requested Optional cancellation check, polled before every firmware chunk is
    /// sent and while any request waits for its reply (version probe, upload start, chunk, connection
    /// check). Returning true aborts the (potentially multi-minute) upload and bounds every probe to
    /// a fraction of a second, so a shutdown does not have to wait for the flash or for a full retry
    /// budget to expire. The abort is reported like any other failure; it happens before the finish
    /// packet, so the device keeps running its previous firmware. The finish handshake itself is
    /// intentionally not cancellable. Defaults to an empty check, i.e. no cancellation at all.
    sync_fw_updater(fw_update_config const& config, std::function<bool()> abort_requested = {});
    ~sync_fw_updater() = default;

    std::optional<std::string> get_fw_version();
    bool switch_bank();
    /// Silent management-port ping: unlike quick_check_connection()/check_connection() it reports
    /// nothing, which makes it usable as a periodic liveness probe.
    /// @param timeout_ms How long a single attempt waits for the reply.
    /// @param retries How often the ping is retransmitted after an unanswered attempt, so an
    /// unanswered ping costs retries * timeout_ms (the abort check, if armed, cuts that short). A
    /// reachable device answers the first attempt, so its cost is one round trip regardless.
    /// The defaults are the slow firmware budget (3 x 3000 ms = 9 s); a caller on a fixed cadence
    /// must pass a budget that fits its cycle.
    bool ping(std::uint16_t timeout_ms = default_udp_timeout_ms, std::uint16_t retries = default_udp_retries);
    bool upload_fw();

    void print_fw_version();
    bool print_switch_bank();
    bool quick_check_connection();
    bool check_connection();
    bool check_if_correct_fw_installed();

private:
    bool check_reply(utilities::sync_udp_client::reply const& val);
    bool is_abort_requested() const;
    std::string connection_result_message(bool connected) const;

    bool upload_firmware(bool& aborted);

    bool upload_init(const fs::path& file_path, std::uint32_t& offset,
                     charge_bridge::filesystem_utils::CryptSignedHeader& hdr);
    bool upload_transfer(const fs::path& file_path, std::uint16_t& sector, std::uint32_t offset,
                         std::uint32_t& total_bytes, bool& aborted);
    bool upload_finish(const fs::path& file_path, std::uint32_t total_bytes,
                       const charge_bridge::filesystem_utils::CryptSignedHeader& hdr);

    everest::lib::io::udp::udp_payload make_fw_chunk(std::uint16_t sector, std::uint8_t last_chunk,
                                                     std::vector<std::uint8_t> const& data);

    utilities::sync_udp_client m_udp;
    fw_update_config m_config;
    std::function<bool()> m_abort_requested;
    static const std::uint32_t app_udp_sector_size;
    static const std::uint16_t sub_chunk_size;
};
} // namespace charge_bridge::firmware_update
