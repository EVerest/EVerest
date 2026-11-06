// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/service_detail.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include <cbv2g/iso_20/iso20_CommonMessages_Datatypes.h>

#include <iso15118/d20/state/service_selection.hpp>

#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/state/service_detail.hpp>

#include <everest/util/misc/container.hpp>
#include <iso15118/detail/d20/state/session_stop.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::state {

namespace dt = message_20::datatypes;

namespace {

bool find_energy_services(const std::vector<uint16_t>& services, const uint16_t service) {
    return everest::lib::util::exists(services, service);
}

void fill_internet_parameter_list(std::vector<dt::InternetParameterList>& internet_parameter_list,
                                  const dt::ServiceParameterList& custom_vas_parameters) {
    for (const auto& parameter_set : custom_vas_parameters) {
        auto& internet_parameter = internet_parameter_list.emplace_back();
        if (parameter_set.id == 1) {
            internet_parameter.port = dt::Port::Port20;
            internet_parameter.protocol = dt::Protocol::Ftp;
        } else if (parameter_set.id == 2) {
            internet_parameter.port = dt::Port::Port21;
            internet_parameter.protocol = dt::Protocol::Ftp;
        } else if (parameter_set.id == 3) {
            internet_parameter.port = dt::Port::Port80;
            internet_parameter.protocol = dt::Protocol::Http;
        } else if (parameter_set.id == 4) {
            internet_parameter.port = dt::Port::Port443;
            internet_parameter.protocol = dt::Protocol::Https;
        }
    }
}

void fill_parking_parameter_list(std::vector<message_20::datatypes::ParkingParameterList>& parking_parameter_list,
                                 const dt::ServiceParameterList& custom_vas_parameters) {
    for (const auto& parameter_set : custom_vas_parameters) {
        auto& parking_parameter = parking_parameter_list.emplace_back();
        for (const auto& parameter : parameter_set.parameter) {
            const auto value = std::get<int32_t>(parameter.value);
            if (parameter.name == "IntendedService") {
                parking_parameter.intended_service = static_cast<dt::IntendedService>(value);
            } else if (parameter.name == "ParkingStatusType") {
                parking_parameter.parking_status = static_cast<dt::ParkingStatus>(value);
            }
        }
    }
}

// 32 parameter sets of 32 parameters each are allowed by the schema, but cbExiGen generates much smaller arrays.
// Do not offer more than can be encoded.
constexpr std::size_t MAX_PARAMETER_SETS = iso20_ParameterSetType_4_ARRAY_SIZE;
constexpr std::size_t MAX_PARAMETERS = iso20_ParameterType_8_ARRAY_SIZE;

bool can_offer_another_set(const message_20::ServiceDetailResponse& res) {
    if (res.service_parameter_list.size() < MAX_PARAMETER_SETS) {
        return true;
    }
    logf_warning("Service %u has more parameter sets than a ServiceDetailRes carries (%zu), dropping the rest",
                 res.service, MAX_PARAMETER_SETS);
    return false;
}

template <typename ParameterList>
void offer_parameter_sets(message_20::ServiceDetailResponse& res, const std::vector<ParameterList>& parameter_lists,
                          std::map<uint8_t, ParameterList>& offered) {
    uint8_t id = 0;
    for (const auto& parameter_list : parameter_lists) {
        if (not can_offer_another_set(res)) {
            break;
        }
        offered[id] = parameter_list;
        res.service_parameter_list.push_back(dt::ParameterSet(id++, parameter_list));
    }
}

void drop_unencodable_vas_parameters(message_20::ServiceDetailResponse& res) {
    auto& parameter_sets = res.service_parameter_list;
    if (parameter_sets.size() > MAX_PARAMETER_SETS) {
        logf_warning("VAS %u has %zu parameter sets, a ServiceDetailRes carries at most %zu, dropping the rest",
                     res.service, parameter_sets.size(), MAX_PARAMETER_SETS);
        while (parameter_sets.size() > MAX_PARAMETER_SETS) {
            parameter_sets.pop_back();
        }
    }
    for (auto& parameter_set : parameter_sets) {
        if (parameter_set.parameter.size() > MAX_PARAMETERS) {
            logf_warning("VAS %u parameter set %u has %zu parameters, a ServiceDetailRes carries at most %zu, dropping "
                         "the rest",
                         res.service, parameter_set.id, parameter_set.parameter.size(), MAX_PARAMETERS);
            while (parameter_set.parameter.size() > MAX_PARAMETERS) {
                parameter_set.parameter.pop_back();
            }
        }
    }
}

} // namespace

