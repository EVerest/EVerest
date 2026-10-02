// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <cstdio>
#include <iomanip>
#include <stdexcept>
#include <thread>

extern "C" {
#include <linux_nfc_api.h>
#include <linux_nfc_api_compatibility.h>
}

#include "libnfc_handler.hpp"

// NOTE (aw): access to this variable is not thread safe
// but for the current "one-shot" use, this should not be necessary
static NfcHandler* nfc_handler_instance{nullptr};
static nfcTagCallback_t nfc_callbacks;

namespace {

void set_if_configured(const char* key, const std::string& value) {
    if (!value.empty()) {
        setNxpConfigValue(key, value.c_str());
    }
}

} // namespace

NfcHandler::NfcHandler(const std::filesystem::path& config_path, const RuntimeConfig& runtime_config) {
    // we could have also used a singleton instance,
    if (nfc_handler_instance) {
        throw std::runtime_error("Only one nfc handler instance allowed");
    }

    // set path to static config file
    setConfigPath(config_path.c_str());

    // or use dynamic runtime configuration from module configuration
    if (!runtime_config.transport.empty()) {
        setNxpConfigValue("NXP_TRANSPORT", runtime_config.transport.c_str());

        // the GPIOs are in both transports important
        set_if_configured("PIN_INT", runtime_config.pin_int);
        set_if_configured("PIN_ENABLE", runtime_config.pin_enable);
        set_if_configured("PIN_FWDNLD", runtime_config.pin_fwdnld);

        // for I2C we need device and address
        if (runtime_config.transport == "0x02") {
            set_if_configured("I2C_ADDRESS", runtime_config.i2c_address);
            set_if_configured("I2C_BUS", runtime_config.device);

        // for SPI we only need the device
        } else if (runtime_config.transport == "0x03") {
            set_if_configured("SPI_BUS", runtime_config.device);
        }
    }

    InitializeLogLevel();

    if (doInitialize() != 0) {
        throw std::runtime_error("Failed to initialize libnfc_nci library");
    }

    nfc_callbacks.onTagArrival = [](nfc_tag_info_t* tag_info) { nfc_handler_instance->on_tag_arrival(tag_info); };

    nfc_callbacks.onTagDeparture = []() { nfc_handler_instance->on_tag_departure(); };

    nfc_handler_instance = this;
}

bool NfcHandler::start(Callback callback_) {
    if (this->callback) {
        return false;
    }

    this->callback = std::move(callback_);

    registerTagCallback(&nfc_callbacks);
    // enable discovery in reader-only mody, no host-routing/host-card-emulation and force a restart
    // doEnableDiscovery (int technologies_mask,
    //                    int reader_only_mode,
    //                    int enable_host_routing,
    //                    int restart)
    doEnableDiscovery(DEFAULT_NFA_TECH_MASK, 0x01, 0x0, 0x1);

    return true;
}

void NfcHandler::on_tag_arrival(void* tag_info_) {
    // unfortunately, typedefs can't be forward declared, therefor we need to do it here
    const auto tag_info = reinterpret_cast<nfc_tag_info_t*>(tag_info_);

    const auto protocol = [](tNFC_PROTOCOL const in) {
        if (in == NFA_PROTOCOL_MIFARE) {
            return Protocol::MIFARE;
        } else if (in == NFA_PROTOCOL_15693) {
            return Protocol::ISO_IEC_15693;
        }

        return Protocol::UNKNOWN;
    }(tag_info->protocol);

    if (this->callback) {
        this->callback(tag_info->uid, tag_info->uid_length, protocol);
    } else {
        // this should not happen
        // assert(false);
    }
}

void NfcHandler::on_tag_departure() {
    // handle tag departure
}

NfcHandler::~NfcHandler() {
    disableDiscovery();
    deregisterTagCallback();
    doDeinitialize();
}
