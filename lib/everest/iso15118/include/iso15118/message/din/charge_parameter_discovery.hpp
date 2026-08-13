// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/din/msg_data_types.hpp>

#include <optional>
#include <variant>
#include <vector>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::din::msg {

namespace data_types {

using SAID = int16_t;

enum class CostKind {
    RelativePricePercentage,
    RenewableGenerationPercentage,
    CarbonDioxideEmission
};

struct AcEvChargeParameter {
    std::optional<uint32_t> departure_time; // Seconds to departure
    PhysicalValue e_amount;                 // Wh
    PhysicalValue ev_max_voltage;
    PhysicalValue ev_max_current;
    PhysicalValue ev_min_current;
};

struct DcEvChargeParameter {
    DcEvStatus dc_ev_status;
    PhysicalValue ev_maximum_current_limit;
    std::optional<PhysicalValue> ev_maximum_power_limit;
    PhysicalValue ev_maximum_voltage_limit;
    std::optional<PhysicalValue> ev_energy_capacity; // Wh
    std::optional<PhysicalValue> ev_energy_request;  // Wh
    std::optional<PercentValue> full_soc;
    std::optional<PercentValue> bulk_soc;
};

struct RelativeTimeInterval {
    uint32_t start;                   // Start in s from now
    std::optional<uint32_t> duration; // Duration in s
};

struct Entry {
    RelativeTimeInterval time_interval;
};

struct Cost {
    uint32_t amount;                         // cost per kWh
    std::optional<int8_t> amount_multiplier; // [-3 - 3]
    CostKind cost_kind;
};

struct ConsumptionCost {
    unsigned int start_value; // W
    std::optional<Cost> cost;
};

struct SalesTariffEntry : Entry {
    uint8_t e_price_level;
    std::optional<ConsumptionCost> consumption_cost;
};
constexpr auto SalesTariffEntryConsumptionCostMaxLength = 3;

struct SalesTariff {
    std::string id;
    SAScheduleTupleID sales_tariff_id;
    std::optional<std::string> sales_tariff_description; // MaxLength: 32
    uint8_t num_e_price_levels;
    everest::lib::util::fixed_vector<SalesTariffEntry, din_SalesTariffEntryType_5_ARRAY_SIZE> sales_tariff_entry;
};

struct PMaxScheduleEntry : Entry {
    int16_t p_max; // W
};
using PMaxScheduleEntryList =
    everest::lib::util::fixed_vector<PMaxScheduleEntry, din_PMaxScheduleEntryType_5_ARRAY_SIZE>;

struct PMaxSchedule {
    SAID pmax_schedule_id;
    PMaxScheduleEntryList pmax_schedule_entry;
};

struct SaScheduleTuple {
    SAScheduleTupleID sa_schedule_tuple_id;
    PMaxSchedule pmax_schedule;
    std::optional<SalesTariff> sales_tariff;
};
using SaSchedules = everest::lib::util::fixed_vector<SaScheduleTuple, din_SAScheduleTupleType_5_ARRAY_SIZE>;

struct AcEvseChargeParameter {
    AcEvseStatus ac_evse_status;
    PhysicalValue evse_max_voltage;
    PhysicalValue evse_max_current;
    PhysicalValue evse_min_current;
};

struct DcEvseChargeParameter {
    DcEvseStatus dc_evse_status;
    PhysicalValue evse_maximum_current_limit;
    std::optional<PhysicalValue> evse_maximum_power_limit;
    PhysicalValue evse_maximum_voltage_limit;
    PhysicalValue evse_minimum_current_limit;
    PhysicalValue evse_minimum_voltage_limit;
    std::optional<PhysicalValue> evse_current_regulation_tolerance;
    PhysicalValue evse_peak_current_ripple;
    std::optional<PhysicalValue> evse_energy_to_be_delivered;
};

enum EvRequestedEnergyTransfer {
    AC_single_phase_core,
    AC_three_phase_core,
    DC_core,
    DC_extended,
    DC_combo_core,
    DC_unique,
};

} // namespace data_types

struct ChargeParameterDiscoveryRequest {
    Header header;
    data_types::EvRequestedEnergyTransfer requested_energy_transfer_mode;
    std::variant<data_types::AcEvChargeParameter, data_types::DcEvChargeParameter> ev_charge_parameter;
};

struct ChargeParameterDiscoveryResponse {
    Header header;
    data_types::ResponseCode response_code;
    data_types::EvseProcessing evse_processing;
    std::optional<data_types::SaSchedules> sa_schedule_list;
    std::variant<data_types::AcEvseChargeParameter, data_types::DcEvseChargeParameter> evse_charge_parameter;
};

} // namespace iso15118::din::msg
