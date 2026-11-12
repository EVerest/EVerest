// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "powermeterImpl.hpp"
#include "http_client.hpp"
#include "lem_dcbm_time_sync_helper.hpp"
#include "measurement_change.hpp"
#include "poll_scheduler.hpp"
#include <chrono>
#include <everest/logging.hpp>
#include <fmt/core.h>
#include <optional>
#include <string>
#include <thread>

namespace module::main {

namespace {

/// \brief Reports an unexpectedly long gap between two consecutive polls of the live measurements.
///
/// Consumers of the powermeter var watch the publication stream for gaps - EvseManager aborts a DC cable
/// check when it sees no power supply measurement for two seconds. A publication needs a poll that returns
/// a new measurement, so a poll gap that grows well beyond the configured interval is worth reporting even
/// though nothing here has failed.
void warn_on_poll_gap(std::chrono::steady_clock::time_point last_poll, std::chrono::steady_clock::time_point poll,
                      std::chrono::steady_clock::duration request_duration, std::chrono::milliseconds poll_interval) {
    if (last_poll == std::chrono::steady_clock::time_point{}) {
        return; // first poll, there is no gap to report yet
    }

    const auto gap = poll - last_poll;
    if (gap <= poll_interval + poll_interval / 2) {
        return;
    }

    EVLOG_warning << fmt::format(
        "LEM DCBM 400/600: {} ms between two live measurement polls, expected about {} ms "
        "(the live measurement request itself took {} ms). Consumers watching the measurement stream may "
        "time out.",
        std::chrono::duration_cast<std::chrono::milliseconds>(gap).count(), poll_interval.count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(request_duration).count());
}

/// \brief How long the device may repeat the same live measurement before it is reported.
///
/// The DCBM 400/600 refreshes about once per second; EvseManager gives up on a DC cable check after two
/// seconds without a measurement.
constexpr auto STALE_MEASUREMENT_WARNING_AFTER = std::chrono::seconds(2);

} // namespace

void powermeterImpl::init() {
    // Dependency injection pattern: Create the HTTP client first,
    // then move it into the controller as a constructor argument
    auto http_client =
        std::make_unique<HttpClient>(mod->config.ip_address, mod->config.port, mod->config.meter_tls_certificate,
                                     mod->config.interface, mod->config.http_connection_reuse);

    auto ntp_server_spec =
        module::main::ntp_server_spec{mod->config.ntp_server_1_ip_addr, mod->config.ntp_server_1_port,
                                      mod->config.ntp_server_2_ip_addr, mod->config.ntp_server_2_port};

    this->controller = std::make_unique<LemDCBM400600Controller>(
        std::move(http_client), std::make_unique<LemDCBMTimeSyncHelper>(ntp_server_spec),
        LemDCBM400600Controller::Conf{
            mod->config.resilience_initial_connection_retries, mod->config.resilience_initial_connection_retry_delay,
            mod->config.resilience_transaction_request_retries, mod->config.resilience_transaction_request_retry_delay,
            mod->config.cable_id, mod->config.tariff_id, mod->config.meter_timezone, mod->config.meter_dst,
            mod->config.SC, mod->config.UV, mod->config.UD, mod->config.IT, mod->config.command_timeout_ms,
            mod->config.transaction_ocmf_fetch_interval_s});

    // Validate and normalize temperature thresholds for the monitor.
    // If the error level is configured below the warning level, clamp it and log a warning.
    double warning_level_C = mod->config.temperature_warning_level_C;
    double error_level_C = mod->config.temperature_error_level_C;
    if (error_level_C < warning_level_C) {
        EVLOG_warning << "LEM DCBM 400/600: temperature_error_level_C (" << error_level_C
                      << " °C) is below temperature_warning_level_C (" << warning_level_C
                      << " °C). Clamping error level to the warning level.";
        error_level_C = warning_level_C;
    }

    this->temperature_monitor = std::make_unique<TemperatureMonitor>(
        TemperatureMonitor::Config{warning_level_C, error_level_C, mod->config.temperature_hysteresis_K,
                                   std::chrono::milliseconds(mod->config.temperature_min_time_as_valid_ms)});
}

void powermeterImpl::ready() {
    publish_configured_capabilities();

    // Start the live_measure_publisher thread, which periodically publishes the live measurements of the device
    this->live_measure_publisher_thread = std::thread([this] {
        const auto poll_interval = std::chrono::milliseconds(mod->config.poll_interval_ms);
        PollScheduler scheduler{std::chrono::steady_clock::now(), poll_interval};
        auto last_poll = std::chrono::steady_clock::time_point{};
        // The device refreshes its live measurements about once per second, so with a shorter poll
        // interval a poll returns the previous sample again. Only new measurements are published.
        std::optional<types::powermeter::Powermeter> last_published;
        auto last_new_measurement = std::chrono::steady_clock::time_point{};
        bool stale_measurement_reported = false;

        while (true) {
            try {
                if (!this->controller->is_initialized()) {
                    this->controller->init();
                    this->publish_public_key_ocmf(this->controller->get_public_key_ocmf());
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(mod->config.resilience_initial_connection_retry_delay));
                    scheduler.reset(std::chrono::steady_clock::now());
                } else {
                    std::this_thread::sleep_until(scheduler.next(std::chrono::steady_clock::now()));

                    const auto request_start = std::chrono::steady_clock::now();
                    auto powermeter_data = this->controller->get_powermeter();
                    const auto poll = std::chrono::steady_clock::now();
                    if (is_new_measurement(last_published, powermeter_data)) {
                        this->publish_powermeter(powermeter_data);
                        last_published = powermeter_data;
                        last_new_measurement = poll;
                        stale_measurement_reported = false;
                    } else {
                        EVLOG_verbose << "LEM DCBM 400/600: the device still reports the live measurement of "
                                      << powermeter_data.timestamp << ", not published again";
                        if (not stale_measurement_reported and
                            poll - last_new_measurement > STALE_MEASUREMENT_WARNING_AFTER) {
                            EVLOG_warning << fmt::format(
                                "LEM DCBM 400/600: the device has reported the same live measurement ({}) for {} "
                                "ms. Consumers watching the measurement stream may time out.",
                                powermeter_data.timestamp,
                                std::chrono::duration_cast<std::chrono::milliseconds>(poll - last_new_measurement)
                                    .count());
                            stale_measurement_reported = true;
                        }
                    }
                    warn_on_poll_gap(last_poll, poll, poll - request_start, poll_interval);
                    last_poll = poll;

                    // Refresh the fallback OCMF record only after the measurements have been published:
                    // it is a fallback for an unreachable device at transaction stop, so its round trip
                    // must not be added to the gap between two publications.
                    this->controller->update_transaction_fallback_ocmf(powermeter_data.timestamp);

                    // if the communication error is set, clear the error
                    if (this->error_state_monitor->is_error_active("powermeter/CommunicationFault",
                                                                   "Communication timed out")) {
                        // need to update LEM status since we have recovered from a communication loss
                        this->controller->update_lem_status();
                        clear_error("powermeter/CommunicationFault", "Communication timed out");
                    }

                    // Evaluate temperature thresholds
                    if (powermeter_data.temperatures.has_value() && powermeter_data.temperatures->size() >= 2) {
                        const double temp_H = powermeter_data.temperatures->at(0).temperature;
                        const double temp_L = powermeter_data.temperatures->at(1).temperature;
                        auto events = this->temperature_monitor->update(temp_H, temp_L);
                        handle_temperature_events(events, this->temperature_monitor->last_max_temperature());
                    }
                }
            } catch (LemDCBM400600Controller::DCBMUnexpectedResponseException& error) {
                EVLOG_error << "LEM DCBM 400/600: Failed to execute the powermeter ready loop due to an invalid device "
                               "response: "
                            << error.what();
            } catch (HttpClientError& client_error) {
                if (!this->error_state_monitor->is_error_active("powermeter/CommunicationFault",
                                                                "Communication timed out")) {
                    EVLOG_error << "Failed to communicate with the powermeter due to http error: "
                                << client_error.what();
                    auto error =
                        this->error_factory->create_error("powermeter/CommunicationFault", "Communication timed out",
                                                          "This error is raised due to communication timeout");
                    raise_error(error);
                }
            }
        }
    });
}

