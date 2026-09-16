// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/charge_parameter_discovery.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

static void convert(const struct iso2_AC_EVChargeParameterType& in, datatypes::AC_EVChargeParameter& out) {
    CB2CPP_ASSIGN_IF_USED(in.DepartureTime, out.departure_time);
    convert(in.EAmount, out.e_amount);
    convert(in.EVMaxVoltage, out.ev_max_voltage);
    convert(in.EVMaxCurrent, out.ev_max_current);
    convert(in.EVMinCurrent, out.ev_min_current);
}

static void convert(const datatypes::AC_EVChargeParameter& in, struct iso2_AC_EVChargeParameterType& out) {
    init_iso2_AC_EVChargeParameterType(&out);
    CPP2CB_ASSIGN_IF_USED(in.departure_time, out.DepartureTime);
    convert(in.e_amount, out.EAmount);
    convert(in.ev_max_voltage, out.EVMaxVoltage);
    convert(in.ev_max_current, out.EVMaxCurrent);
    convert(in.ev_min_current, out.EVMinCurrent);
}

static void convert(const struct iso2_DC_EVChargeParameterType& in, datatypes::DC_EVChargeParameter& out) {
    CB2CPP_ASSIGN_IF_USED(in.DepartureTime, out.departure_time);
    convert(in.DC_EVStatus, out.dc_ev_status);
    convert(in.EVMaximumCurrentLimit, out.ev_maximum_current_limit);
    if (in.EVMaximumPowerLimit_isUsed) {
        convert(in.EVMaximumPowerLimit, out.ev_maximum_power_limit.emplace());
    }
    convert(in.EVMaximumVoltageLimit, out.ev_maximum_voltage_limit);
    if (in.EVEnergyCapacity_isUsed) {
        convert(in.EVEnergyCapacity, out.ev_energy_capacity.emplace());
    }
    if (in.EVEnergyRequest_isUsed) {
        convert(in.EVEnergyRequest, out.ev_energy_request.emplace());
    }
    CB2CPP_ASSIGN_IF_USED(in.FullSOC, out.full_soc);
    CB2CPP_ASSIGN_IF_USED(in.BulkSOC, out.bulk_soc);
}

static void convert(const datatypes::DC_EVChargeParameter& in, struct iso2_DC_EVChargeParameterType& out) {
    init_iso2_DC_EVChargeParameterType(&out);
    CPP2CB_ASSIGN_IF_USED(in.departure_time, out.DepartureTime);
    convert(in.dc_ev_status, out.DC_EVStatus);
    convert(in.ev_maximum_current_limit, out.EVMaximumCurrentLimit);
    if (in.ev_maximum_power_limit) {
        convert(in.ev_maximum_power_limit.value(), out.EVMaximumPowerLimit);
        CB_SET_USED(out.EVMaximumPowerLimit);
    }
    convert(in.ev_maximum_voltage_limit, out.EVMaximumVoltageLimit);
    if (in.ev_energy_capacity) {
        convert(in.ev_energy_capacity.value(), out.EVEnergyCapacity);
        CB_SET_USED(out.EVEnergyCapacity);
    }
    if (in.ev_energy_request) {
        convert(in.ev_energy_request.value(), out.EVEnergyRequest);
        CB_SET_USED(out.EVEnergyRequest);
    }
    CPP2CB_ASSIGN_IF_USED(in.full_soc, out.FullSOC);
    CPP2CB_ASSIGN_IF_USED(in.bulk_soc, out.BulkSOC);
}

static void convert(const struct iso2_AC_EVSEChargeParameterType& in, datatypes::AC_EVSEChargeParameter& out) {
    convert(in.AC_EVSEStatus, out.ac_evse_status);
    convert(in.EVSENominalVoltage, out.evse_nominal_voltage);
    convert(in.EVSEMaxCurrent, out.evse_max_current);
}

static void convert(const datatypes::AC_EVSEChargeParameter& in, struct iso2_AC_EVSEChargeParameterType& out) {
    init_iso2_AC_EVSEChargeParameterType(&out);
    convert(in.ac_evse_status, out.AC_EVSEStatus);
    convert(in.evse_nominal_voltage, out.EVSENominalVoltage);
    convert(in.evse_max_current, out.EVSEMaxCurrent);
}

static void convert(const struct iso2_DC_EVSEChargeParameterType& in, datatypes::DC_EVSEChargeParameter& out) {
    convert(in.DC_EVSEStatus, out.dc_evse_status);
    convert(in.EVSEMaximumCurrentLimit, out.evse_maximum_current_limit);
    convert(in.EVSEMaximumPowerLimit, out.evse_maximum_power_limit);
    convert(in.EVSEMaximumVoltageLimit, out.evse_maximum_voltage_limit);
    convert(in.EVSEMinimumCurrentLimit, out.evse_minimum_current_limit);
    convert(in.EVSEMinimumVoltageLimit, out.evse_minimum_voltage_limit);
    if (in.EVSECurrentRegulationTolerance_isUsed) {
        convert(in.EVSECurrentRegulationTolerance, out.evse_current_regulation_tolerance.emplace());
    }
    convert(in.EVSEPeakCurrentRipple, out.evse_peak_current_ripple);
    if (in.EVSEEnergyToBeDelivered_isUsed) {
        convert(in.EVSEEnergyToBeDelivered, out.evse_energy_to_be_delivered.emplace());
    }
}

