// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "external_energy_node_client_API.hpp"

#include <utils/date.hpp>

namespace module {

void external_energy_node_client_API::init() {
    invoke_init(*p_main);
    invoke_init(*p_energy_grid);

    // Initialise ApiHelper — registers heartbeat and communication-check parameters
    // for this client's own API identity (external_energy_node_client).
    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void external_energy_node_client_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_energy_grid);

    helper.generate_api_var_communication_check(&comm_check);

    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);

    helper.publish_ready_beacon();
}

void external_energy_node_client_API::note_remote_request(const std::string& uuid) {
    if (config.stale_timeout_s <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(stale_mutex);
    stale_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(config.stale_timeout_s);
    last_remote_uuid = uuid;
    if (remote_stale_reported) {
        remote_stale_reported = false;
        EVLOG_info << "external_energy_node_client_API [" << config.server_id << "]: remote aggregate is live again";
    }
    stale_timer.timeout([this]() { on_remote_stale(); }, std::chrono::seconds(config.stale_timeout_s));
}

void external_energy_node_client_API::on_remote_stale() {
    std::lock_guard<std::mutex> lock(stale_mutex);

    const auto now = std::chrono::steady_clock::now();
    if (now < stale_deadline) {
        // Stale or premature fire: either a fresh energy_flow_request moved the
        // deadline after this handler was already queued (Everest::Timer's
        // cancel() cannot recall it), or the timer's wall clock stepped forward.
        // The monotonic deadline is authoritative — ignore and re-arm for the
        // remainder.
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(stale_deadline - now);
        stale_timer.timeout([this]() { on_remote_stale(); }, remaining);
        return;
    }

    if (remote_stale_reported) {
        return;
    }
    remote_stale_reported = true;
    EVLOG_warning << "external_energy_node_client_API [" << config.server_id
                  << "]: no energy_flow_request from the remote server for more than " << config.stale_timeout_s
                  << " s — withdrawing the remote subtree (zero-limit aggregate)";

    // Publish a childless zero-limit aggregate under the same (namespaced) root
    // uuid. The site-level EnergyManager then allocates 0 A to this subtree and
    // frees the budget it was reserving for EVSEs that are no longer reachable
    // (they are governed by the remote process's local fallback in the meantime).
    types::energy::EnergyFlowRequest withdrawn;
    withdrawn.uuid = last_remote_uuid;
    withdrawn.node_type = types::energy::NodeType::Generic;

    const std::string source = "external_energy_node_client_API/" + config.server_id + "/stale";
    types::energy::ScheduleReqEntry zero;
    zero.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
    zero.limits_to_root.ac_max_current_A = {0., source};
    zero.limits_to_root.total_power_W = {0., source};
    withdrawn.schedule_import = {zero};
    withdrawn.schedule_export = {zero};

    p_energy_grid->publish_energy_flow_request(withdrawn);
}

} // namespace module
