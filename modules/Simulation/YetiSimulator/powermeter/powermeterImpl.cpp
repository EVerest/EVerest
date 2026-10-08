// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "powermeterImpl.hpp"
#include "generated/types/powermeter.hpp"
#include "generated/types/units_signed.hpp"
#include <string>

namespace module::powermeter {

void powermeterImpl::init() {
}

void powermeterImpl::ready() {
    this->publish_public_key_ocmf("TESTPUBLICKEY" + std::to_string(this->mod->config.connector_id));
}

types::units_signed::SignedMeterValue format_signed_meter_value(const std::string& blob) {
    types::units_signed::SignedMeterValue value;
    value.signed_meter_data = blob;
    value.signing_method = "dummy";
    value.encoding_method = "plain";

    return value;
}

types::powermeter::TransactionStartResponse
powermeterImpl::handle_start_transaction(types::powermeter::TransactionReq& _) {
    types::powermeter::TransactionStartResponse response;
    response.status = types::powermeter::TransactionRequestStatus::OK;
    if (this->mod->config.dummy_meter_value_send_on_transaction_start) {
        response.signed_meter_value = format_signed_meter_value(this->mod->config.dummy_meter_value_blob_start);
    }
    return response;
}

types::powermeter::TransactionStopResponse powermeterImpl::handle_stop_transaction(std::string& _) {
    types::powermeter::TransactionStopResponse response;
    response.status = types::powermeter::TransactionRequestStatus::OK;
    response.start_signed_meter_value = format_signed_meter_value(this->mod->config.dummy_meter_value_blob_start);
    response.signed_meter_value = format_signed_meter_value(this->mod->config.dummy_meter_value_blob_stop);

    return response;
}

} // namespace module::powermeter