static void convert(const datatypes::DC_EVSEChargeParameter& in, struct iso2_DC_EVSEChargeParameterType& out) {
    init_iso2_DC_EVSEChargeParameterType(&out);
    convert(in.dc_evse_status, out.DC_EVSEStatus);
    convert(in.evse_maximum_current_limit, out.EVSEMaximumCurrentLimit);
    convert(in.evse_maximum_power_limit, out.EVSEMaximumPowerLimit);
    convert(in.evse_maximum_voltage_limit, out.EVSEMaximumVoltageLimit);
    convert(in.evse_minimum_current_limit, out.EVSEMinimumCurrentLimit);
    convert(in.evse_minimum_voltage_limit, out.EVSEMinimumVoltageLimit);
    if (in.evse_current_regulation_tolerance) {
        convert(in.evse_current_regulation_tolerance.value(), out.EVSECurrentRegulationTolerance);
        CB_SET_USED(out.EVSECurrentRegulationTolerance);
    }
    convert(in.evse_peak_current_ripple, out.EVSEPeakCurrentRipple);
    if (in.evse_energy_to_be_delivered) {
        convert(in.evse_energy_to_be_delivered.value(), out.EVSEEnergyToBeDelivered);
        CB_SET_USED(out.EVSEEnergyToBeDelivered);
    }
}

static void convert(const struct iso2_SAScheduleListType& in, datatypes::SAScheduleList& out) {
    out.clear();
    for (uint16_t i = 0; i < in.SAScheduleTuple.arrayLen; i++) {
        const auto& tuple = in.SAScheduleTuple.array[i];
        auto& out_tuple = out.emplace_back();
        out_tuple.sa_schedule_tuple_id = tuple.SAScheduleTupleID;
        for (uint16_t j = 0; j < tuple.PMaxSchedule.PMaxScheduleEntry.arrayLen; j++) {
            const auto& entry = tuple.PMaxSchedule.PMaxScheduleEntry.array[j];
            auto& out_entry = out_tuple.pmax_schedule.emplace_back();
            out_entry.start = entry.RelativeTimeInterval.start;
            if (entry.RelativeTimeInterval.duration_isUsed) {
                out_entry.duration = entry.RelativeTimeInterval.duration;
            }
            convert(entry.PMax, out_entry.p_max);
        }

        // [V2G2-659] SalesTariff support is mandatory for the EVCC; decode it (structure only -- the
        // nested ConsumptionCost detail is not modelled) instead of silently dropping it.
        if (tuple.SalesTariff_isUsed) {
            auto& out_st = out_tuple.sales_tariff.emplace();
            out_st.sales_tariff_id = tuple.SalesTariff.SalesTariffID;
            if (tuple.SalesTariff.SalesTariffDescription_isUsed) {
                out_st.sales_tariff_description = std::string(tuple.SalesTariff.SalesTariffDescription.characters,
                                                              tuple.SalesTariff.SalesTariffDescription.charactersLen);
            }
            if (tuple.SalesTariff.NumEPriceLevels_isUsed) {
                out_st.num_e_price_levels = tuple.SalesTariff.NumEPriceLevels;
            }
            for (uint16_t k = 0; k < tuple.SalesTariff.SalesTariffEntry.arrayLen; k++) {
                const auto& st_entry = tuple.SalesTariff.SalesTariffEntry.array[k];
                auto& out_st_entry = out_st.sales_tariff_entry.emplace_back();
                if (st_entry.RelativeTimeInterval_isUsed) {
                    out_st_entry.start = st_entry.RelativeTimeInterval.start;
                    if (st_entry.RelativeTimeInterval.duration_isUsed) {
                        out_st_entry.duration = st_entry.RelativeTimeInterval.duration;
                    }
                }
                if (st_entry.EPriceLevel_isUsed) {
                    out_st_entry.e_price_level = st_entry.EPriceLevel;
                }
            }
        }
    }
}

