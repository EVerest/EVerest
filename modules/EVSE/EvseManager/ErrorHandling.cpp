// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest

#include "ErrorHandling.hpp"

namespace {
std::string generate_description(const Everest::error::Error& error) {
    std::string result;
    if (auto n = error.type.find('/'); n != std::string::npos) {
        // found slash '/'
        if ((n + 1) == error.type.size()) {
            // slash is the last character
            result = error.type.substr(0, n);
        } else {
            // remove upto and including the first /
            result = error.type.substr(++n);
            // remove slash at the end
            if (result.back() == '/') {
                result.pop_back();
            }
        }
        if (!error.sub_type.empty()) {
            result += '/' + error.sub_type;
        }
    } else {
        // return the original description
        result = error.description;
    }
    return result;
}
} // namespace

namespace module {

using ErrorList = std::list<Everest::error::ErrorType>;
static const struct IgnoreErrors {
    // p_evse. We need to ignore Inoperative here as this is the result of this check.
    ErrorList evse{"evse_manager/Inoperative"};
    ErrorList bsp{"evse_board_support/MREC3HighTemperature", "evse_board_support/MREC18CableOverTempDerate",
                  "evse_board_support/VendorWarning"};
    ErrorList connector_lock{"connector_lock/VendorWarning"};
    ErrorList ac_rcd{"ac_rcd/VendorWarning"};
    ErrorList imd{"isolation_monitor/VendorWarning"};
    ErrorList powersupply{"power_supply_DC/VendorWarning"};
    ErrorList powermeter{"generic/VendorWarning"};
    ErrorList slac{"generic/VendorWarning"};
    ErrorList hlc{"generic/VendorWarning"};
    ErrorList over_voltage_monitor{"over_voltage_monitor/VendorWarning"};
} ignore_errors;

ErrorHandling::ErrorHandling(const std::unique_ptr<evse_board_supportIntf>& _r_bsp,
                             const std::vector<std::unique_ptr<ISO15118_chargerIntf>>& _r_hlc,
                             const std::vector<std::unique_ptr<connector_lockIntf>>& _r_connector_lock,
                             const std::vector<std::unique_ptr<ac_rcdIntf>>& _r_ac_rcd,
                             const std::unique_ptr<evse_managerImplBase>& _p_evse,
                             const std::vector<std::unique_ptr<isolation_monitorIntf>>& _r_imd,
                             const std::vector<std::unique_ptr<power_supply_DCIntf>>& _r_powersupply,
                             const std::vector<std::unique_ptr<powermeterIntf>>& _r_powermeter,
                             const std::vector<std::unique_ptr<slacIntf>>& _r_slac,
                             const std::vector<std::unique_ptr<over_voltage_monitorIntf>>& _r_over_voltage_monitor,
                             bool _inoperative_error_use_vendor_id) :
    r_bsp(_r_bsp),
    r_hlc(_r_hlc),
    r_connector_lock(_r_connector_lock),
    r_ac_rcd(_r_ac_rcd),
    p_evse(_p_evse),
    r_imd(_r_imd),
    r_powersupply(_r_powersupply),
    r_powermeter(_r_powermeter),
    r_slac(_r_slac),
    r_over_voltage_monitor(_r_over_voltage_monitor),
    inoperative_error_use_vendor_id(_inoperative_error_use_vendor_id) {

    // Subscribe to bsp driver to receive Errors from the bsp hardware
    r_bsp->subscribe_all_errors([this](const Everest::error::Error& error) { process_error(); },
                                [this](const Everest::error::Error& error) { process_error(); });

    // Subscribe to HLC to receive errors from ISO15118 charger module
    if (r_hlc.size() > 0) {
        r_hlc[0]->subscribe_all_errors([this](const Everest::error::Error& error) { process_error(); },
                                       [this](const Everest::error::Error& error) { process_error(); });
    }

    // Subscribe to connector lock to receive errors from connector lock hardware
    if (r_connector_lock.size() > 0) {
        r_connector_lock[0]->subscribe_all_errors([this](const Everest::error::Error& error) { process_error(); },
                                                  [this](const Everest::error::Error& error) { process_error(); });
    }

    // Subscribe to ac_rcd to receive errors from AC RCD hardware
    if (r_ac_rcd.size() > 0) {
        r_ac_rcd[0]->subscribe_all_errors([this](const Everest::error::Error& error) { process_error(); },
                                          [this](const Everest::error::Error& error) { process_error(); });
    }

    // Subscribe to ac_rcd to receive errors from IMD hardware
    if (r_imd.size() > 0) {
        r_imd[0]->subscribe_all_errors([this](const Everest::error::Error& error) { process_error(); },
                                       [this](const Everest::error::Error& error) { process_error(); });
    }

    // Subscribe to powersupply to receive errors from DC powersupply hardware
    if (r_powersupply.size() > 0) {
        r_powersupply[0]->subscribe_all_errors([this](const Everest::error::Error& error) { process_error(); },
                                               [this](const Everest::error::Error& error) { process_error(); });
    }

    // Subscribe to powermeter to receive errors from powermeter hardware
    if (r_powermeter.size() > 0) {
        r_powermeter[0]->subscribe_all_errors([this](const Everest::error::Error& error) { process_error(); },
                                              [this](const Everest::error::Error& error) { process_error(); });
    }

    // Subscribe to slac to receive errors from SLAC module
    if (r_slac.size() > 0) {
        r_slac[0]->subscribe_all_errors([this](const Everest::error::Error& error) { process_error(); },
                                        [this](const Everest::error::Error& error) { process_error(); });
    }

    // Subscribe to over_voltage_monitor to receive errors from over voltage monitor hardware
    if (r_over_voltage_monitor.size() > 0) {
        r_over_voltage_monitor[0]->subscribe_all_errors(
            [this](const Everest::error::Error& error) { process_error(); },
            [this](const Everest::error::Error& error) { process_error(); });
    }
}

void ErrorHandling::raise_overcurrent_error(const std::string& description) {
    // raise externally
    // Emergency shutdown according to IEC61851-23 Table CC.10 --> Severity::High
    Everest::error::Error error_object = p_evse->error_factory->create_error(
        "evse_manager/MREC4OverCurrentFailure", "", description, Everest::error::Severity::High);
    p_evse->raise_error(error_object);
    process_error();
}

void ErrorHandling::clear_overcurrent_error() {
    // clear externally
    if (p_evse->error_state_monitor->is_error_active("evse_manager/MREC4OverCurrentFailure", "")) {
        p_evse->clear_error("evse_manager/MREC4OverCurrentFailure", "");
    }
    process_error();
}

void ErrorHandling::raise_over_voltage_error(Everest::error::Severity severity, const std::string& description) {
    Everest::error::Error error_object =
        p_evse->error_factory->create_error("evse_manager/MREC5OverVoltage", "", description, severity);
    p_evse->raise_error(error_object);
    process_error();
}

void ErrorHandling::clear_over_voltage_error() {
    if (p_evse->error_state_monitor->is_error_active("evse_manager/MREC5OverVoltage", "")) {
        p_evse->clear_error("evse_manager/MREC5OverVoltage", "");
    }
    process_error();
}

// Find out if the current error set is fatal to charging or not
void ErrorHandling::process_error() {
    // Called from every requirement's error callbacks (potentially on different threads). Holding the monitor handle
    // serializes the whole inoperative_causes read-compare-clear-raise sequence below so it cannot interleave.
    auto causes = inoperative_causes.handle();

    const auto fatal = errors_prevent_charging();
    if (not fatal.empty()) {
        // signal to charger that errors are active that prevent charging
        raise_inoperative_error(fatal, *causes);
    } else {
        // signal an error that does not prevent charging
        clear_inoperative_error(*causes);
    }

    // All errors cleared signal is for OCPP 1.6. It is triggered when there are no errors anymore,
    // even those that did not block charging.

    auto number_of_active_errors = [](const auto& impl) {
        if (impl.size() > 0) {
            return static_cast<int>(impl[0]->error_state_monitor->get_active_errors().size());
        } else {
            return 0;
        }
    };

    const int error_count =
        p_evse->error_state_monitor->get_active_errors().size() +
        r_bsp->error_state_monitor->get_active_errors().size() + number_of_active_errors(r_connector_lock) +
        number_of_active_errors(r_ac_rcd) + number_of_active_errors(r_imd) + number_of_active_errors(r_powersupply) +
        number_of_active_errors(r_powermeter) + number_of_active_errors(r_slac) + number_of_active_errors(r_hlc);

    if (error_count == 0) {
        signal_all_errors_cleared();
    }
}

// Collect all errors from p_evse and all requirements that block charging, in detection order.
std::vector<Everest::error::Error> ErrorHandling::errors_prevent_charging() {
    std::vector<Everest::error::Error> fatal_errors;

    auto collect_fatal = [&fatal_errors](const auto& errors, const auto& ignore_list) {
        for (const auto& e : errors) {
            if (std::none_of(ignore_list.begin(), ignore_list.end(),
                             [&e](const auto& ign) { return e->type == ign; })) {
                fatal_errors.push_back(*e);
            }
        }
    };

    collect_fatal(p_evse->error_state_monitor->get_active_errors(), ignore_errors.evse);
    collect_fatal(r_bsp->error_state_monitor->get_active_errors(), ignore_errors.bsp);
    if (r_connector_lock.size() > 0) {
        collect_fatal(r_connector_lock[0]->error_state_monitor->get_active_errors(), ignore_errors.connector_lock);
    }
    if (r_ac_rcd.size() > 0) {
        collect_fatal(r_ac_rcd[0]->error_state_monitor->get_active_errors(), ignore_errors.ac_rcd);
    }
    if (r_imd.size() > 0) {
        collect_fatal(r_imd[0]->error_state_monitor->get_active_errors(), ignore_errors.imd);
    }
    if (r_powersupply.size() > 0) {
        collect_fatal(r_powersupply[0]->error_state_monitor->get_active_errors(), ignore_errors.powersupply);
    }
    if (r_powermeter.size() > 0) {
        collect_fatal(r_powermeter[0]->error_state_monitor->get_active_errors(), ignore_errors.powermeter);
    }
    if (r_slac.size() > 0) {
        collect_fatal(r_slac[0]->error_state_monitor->get_active_errors(), ignore_errors.slac);
    }
    if (r_hlc.size() > 0) {
        collect_fatal(r_hlc[0]->error_state_monitor->get_active_errors(), ignore_errors.hlc);
    }
    if (r_over_voltage_monitor.size() > 0) {
        collect_fatal(r_over_voltage_monitor[0]->error_state_monitor->get_active_errors(),
                      ignore_errors.over_voltage_monitor);
    }

    return fatal_errors;
}

// Caller must hold the inoperative_causes monitor handle (this mutates it).
void ErrorHandling::raise_inoperative_error(const std::vector<Everest::error::Error>& causes,
                                            InoperativeCauses& inoperative_causes) {
    if (causes.empty()) {
        return;
    }

    auto is_emergency = [](const auto& errors) {
        return std::any_of(errors.begin(), errors.end(), [](const Everest::error::Error& cause) {
            return cause.severity == Everest::error::Severity::High;
        });
    };

    // Everest::error::Error has no operator==, so compare the two sets by their (type, sub_type) keys via the
    // comparator: same size and every corresponding element equivalent under ErrorCauseLess.
    auto same_causes = [](const InoperativeCauses& a, const InoperativeCauses& b) {
        const ErrorCauseLess less;
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(),
                          [&less](const Everest::error::Error& x, const Everest::error::Error& y) {
                              return not less(x, y) and not less(y, x);
                          });
    };

