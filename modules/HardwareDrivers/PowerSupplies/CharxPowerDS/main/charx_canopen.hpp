// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef CHARX_POWER_DS_CHARX_CANOPEN_HPP
#define CHARX_POWER_DS_CHARX_CANOPEN_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace charx {

// An object of the CHARX power 40 kW DS (1740541) object dictionary.
// Units: voltages in mV, currents in mA, temperatures in 0.1 degC.
struct Obj {
    uint16_t index;
    uint8_t sub;
};

namespace obj {
constexpr Obj readiness{0x4000, 0x01};        // 1 = PowerOn, 0 = PowerOff
constexpr Obj ds_output{0x4000, 0x0C};        // 0 open, 1 close contactor A, 2 close B
constexpr Obj flags1{0x4001, 0x16};           // status and fault bits, see namespace flag
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

// SDO abort codes the module uses
constexpr uint32_t ABORT_DEVICE_STATE = 0x08000022; // e.g. PowerOn while the contactor is still open
constexpr uint32_t ABORT_LENGTH = 0x06070010;       // size-indicated write to a 1-byte object

struct SdoResult {
    // Protocol: an answer that is neither the expected response nor an abort
    enum class Status {
        Ok,
        Abort,
        Timeout,
        IoError,
        Protocol
    } status{Status::IoError};
    uint32_t abort_code{0};
    int32_t value{0};

    bool ok() const {
        return status == Status::Ok;
    }
    std::string describe() const;
};

using Frame = std::array<uint8_t, 8>;

// SDO client requests (CiA 301 expedited transfer)
Frame sdo_upload_request(Obj o);
// Sent WITHOUT size indication (command 0x22), as the PS_CHARX_POWER library does: the module rejects
// size-indicated writes to its 1-byte objects (e.g. 0x4000:01) with ABORT_LENGTH.
Frame sdo_download_request(Obj o, int32_t value);

// Interprets a frame from the node's SDO server channel as the answer to an upload (read) or download (write)
// of o. Returns nullopt if the frame does not refer to o, so the caller keeps waiting.
std::optional<SdoResult> parse_sdo_response(const uint8_t* data, std::size_t len, Obj o, bool upload);

// What the controller needs from the bus. Implemented by Canopen, and by fakes in the unit tests.
class SdoClient {
public:
    virtual ~SdoClient() = default;
    virtual SdoResult read(Obj o) = 0;
    virtual SdoResult write(Obj o, int32_t value) = 0;
    // NMT "start remote node"; false if the frame could not be sent
    virtual bool start_remote_node() = 0;
};

// Minimal CANopen master for one node over SocketCAN: master heartbeat, NMT start and expedited SDO transfers.
// SDO transfers are serialised. No call throws once the object is constructed: a failed send (interface down,
// transmit queue full because no node acknowledges) is reported as SdoResult::Status::IoError.
class Canopen : public SdoClient {
public:
    // Throws std::runtime_error if the CAN interface cannot be opened.
    Canopen(const std::string& interface_name, uint8_t node_id, uint8_t master_node_id,
            std::chrono::milliseconds sdo_timeout);
    // Takes ownership of an open, connected datagram socket that carries struct can_frame - e.g. one end of a
    // socketpair in the unit tests. No receive filter is set on it.
    Canopen(int socket_fd, uint8_t node_id, uint8_t master_node_id, std::chrono::milliseconds sdo_timeout);
    ~Canopen() override;
    Canopen(const Canopen&) = delete;
    Canopen& operator=(const Canopen&) = delete;

    SdoResult read(Obj o) override;
    SdoResult write(Obj o, int32_t value) override;
    bool start_remote_node() override;

    // Number of transfers in a row that failed with IoError; the owner reopens the socket when it grows.
    int io_errors_in_row() const {
        return io_errors.load();
    }

private:
    bool send(uint32_t can_id, const uint8_t* data, uint8_t len);
    SdoResult transfer(const Frame& request, Obj o, bool upload);
    void heartbeat_loop();

    int fd{-1};
    uint8_t node_id;
    uint8_t master_node_id;
    std::chrono::milliseconds sdo_timeout;
    std::mutex send_mutex;
    std::mutex sdo_mutex;
    std::atomic<int> io_errors{0};
    std::atomic<bool> stop{false};
    std::thread heartbeat_thread;
};

} // namespace charx

#endif // CHARX_POWER_DS_CHARX_CANOPEN_HPP
