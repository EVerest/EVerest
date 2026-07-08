// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EEBUS_HPP
#define EEBUS_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for required interface implementations
#include <generated/interfaces/external_energy_limits/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <EebusCallbacks.hpp>
#include <EebusConnectionHandler.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    bool manage_eebus_grpc_api_binary;
    int eebus_service_port;
    int grpc_port;
    std::string eebus_ems_ski_allowlist;
    bool accept_unknown_ems;
    std::string certificate_path;
    std::string private_key_path;
    std::string eebus_grpc_api_binary_path;
    std::string vendor_code;
    std::string device_brand;
    std::string device_model;
    std::string serial_number;
    int failsafe_control_limit_W;
    int max_nominal_power_W;
    int restart_delay_s;
    int reconnect_delay_s;
};

class EEBUS : public Everest::ModuleBase {
public:
    EEBUS() = delete;
    EEBUS(const ModuleInfo& info, std::unique_ptr<external_energy_limitsIntf> r_eebus_energy_sink, Conf& config) :
        ModuleBase(info), r_eebus_energy_sink(std::move(r_eebus_energy_sink)), config(config){};

    const std::unique_ptr<external_energy_limitsIntf> r_eebus_energy_sink;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    ~EEBUS() override;
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    friend class LdEverest;
    void init();
    void ready();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    /// \brief run and supervise the eebus_grpc_api sidecar, restarting it on exit
    void start_eebus_grpc_api(const std::filesystem::path& binary_path, int port,
                              const std::filesystem::path& cert_file, const std::filesystem::path& key_file,
                              int restart_delay_s);
    std::thread m_eebus_grpc_api_thread;
    std::atomic<bool> m_eebus_grpc_api_thread_active{false};
    std::shared_ptr<std::atomic_bool> m_eebus_grpc_api_stop_requested{std::make_shared<std::atomic_bool>(false)};

    std::unique_ptr<EebusConnectionHandler> m_connection_handler;

    EebusCallbacks m_callbacks{};
    everest::lib::io::event::fd_event_handler m_event_handler;
    std::thread m_event_handler_thread;
    std::atomic_bool m_running_flag{true};
    std::mutex m_shutdown_mutex;
    std::condition_variable m_shutdown_cv;
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // EEBUS_HPP