    // Key the causes on (type, sub_type); the set makes change-detection independent of detection order.
    const InoperativeCauses current(causes.begin(), causes.end());

    const bool already_raised = p_evse->error_state_monitor->is_error_active("evse_manager/Inoperative", "");
    if (already_raised && same_causes(current, inoperative_causes)) {
        // Same set of causes, nothing to update.
        return;
    }

    const bool was_emergency = is_emergency(inoperative_causes);

    if (already_raised) {
        // Causes changed: the framework has no in-place update, so clear and re-raise. No
        // AllErrorsPreventingChargingCleared is emitted, keeping the charger shut down across the refresh.
        p_evse->clear_error("evse_manager/Inoperative");
    }

    // First cause drives message and vendor_id; the description lists every active cause. Downstream consumers with
    // shorter limits (e.g. OCPP 1.6 StatusNotification, CiString<50>) truncate it themselves.
    const auto& primary = causes.front();
    std::string description = generate_description(primary);
    for (auto it = std::next(causes.begin()); it != causes.end(); ++it) {
        description += ", " + generate_description(*it);
    }

    Everest::error::Error error_object = p_evse->error_factory->create_error(
        "evse_manager/Inoperative", "", primary.type, Everest::error::Severity::High);
    error_object.description = description;
    if (inoperative_error_use_vendor_id && !primary.vendor_id.empty()) {
        error_object.vendor_id = primary.vendor_id;
    } else {
        error_object.vendor_id = "EVerest";
    }
    p_evse->raise_error(error_object);
    // Track what we just raised, so the next call detects a changed cause set.
    inoperative_causes = current;