message_20::ServiceDetailResponse handle_request(const message_20::ServiceDetailRequest& req, d20::Session& session,
                                                 const session::SessionConfig& config,
                                                 const std::optional<dt::ServiceParameterList>& custom_vas_parameters) {

    message_20::ServiceDetailResponse res;

    if (not validate_and_setup_header(res.header, session, req.header.session_id)) {
        set_response_code(res, dt::ResponseCode::FAILED_UnknownSession);
        return res;
    }

    bool service_found = false;

    for (auto& energy_service : session.offered_services.energy_services) {
        if (message_20::to_underlying_value(energy_service) == req.service) {
            service_found = true;
            break;
        }
    }

    for (auto& vas_service : session.offered_services.vas_services) {
        if (vas_service == req.service) {
            service_found = true;
            break;
        }
    }

    if (!service_found) {
        set_response_code(res, dt::ResponseCode::FAILED_ServiceIDInvalid);
        return res;
    }

    res.service_parameter_list.clear(); // reset default values

    if (custom_vas_parameters.has_value()) {
        logf_info("Sending custom vas parameters");

        res.service = req.service;
        res.service_parameter_list = custom_vas_parameters.value();
        drop_unencodable_vas_parameters(res);

        std::vector<uint16_t> parameter_set_ids{};
        for (const auto& vas : res.service_parameter_list) {
            parameter_set_ids.push_back(vas.id);
        }
        session.offered_services.custom_vas_list[req.service] = parameter_set_ids;

        set_response_code(res, dt::ResponseCode::OK);
        return res;
    }

    if (req.service == message_20::to_underlying_value(dt::ServiceCategory::AC)) {
        res.service = message_20::to_underlying_value(dt::ServiceCategory::AC);
        offer_parameter_sets(res, config.ac_parameter_list, session.offered_services.ac_parameter_list);
    } else if (req.service == message_20::to_underlying_value(dt::ServiceCategory::AC_BPT)) {
        res.service = message_20::to_underlying_value(dt::ServiceCategory::AC_BPT);
        offer_parameter_sets(res, config.ac_bpt_parameter_list, session.offered_services.ac_bpt_parameter_list);
    } else if (req.service == message_20::to_underlying_value(dt::ServiceCategory::AC_DER_IEC)) {
        res.service = message_20::to_underlying_value(dt::ServiceCategory::AC_DER_IEC);
        offer_parameter_sets(res, config.ac_der_iec_parameter_list, session.offered_services.ac_der_iec_parameter_list);
    } else if (req.service == message_20::to_underlying_value(dt::ServiceCategory::AC_DER_SAE)) {
        res.service = message_20::to_underlying_value(dt::ServiceCategory::AC_DER_SAE);
        // Using AC parameter list
        offer_parameter_sets(res, config.ac_parameter_list, session.offered_services.ac_parameter_list);
    } else if (req.service == message_20::to_underlying_value(dt::ServiceCategory::DC)) {
        res.service = message_20::to_underlying_value(dt::ServiceCategory::DC);
        offer_parameter_sets(res, config.dc_parameter_list, session.offered_services.dc_parameter_list);
    } else if (req.service == message_20::to_underlying_value(dt::ServiceCategory::DC_BPT)) {
        res.service = message_20::to_underlying_value(dt::ServiceCategory::DC_BPT);
        offer_parameter_sets(res, config.dc_bpt_parameter_list, session.offered_services.dc_bpt_parameter_list);
    } else if (req.service == message_20::to_underlying_value(dt::ServiceCategory::MCS)) {
        res.service = message_20::to_underlying_value(dt::ServiceCategory::MCS);
        offer_parameter_sets(res, config.mcs_parameter_list, session.offered_services.mcs_parameter_list);
    } else if (req.service == message_20::to_underlying_value(dt::ServiceCategory::MCS_BPT)) {
        res.service = message_20::to_underlying_value(dt::ServiceCategory::MCS_BPT);
        offer_parameter_sets(res, config.mcs_bpt_parameter_list, session.offered_services.mcs_bpt_parameter_list);
    } else if (req.service == message_20::to_underlying_value(dt::ServiceCategory::Internet)) {
        res.service = message_20::to_underlying_value(dt::ServiceCategory::Internet);

        uint8_t id = 0;
        for (auto& parameter_set : config.internet_parameter_list) {
            if (not can_offer_another_set(res)) {
                break;
            }
            // TODO(sl): Possibly refactor, define const
            if (parameter_set.port == dt::Port::Port20) {
                id = 1;
            } else if (parameter_set.port == dt::Port::Port21) {
                id = 2;
            } else if (parameter_set.port == dt::Port::Port80) {
                id = 3;
            } else if (parameter_set.port == dt::Port::Port443) {
                id = 4;
            }
            session.offered_services.internet_parameter_list[id] = parameter_set;
            res.service_parameter_list.push_back(dt::ParameterSet(id, parameter_set));
        }
    } else if (req.service == message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)) {
        res.service = message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus);
        offer_parameter_sets(res, config.parking_parameter_list, session.offered_services.parking_parameter_list);
    } else {
        logf_warning("There is no parameters for this service %u available. Sending an \"empty\" response.",
                     req.service);
        res.service = req.service;
        res.service_parameter_list = {dt::ParameterSet(0)};
    }

    set_response_code(res, dt::ResponseCode::OK);
    return res;
}

