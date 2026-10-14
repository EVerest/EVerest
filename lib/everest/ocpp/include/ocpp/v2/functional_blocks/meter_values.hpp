// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <ocpp/v2/message_handler.hpp>

#include <map>
#include <memory>
#include <mutex>

#include <ocpp/common/aligned_timer.hpp>
#include <ocpp/v2/average_meter_values.hpp>
#include <ocpp/v2/connection_id.hpp>

namespace ocpp::v2 {
struct FunctionalBlockContext;
struct RequiredComponentVariable;

class MeterValuesInterface : public MessageHandlerInterface {
public:
    ~MeterValuesInterface() override = default;
    virtual void update_aligned_data_interval() = 0;
    /// \brief Event handler that should be called when a new meter value is present
    /// \param evse_id
    /// \param meter_value
    virtual void on_meter_value(const std::int32_t evse_id, const MeterValue& meter_value) = 0;
    virtual MeterValue get_latest_meter_value_filtered(const MeterValue& meter_value, ReadingContextEnum context,
                                                       const RequiredComponentVariable& component_variable) = 0;
    // Functional Block J: MeterValues
    virtual void meter_values_req(const std::int32_t evse_id, const std::vector<MeterValue>& meter_values,
                                  const bool initiated_by_trigger_message = false) = 0;
};

class MeterValues : public MeterValuesInterface {
public:
    ~MeterValues() override = default;
    explicit MeterValues(const FunctionalBlockContext& functional_block_context);
    void handle_message(const ocpp::EnhancedMessage<MessageType>& message) override;
    void update_aligned_data_interval() override;
    void on_meter_value(const std::int32_t evse_id, const MeterValue& meter_value) override;
    MeterValue get_latest_meter_value_filtered(const MeterValue& meter_value, ReadingContextEnum context,
                                               const RequiredComponentVariable& component_variable) override;

    void meter_values_req(const std::int32_t evse_id, const std::vector<MeterValue>& meter_values,
                          const bool initiated_by_trigger_message = false) override;

private: // Types
    /// \brief The clock-aligned meter values of one secondary OCPP connection.
    ///
    /// Application note section 4.5.1: a connection can be given a sampling rate and a set of
    /// measurands of its own, typically a much higher rate than is practical towards the CSMS over
    /// a mobile network. It therefore needs its own timer and its own averaging, so that reading
    /// values for one connection neither disturbs another nor disturbs the charging station's own
    /// clock-aligned data.
    struct ConnectionAlignedData {
        ClockAlignedTimer timer;
        AverageMeterValues evse0;
        std::map<std::int32_t, AverageMeterValues> per_evse;
    };

private: // Members
    const FunctionalBlockContext& context;

    ClockAlignedTimer aligned_meter_values_timer;
    AverageMeterValues aligned_data_evse0; // represents evseId = 0 meter value

    /// Entries are created when a connection is first configured with an interval and then kept,
    /// with the timer stopped, rather than removed: a timer that is being destroyed while its own
    /// callback runs is not worth the saving.
    std::map<ConnectionId, std::unique_ptr<ConnectionAlignedData>> secondary_aligned_data;
    std::mutex secondary_aligned_data_mutex;

private: // Functions
    /// \brief Starts, restarts or stops the clock-aligned meter value timer of every secondary
    ///        connection, following what the device model currently says.
    void update_secondary_aligned_data_intervals();

    /// \brief Sends \p connection the clock-aligned meter values it has accumulated.
    void send_secondary_aligned_meter_values(const ConnectionId& connection, ConnectionAlignedData& data,
                                             std::chrono::seconds interval);
};
} // namespace ocpp::v2