    // Emergency shutdown if any active cause is high severity, otherwise error shutdown. Only (re)signal on a genuine
    // transition -- the first raise or a change of shutdown type. A pure description refresh must not re-signal:
    // downstream this cancels an active reservation again (EvseManager) on every refresh, and the charger already
    // stays shut down without a repeat.
    const bool emergency = is_emergency(causes);
    if (not already_raised or emergency != was_emergency) {
        signal_error(emergency ? ErrorHandlingEvents::ForceEmergencyShutdown : ErrorHandlingEvents::ForceErrorShutdown);
    }
}

// Caller must hold the inoperative_causes monitor handle (this mutates it).
void ErrorHandling::clear_inoperative_error(InoperativeCauses& inoperative_causes) {
    // clear externally
    if (p_evse->error_state_monitor->is_error_active("evse_manager/Inoperative", "")) {
        p_evse->clear_error("evse_manager/Inoperative");
        inoperative_causes.clear();
        signal_error(ErrorHandlingEvents::AllErrorsPreventingChargingCleared);
    }
}

void ErrorHandling::raise_internal_error(const std::string& description) {
    // raise externally
    Everest::error::Error error_object =
        p_evse->error_factory->create_error("evse_manager/Internal", "", description, Everest::error::Severity::High);
    p_evse->raise_error(error_object);
    process_error();
}

