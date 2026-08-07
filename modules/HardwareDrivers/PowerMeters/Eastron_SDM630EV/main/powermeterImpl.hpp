// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_POWERMETER_IMPL_HPP
#define MAIN_POWERMETER_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 4
//

#include <generated/interfaces/powermeter/Implementation.hpp>

#include "../Eastron_SDM630EV.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "transport.hpp"
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace main {

struct Conf {
    int powermeter_device_id;
    int communication_error_pause_delay_s;
    int live_measurement_interval_ms;
    int timezone_offset_minutes;
    std::string ocmf_charge_point_identification_type;
    std::string ocmf_charge_point_identification;
    int ocmf_signature_timeout_ms;
    std::string public_key_format;
    std::string meter_id;
};

class powermeterImpl : public powermeterImplBase {
public:
    powermeterImpl() = delete;
    powermeterImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<Eastron_SDM630EV>& mod, Conf& config) :
        powermeterImplBase(ev, "main"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    ~powermeterImpl() override;

    // Narrow test hooks: inject a fake transport, tweak internal state and
    // invoke the protected handlers without the EVerest runtime.
    struct TestAccess {
        static void set_modbus_transport(powermeterImpl& self,
                                         std::unique_ptr<transport::AbstractModbusTransport> transport) {
            self.p_modbus_transport = std::move(transport);
        }

        static void set_transaction_id(powermeterImpl& self, std::string transaction_id) {
            {
                std::lock_guard<std::mutex> lock(self.m_transaction_mutex);
                self.m_transaction_id = std::move(transaction_id);
            }
            self.m_transaction_active.store(true);
        }

        static void set_pending_closed_transaction(powermeterImpl& self, bool pending) {
            self.m_pending_closed_transaction.store(pending);
        }

        static bool pending_closed_transaction(const powermeterImpl& self) {
            return self.m_pending_closed_transaction.load();
        }

        static void set_public_key_hex(powermeterImpl& self, std::string public_key_hex) {
            self.m_public_key_hex = std::move(public_key_hex);
        }

        static void monitor_charging_status(powermeterImpl& self, std::uint16_t status) {
            self.monitor_charging_status(status);
        }

        static types::powermeter::TransactionStartResponse start_transaction(powermeterImpl& self,
                                                                             types::powermeter::TransactionReq& req) {
            return self.handle_start_transaction(req);
        }

        static types::powermeter::TransactionStopResponse stop_transaction(powermeterImpl& self,
                                                                           std::string& transaction_id) {
            return self.handle_stop_transaction(transaction_id);
        }
    };
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual types::powermeter::TransactionStartResponse
    handle_start_transaction(types::powermeter::TransactionReq& value) override;
    virtual types::powermeter::TransactionStopResponse handle_stop_transaction(std::string& transaction_id) override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<Eastron_SDM630EV>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;
    void shutdown() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    std::unique_ptr<transport::AbstractModbusTransport> p_modbus_transport;

    std::string m_public_key_hex;
    std::string m_transaction_id;
    std::mutex m_transaction_mutex;

    std::atomic_bool m_transaction_active{false};
    std::atomic_bool m_pending_closed_transaction{false};

    std::atomic_bool stop_requested_{false};
    std::mutex stop_mutex_;
    std::condition_variable stop_cv_;
    std::thread live_measure_thread_;
    std::thread time_sync_thread_;

    void stop_threads();
    void configure_device();
    void read_public_key();
    void read_powermeter_values();
    void synchronize_time();
    void set_timezone();
    void time_sync_loop();
    [[nodiscard]] std::uint16_t read_charging_status();
    void apply_charging_status_on_configure(std::uint16_t status);
    void monitor_charging_status(std::uint16_t status);
    void write_identification_registers(const types::powermeter::TransactionReq& req);
    void send_charge_control(std::uint16_t command);
    void wait_for_signature();
    [[nodiscard]] std::string read_ocmf_document();
    void clear_transaction_states();
    void raise_communication_fault(const std::string& message);
    void clear_communication_fault();
    void raise_vendor_error(const std::string& sub_type, const std::string& message);
    void clear_vendor_error(const std::string& sub_type);
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace main
} // namespace module

#endif // MAIN_POWERMETER_IMPL_HPP