static void convert(const datatypes::SAScheduleList& in, struct iso2_SAScheduleListType& out) {
    init_iso2_SAScheduleListType(&out);
    uint16_t tuple_index = 0;
    for (const auto& tuple : in) {
        auto& out_tuple = out.SAScheduleTuple.array[tuple_index++];
        init_iso2_SAScheduleTupleType(&out_tuple);
        out_tuple.SAScheduleTupleID = tuple.sa_schedule_tuple_id;
        uint16_t entry_index = 0;
        for (const auto& entry : tuple.pmax_schedule) {
            auto& out_entry = out_tuple.PMaxSchedule.PMaxScheduleEntry.array[entry_index++];
            init_iso2_PMaxScheduleEntryType(&out_entry);
            out_entry.RelativeTimeInterval.start = entry.start;
            if (entry.duration) {
                out_entry.RelativeTimeInterval.duration = entry.duration.value();
                CB_SET_USED(out_entry.RelativeTimeInterval.duration);
            }
            CB_SET_USED(out_entry.RelativeTimeInterval);
            convert(entry.p_max, out_entry.PMax);
        }
        out_tuple.PMaxSchedule.PMaxScheduleEntry.arrayLen = tuple.pmax_schedule.size();
    }
    out.SAScheduleTuple.arrayLen = in.size();
}

template <> void convert(const struct iso2_ChargeParameterDiscoveryReqType& in, ChargeParameterDiscoveryRequest& out) {
    CB2CPP_ASSIGN_IF_USED(in.MaxEntriesSAScheduleTuple, out.max_entries_sa_schedule_tuple);
    cb_convert_enum(in.RequestedEnergyTransferMode, out.requested_energy_transfer_mode);
    if (in.AC_EVChargeParameter_isUsed) {
        convert(in.AC_EVChargeParameter, out.ac_ev_charge_parameter.emplace());
    }
    if (in.DC_EVChargeParameter_isUsed) {
        convert(in.DC_EVChargeParameter, out.dc_ev_charge_parameter.emplace());
    }
}

template <> void convert(const struct iso2_ChargeParameterDiscoveryResType& in, ChargeParameterDiscoveryResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    cb_convert_enum(in.EVSEProcessing, out.evse_processing);
    if (in.SAScheduleList_isUsed) {
        convert(in.SAScheduleList, out.sa_schedule_list.emplace());
    }
    if (in.AC_EVSEChargeParameter_isUsed) {
        convert(in.AC_EVSEChargeParameter, out.ac_evse_charge_parameter.emplace());
    }
    if (in.DC_EVSEChargeParameter_isUsed) {
        convert(in.DC_EVSEChargeParameter, out.dc_evse_charge_parameter.emplace());
    }
}

template <> void convert(const ChargeParameterDiscoveryRequest& in, struct iso2_ChargeParameterDiscoveryReqType& out) {
    init_iso2_ChargeParameterDiscoveryReqType(&out);
    CPP2CB_ASSIGN_IF_USED(in.max_entries_sa_schedule_tuple, out.MaxEntriesSAScheduleTuple);
    cb_convert_enum(in.requested_energy_transfer_mode, out.RequestedEnergyTransferMode);
    if (in.ac_ev_charge_parameter) {
        convert(in.ac_ev_charge_parameter.value(), out.AC_EVChargeParameter);
        CB_SET_USED(out.AC_EVChargeParameter);
    }
    if (in.dc_ev_charge_parameter) {
        convert(in.dc_ev_charge_parameter.value(), out.DC_EVChargeParameter);
        CB_SET_USED(out.DC_EVChargeParameter);
    }
}

template <> void convert(const ChargeParameterDiscoveryResponse& in, struct iso2_ChargeParameterDiscoveryResType& out) {
    init_iso2_ChargeParameterDiscoveryResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    cb_convert_enum(in.evse_processing, out.EVSEProcessing);
    if (in.sa_schedule_list) {
        convert(in.sa_schedule_list.value(), out.SAScheduleList);
        CB_SET_USED(out.SAScheduleList);
    }
    if (in.ac_evse_charge_parameter) {
        convert(in.ac_evse_charge_parameter.value(), out.AC_EVSEChargeParameter);
        CB_SET_USED(out.AC_EVSEChargeParameter);
    }
    if (in.dc_evse_charge_parameter) {
        convert(in.dc_evse_charge_parameter.value(), out.DC_EVSEChargeParameter);
        CB_SET_USED(out.DC_EVSEChargeParameter);
    }
}

template <> void insert_type(VariantAccess& va, const struct iso2_ChargeParameterDiscoveryReqType& in) {
    va.insert_type<ChargeParameterDiscoveryRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_ChargeParameterDiscoveryResType& in) {
    va.insert_type<ChargeParameterDiscoveryResponse>(in);
}

template <> int serialize_to_exi(const ChargeParameterDiscoveryRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ChargeParameterDiscoveryReq);
    convert(in, doc.V2G_Message.Body.ChargeParameterDiscoveryReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const ChargeParameterDiscoveryResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ChargeParameterDiscoveryRes);
    convert(in, doc.V2G_Message.Body.ChargeParameterDiscoveryRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const ChargeParameterDiscoveryRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const ChargeParameterDiscoveryResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
