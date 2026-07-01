// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#ifndef MAIN_EV_SLAC_IMPL_HPP
#define MAIN_EV_SLAC_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include <generated/interfaces/ev_slac/Implementation.hpp>

#include "../EvSlacNeo.hpp"

class FSMController;

namespace everest {
namespace lib {
namespace slac {
class SlacEvent;
} // namespace slac
} // namespace lib
} // namespace everest

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
// insert your custom include headers here
#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <everest/io/event/event_fd.hpp>
#include <everest/slac/fsm/ev/context.hpp>
#include <everest/util/async/monitor.hpp>
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace slac_fsm = everest::lib::slac::fsm;

namespace module {
namespace main {

struct Conf {
    std::string device;
    int set_key_timeout_ms;
};

class ev_slacImpl : public ev_slacImplBase {
public:
    ev_slacImpl() = delete;
    ev_slacImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<EvSlacNeo>& mod, Conf& config);

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    ~ev_slacImpl() override;

    // Preparation for future generated/framework shutdown logic: when a shutdown phase is added next to init()/ready(),
    // that hook should call this helper directly. The destructor calls it today as a fallback for the current static
    // module lifetime.
    void shutdown();
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual void handle_reset() override;
    virtual bool handle_trigger_matching() override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<EvSlacNeo>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    void run();
    bool wait_for_ready_or_shutdown();
    bool initialize_slac_io();
    void configure_callbacks();
    void configure_fsm_context();
    bool create_fsm_controller();
    void configure_slac_io_callbacks();
    void run_blocking_event_loop();
    void handle_slac_io_ready();
    void handle_slac_io_error(bool on_error, const std::string& detail);
    FSMController* get_available_fsm_controller();
    void raise_communication_fault(const std::string& message);
    void clear_communication_fault();
    void mark_worker_offline(const std::string& reason);

    std::atomic<bool> online{true};
    std::thread worker;
    everest::lib::io::event::event_fd exit_event;
    struct LifecycleState {
        bool ready_requested{false};
        bool shutting_down{false};
        bool slac_io_ready{false};
        bool slac_fsm_started{false};
        bool communication_fault_raised{false};
        std::string communication_fault_message;
        FSMController* fsm_ctrl{nullptr};
    };
    everest::lib::util::monitor<LifecycleState> lifecycle_state;
    slac_fsm::ev::ContextCallbacks callbacks;
    std::unique_ptr<slac_fsm::ev::Context> fsm_ctx;
    std::unique_ptr<everest::lib::slac::SlacEvent> slac_io;
    std::unique_ptr<FSMController> fsm_ctrl;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    // insert your private definitions here
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace main
} // namespace module

#endif // MAIN_EV_SLAC_IMPL_HPP