void ErrorHandling::clear_internal_error() {
    // clear externally
    if (p_evse->error_state_monitor->is_error_active("evse_manager/Internal", "")) {
        p_evse->clear_error("evse_manager/Internal");
        process_error();
    }
}

void ErrorHandling::raise_authorization_timeout_error(const std::string& description) {
    // raise externally
    Everest::error::Error error_object = p_evse->error_factory->create_error(
        "evse_manager/MREC9AuthorizationTimeout", "", description, Everest::error::Severity::Medium);
    p_evse->raise_error(error_object);
    process_error();
}

void ErrorHandling::clear_authorization_timeout_error() {
    // clear externally
    if (p_evse->error_state_monitor->is_error_active("evse_manager/MREC9AuthorizationTimeout", "")) {
        p_evse->clear_error("evse_manager/MREC9AuthorizationTimeout");
        process_error();
    }
}

void ErrorHandling::raise_powermeter_transaction_start_failed_error(const std::string& description) {
    // raise externally
    Everest::error::Error error_object = p_evse->error_factory->create_error(
        "evse_manager/PowermeterTransactionStartFailed", "", description, Everest::error::Severity::Medium);
    p_evse->raise_error(error_object);
    process_error();
}

void ErrorHandling::clear_powermeter_transaction_start_failed_error() {
    // clear externally
    if (p_evse->error_state_monitor->is_error_active("evse_manager/PowermeterTransactionStartFailed", "")) {
        p_evse->clear_error("evse_manager/PowermeterTransactionStartFailed");
        process_error();
    }
}

void ErrorHandling::raise_isolation_resistance_fault(const std::string& description, const std::string& sub_type) {
    // Error shutdown according to IEC61851-23 Table CC.10 --> Severity::Medium
    Everest::error::Error error_object = p_evse->error_factory->create_error(
        "evse_manager/MREC22ResistanceFault", sub_type, description, Everest::error::Severity::Medium);
    p_evse->raise_error(error_object);
    process_error();
}

void ErrorHandling::clear_isolation_resistance_fault(const std::string& sub_type) {
    if (p_evse->error_state_monitor->is_error_active("evse_manager/MREC22ResistanceFault", sub_type)) {
        p_evse->clear_error("evse_manager/MREC22ResistanceFault", sub_type);
        process_error();
    }
}

void ErrorHandling::raise_cable_check_fault(const std::string& description) {
    // Error shutdown according to IEC61851-23 Table CC.10 --> Severity::Medium
    Everest::error::Error error_object = p_evse->error_factory->create_error(
        "evse_manager/MREC11CableCheckFault", "Self test failed", description, Everest::error::Severity::Medium);
    p_evse->raise_error(error_object);
    process_error();
}

void ErrorHandling::clear_cable_check_fault() {
    if (p_evse->error_state_monitor->is_error_active("evse_manager/MREC11CableCheckFault", "Self test failed")) {
        p_evse->clear_error("evse_manager/MREC11CableCheckFault", "Self test failed");
        process_error();
    }
}

void ErrorHandling::raise_voltage_plausibility_fault(const std::string& description) {
    // raise externally
    // High severity as this indicates a serious measurement inconsistency
    Everest::error::Error error_object = p_evse->error_factory->create_error(
        "evse_manager/VoltagePlausibilityFault", "", description, Everest::error::Severity::High);
    p_evse->raise_error(error_object);
    process_error();
}

void ErrorHandling::clear_voltage_plausibility_fault() {
    // clear externally
    if (p_evse->error_state_monitor->is_error_active("evse_manager/VoltagePlausibilityFault", "")) {
        p_evse->clear_error("evse_manager/VoltagePlausibilityFault");
        process_error();
    }
}

} // namespace module