void ServiceDetail::enter() {
    logf_debug("Enter state: ServiceDetail");
}

Result ServiceDetail::feed(Event ev) {

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_20::ServiceDetailRequest>()) {
        logf_info("Requested info about ServiceID: %d", req->service);

        using Service = dt::ServiceCategory;
        const std::vector<std::uint16_t> energy_services{
            message_20::to_underlying_value(Service::AC),          message_20::to_underlying_value(Service::DC),
            message_20::to_underlying_value(Service::WPT),         message_20::to_underlying_value(Service::DC_ACDP),
            message_20::to_underlying_value(Service::AC_BPT),      message_20::to_underlying_value(Service::DC_BPT),
            message_20::to_underlying_value(Service::DC_ACDP_BPT), message_20::to_underlying_value(Service::MCS),
            message_20::to_underlying_value(Service::MCS_BPT),     message_20::to_underlying_value(Service::AC_DER_IEC),
            message_20::to_underlying_value(Service::AC_DER_SAE)};

        std::optional<dt::ServiceParameterList> custom_vas_parameters{std::nullopt};

        if (not find_energy_services(energy_services, req->service)) {
            logf_info("Getting vas (id: %u) parameters", req->service);
            custom_vas_parameters = m_ctx.feedback.get_vas_parameters(req->service);

            if (custom_vas_parameters.has_value() and
                req->service == message_20::to_underlying_value(dt::ServiceCategory::Internet)) {
                m_ctx.session_config.internet_parameter_list.clear();

                fill_internet_parameter_list(m_ctx.session_config.internet_parameter_list,
                                             custom_vas_parameters.value());
                custom_vas_parameters.reset();

            } else if (custom_vas_parameters.has_value() and
                       req->service == message_20::to_underlying_value(dt::ServiceCategory::ParkingStatus)) {
                m_ctx.session_config.parking_parameter_list.clear();

                fill_parking_parameter_list(m_ctx.session_config.parking_parameter_list, custom_vas_parameters.value());
                custom_vas_parameters.reset();
            }
        }

        const auto res = handle_request(*req, m_ctx.session, m_ctx.session_config, custom_vas_parameters);

        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        return m_ctx.create_state<ServiceSelection>();
    } else if (const auto req = variant->get_if<message_20::SessionStopRequest>()) {
        const auto res = handle_request(*req, m_ctx.session);

        m_ctx.respond(res);
        mark_session_stop_response(m_ctx, *req, res);
        m_ctx.session_stopped = true;

        return {};
    } else {
        logf_warning("Expected ServiceDetailReq! But code type id: %d", variant->get_type());

        // Sequence Error
        const message_20::Type req_type = variant->get_type();
        send_sequence_error(req_type, m_ctx);

        m_ctx.session_stopped = true;
        return {};
    }
}

} // namespace iso15118::d20::state
