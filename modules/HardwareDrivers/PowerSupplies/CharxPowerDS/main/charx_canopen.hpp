// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef CHARX_POWER_DS_CHARX_CANOPEN_HPP
#define CHARX_POWER_DS_CHARX_CANOPEN_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace charx {

// Objects of the CHARX power 40 kW DS (1740541) used by the driver.
// Units: voltages in mV, currents in mA, temperatures in 0.1 degC.
struct Obj {
    uint16_t index;
    uint8_t sub;
};
namespace obj {
constexpr Obj vendor_id{0x1018, 0x01};
constexpr Obj product_code{0x1018, 0x02};
constexpr Obj serial_number{0x1018, 0x04};
constexpr Obj readiness{0x4000, 0x01}; // 1 = PowerOn, 0 = PowerOff
constexpr Obj ds_output{0x4000, 0x0C}; // 0 open, 1 close contactor A, 2 close B
constexpr Obj temp_internal{0x4001, 0x02};
constexpr Obj flags1{0x4001, 0x16};
constexpr Obj flags2{0x4001, 0x17};
constexpr Obj ds_output_status{0x4001, 0x1C}; // 0 open, 1 A closed, 2 B closed
constexpr Obj contactor_error{0x4001, 0x1D};
constexpr Obj i_meas{0x4002, 0x02};
constexpr Obj i_max_avl{0x4002, 0x06};
constexpr Obj v_min_avl{0x4002, 0x09};
constexpr Obj v_max_avl{0x4002, 0x0A};
constexpr Obj v_meas{0x4002, 0x0C};
constexpr Obj i_set{0x4004, 0x02};
constexpr Obj v_set{0x4004, 0x06};
} // namespace obj

// Flags1 (0x4001:16) bit numbers, as decoded by the PS_CHARX_POWER library.
namespace flag {
constexpr int PfcOff = 1;
constexpr int DcOutputSideOff = 2;
constexpr int InternalFailure = 3;
constexpr int AcAsymmetry = 4;
constexpr int AcPhaseLoss = 5;
constexpr int ModuleIdRepetition = 7;
constexpr int Derating = 8;
constexpr int UvpInput = 10;
constexpr int UvpOutput = 11;
constexpr int OvpInput = 12;
constexpr int OvpOutput = 13;
constexpr int Utp = 14;
constexpr int Otp = 15;
constexpr int Ocp = 16;
constexpr int ShortCircuit = 17;
constexpr int OverPower = 18;
constexpr int FanFault = 19;
constexpr int DischargeProblem = 20;
constexpr int EmergencyStop = 21;
constexpr int AcOverload = 22;
constexpr int RemoteOff = 26;
constexpr int PowerOn = 29;
constexpr int ConverterError = 31;
} // namespace flag

std::string flags1_to_string(uint32_t flags);

struct SdoResult {
    enum class Status {
        Ok,
        Abort,
        Timeout,
        IoError
    } status{Status::IoError};
    uint32_t abort_code{0};
    int32_t value{0};

    bool ok() const {
        return status == Status::Ok;
    }
    std::string describe() const;
};

// Minimal CANopen master for one node: master heartbeat, NMT start and
// expedited SDO transfers. All SDO transfers are serialised.
class Canopen {
public:
    Canopen(const std::string& interface_name, uint8_t node_id, uint8_t master_node_id,
            std::chrono::milliseconds sdo_timeout);
    ~Canopen();
    Canopen(const Canopen&) = delete;
    Canopen& operator=(const Canopen&) = delete;

    void nmt_start_remote_node();
    SdoResult read(Obj o);
    // Writes are sent WITHOUT size indication (command 0x22), as the
    // PS_CHARX_POWER library does: the module rejects size-indicated writes to
    // its 1-byte objects (e.g. 0x4000:01) with abort 0x06070010.
    SdoResult write(Obj o, int32_t value);

private:
    void send(uint32_t can_id, const uint8_t* data, uint8_t len);
    SdoResult transfer(const uint8_t request[8], Obj o);
    void heartbeat_loop();

    int fd{-1};
    uint8_t node_id;
    uint8_t master_node_id;
    std::chrono::milliseconds sdo_timeout;
    std::mutex send_mutex;
    std::mutex sdo_mutex;
    std::atomic<bool> stop{false};
    std::thread heartbeat_thread;
};

} // namespace charx

#endif // CHARX_POWER_DS_CHARX_CANOPEN_HPP