void powermeterImpl::publish_configured_capabilities() {
    if (mod->config.min_import_current_A > 0 or mod->config.min_export_current_A > 0) {
        types::powermeter::Capabilities capabilities;
        if (mod->config.min_import_current_A > 0) {
            capabilities.min_import_current_A = static_cast<float>(mod->config.min_import_current_A);
        }
        if (mod->config.min_export_current_A > 0) {
            capabilities.min_export_current_A = static_cast<float>(mod->config.min_export_current_A);
        }
        publish_capabilities(capabilities);
    }
}

types::powermeter::TransactionStartResponse
powermeterImpl::handle_start_transaction(types::powermeter::TransactionReq& value) {
    if (!this->controller->is_initialized()) {
        return types::powermeter::TransactionStartResponse{
            types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR, {}, {}, "Powermeter is not initialized"};
    }
    return this->controller->start_transaction(value);
}

types::powermeter::TransactionStopResponse powermeterImpl::handle_stop_transaction(std::string& transaction_id) {
    if (!this->controller->is_initialized()) {
        return types::powermeter::TransactionStopResponse{
            types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR, {}, {}, "Powermeter is not initialized"};
    }
    return this->controller->stop_transaction(transaction_id);
}

void powermeterImpl::handle_temperature_events(const TemperatureMonitor::Events& events, double max_temperature) {
    if (events.warning_raised) {
        EVLOG_warning << fmt::format(
            "LEM DCBM 400/600: Temperature warning raised — max temperature {:.1f} °C exceeds warning level {:.1f} °C",
            max_temperature, mod->config.temperature_warning_level_C);
        auto error =
            this->error_factory->create_error("powermeter/VendorWarning", "TemperatureWarning",
                                              fmt::format("Max temperature {:.1f} °C exceeds warning level {:.1f} °C",
                                                          max_temperature, mod->config.temperature_warning_level_C));
        raise_error(error);
    }
    if (events.warning_cleared) {
        EVLOG_info << fmt::format(
            "LEM DCBM 400/600: Temperature warning cleared — max temperature {:.1f} °C dropped below {:.1f} °C",
            max_temperature, mod->config.temperature_warning_level_C - mod->config.temperature_hysteresis_K);
        clear_error("powermeter/VendorWarning", "TemperatureWarning");
    }
    if (events.error_raised) {
        EVLOG_error << fmt::format(
            "LEM DCBM 400/600: Temperature error raised — max temperature {:.1f} °C exceeds error level {:.1f} °C",
            max_temperature, mod->config.temperature_error_level_C);
        auto error =
            this->error_factory->create_error("powermeter/VendorError", "TemperatureError",
                                              fmt::format("Max temperature {:.1f} °C exceeds error level {:.1f} °C",
                                                          max_temperature, mod->config.temperature_error_level_C));
        raise_error(error);
    }
    if (events.error_cleared) {
        EVLOG_info << fmt::format(
            "LEM DCBM 400/600: Temperature error cleared — max temperature {:.1f} °C dropped below {:.1f} °C",
            max_temperature, mod->config.temperature_error_level_C - mod->config.temperature_hysteresis_K);
        clear_error("powermeter/VendorError", "TemperatureError");
    }
}

} // namespace module::main
