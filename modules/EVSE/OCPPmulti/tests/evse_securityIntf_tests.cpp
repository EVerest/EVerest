// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   install_ca_certificate:
//   delete_certificate:
//   update_leaf_certificate:
//   verify_certificate:
//   get_installed_certificates:
//   get_v2g_ocsp_request_data:
//   get_mo_ocsp_request_data:
//   update_ocsp_cache:
//   is_ca_certificate_installed:
//   generate_certificate_signing_request:
//   get_leaf_certificate_info:
//   get_all_valid_certificates_info:
//   get_verify_file:
//   get_verify_location:
//   get_leaf_expiry_days_count:
//   verify_file_signature:
//
// vars:
//   certificate_store_update:

// None of these are used by generic_ocpp it is used by libocpp

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

} // namespace
