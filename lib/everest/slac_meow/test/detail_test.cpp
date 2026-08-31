// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Direct tests for the pure helpers in detail/. The state machine suites exercise these through
// full matching sequences; these pin the edge cases without building a state machine, which is
// the split libiso15118 uses for its handle_request() helpers.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include <everest/slac/ev/detail/guards.hpp>
#include <everest/slac/evse/config.hpp>
#include <everest/slac/evse/detail/link_status.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/format.hpp>
#include <everest/slac/protocol/homeplug_message.hpp>
#include <everest/slac/protocol/messages.hpp>

using namespace everest::slac;
using namespace everest::slac;

namespace {

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

RunId make_run_id(std::uint8_t seed) {
    RunId run_id{};
    for (std::size_t i = 0; i < run_id.size(); ++i) {
        run_id[i] = static_cast<std::uint8_t>(seed + i);
    }
    return run_id;
}

messages::HomeplugMessage make_frame(void const* payload, std::size_t len, std::uint16_t mmtype,
                                     MacAddress const& src) {
    messages::HomeplugMessage msg;
    msg.setup_payload(payload, len, mmtype, defs::MMV::AV_1_1);
    msg.set_source(src);
    return msg;
}

/// Cut a frame short so its payload no longer fits, as a truncated read would.
messages::HomeplugMessage truncate(messages::HomeplugMessage msg) {
    msg.mark_received_length(messages::HOMEPLUG_PAYLOAD_OFFSET);
    return msg;
}

const MacAddress EVSE_MAC{0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
const MacAddress OTHER_MAC{0x02, 0x00, 0x00, 0x00, 0x00, 0x99};

// --- evse::accepts_set_key_cnf_success_result -------------------------------------------------

bool test_set_key_success_modes() {
    const char* name = "test_set_key_success_modes";
    using Mode = evse::SetKeyCnfSuccessMode;
    auto accepts = evse::accepts_set_key_cnf_success_result;

    return assert_true(accepts(Mode::modem_compat_0x01, 0x01), name, "modem_compat must accept 0x01") &&
           assert_true(not accepts(Mode::modem_compat_0x01, 0x00), name, "modem_compat must reject 0x00") &&
           assert_true(not accepts(Mode::modem_compat_0x01, 0x02), name, "modem_compat must reject 0x02") &&
           assert_true(accepts(Mode::hpgp_standard_0x00, 0x00), name, "hpgp must accept 0x00") &&
           assert_true(not accepts(Mode::hpgp_standard_0x00, 0x01), name, "hpgp must reject 0x01") &&
           assert_true(accepts(Mode::accept_0x00_or_0x01, 0x00), name, "dual must accept 0x00") &&
           assert_true(accepts(Mode::accept_0x00_or_0x01, 0x01), name, "dual must accept 0x01") &&
           assert_true(not accepts(Mode::accept_0x00_or_0x01, 0x02), name, "dual must reject 0x02");
}

bool test_format_nmk() {
    const char* name = "test_format_nmk";
    Nmk nmk{};
    nmk.fill(0x00);
    nmk[0] = 0xAB;
    nmk[15] = 0x0F;
    auto const text = format_nmk(nmk);

    return assert_true(text.size() == 32, name, "an NMK must render as 32 hex characters") &&
           assert_true(text.rfind("AB", 0) == 0, name, "leading byte must render uppercase, most significant first") &&
           assert_true(text.substr(30) == "0F", name, "trailing byte must keep its leading zero");
}

// --- evse::link_status ------------------------------------------------------------------------

bool test_link_check_mode_for_vendor() {
    const char* name = "test_link_check_mode_for_vendor";
    return assert_true(evse::link_check_mode_for(defs::ModemVendor::Qualcomm) == evse::LinkCheckMode::Qualcomm, name,
                       "Qualcomm modems support link supervision") &&
           assert_true(evse::link_check_mode_for(defs::ModemVendor::Lumissil) == evse::LinkCheckMode::Lumissil, name,
                       "Lumissil modems support link supervision") &&
           assert_true(evse::link_check_mode_for(defs::ModemVendor::Unknown) == evse::LinkCheckMode::None, name,
                       "an unknown modem cannot be polled") &&
           assert_true(evse::link_check_mode_for(defs::ModemVendor::VertexCom) == evse::LinkCheckMode::None, name,
                       "VertexCom is not supported for link supervision");
}

messages::HomeplugMessage make_lumissil_link_status(std::uint8_t status) {
    messages::lumissil::nscm_get_d_link_status_cnf cnf{};
    cnf.link_status = status;
    return make_frame(&cnf, sizeof(cnf), defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS_CNF, EVSE_MAC);
}

messages::HomeplugMessage make_qualcomm_link_status(std::uint8_t status) {
    messages::qualcomm::link_status_cnf cnf{};
    cnf.link_status = status;
    return make_frame(&cnf, sizeof(cnf), defs::qualcomm::MMTYPE_LINK_STATUS_CNF, EVSE_MAC);
}

bool test_link_status_up_and_down() {
    const char* name = "test_link_status_up_and_down";
    auto const lumissil_up = make_lumissil_link_status(defs::D_LINK_STATUS_LINKED);
    auto const lumissil_down = make_lumissil_link_status(0x00);
    auto const qualcomm_up = make_qualcomm_link_status(defs::D_LINK_STATUS_LINKED);
    auto const qualcomm_down = make_qualcomm_link_status(0x00);

    return assert_true(evse::is_link_up(lumissil_up, evse::LinkCheckMode::Lumissil), name,
                       "a linked Lumissil answer means the link is up") &&
           assert_true(not evse::is_link_down(lumissil_up, evse::LinkCheckMode::Lumissil), name,
                       "a linked answer is not also down") &&
           assert_true(evse::is_link_down(lumissil_down, evse::LinkCheckMode::Lumissil), name,
                       "an unlinked Lumissil answer means the link is down") &&
           assert_true(evse::is_link_up(qualcomm_up, evse::LinkCheckMode::Qualcomm), name,
                       "a linked Qualcomm answer means the link is up") &&
           assert_true(evse::is_link_down(qualcomm_down, evse::LinkCheckMode::Qualcomm), name,
                       "an unlinked Qualcomm answer means the link is down");
}

bool test_link_status_wrong_vendor_is_ignored() {
    const char* name = "test_link_status_wrong_vendor_is_ignored";
    auto const lumissil_up = make_lumissil_link_status(defs::D_LINK_STATUS_LINKED);

    return assert_true(not evse::is_link_up(lumissil_up, evse::LinkCheckMode::Qualcomm), name,
                       "a Lumissil answer must not be read as a Qualcomm one") &&
           assert_true(not evse::is_link_down(lumissil_up, evse::LinkCheckMode::Qualcomm), name,
                       "nor as a Qualcomm negative") &&
           assert_true(not evse::is_link_up(lumissil_up, evse::LinkCheckMode::None), name,
                       "mode None never reports a link");
}

bool test_truncated_link_status_is_neither_up_nor_down() {
    const char* name = "test_truncated_link_status_is_neither_up_nor_down";
    auto const short_lumissil = truncate(make_lumissil_link_status(defs::D_LINK_STATUS_LINKED));
    auto const short_qualcomm = truncate(make_qualcomm_link_status(0x00));

    return assert_true(not evse::is_link_up(short_lumissil, evse::LinkCheckMode::Lumissil), name,
                       "a truncated answer must not report the link up") &&
           assert_true(not evse::is_link_down(short_lumissil, evse::LinkCheckMode::Lumissil), name,
                       "a truncated answer must not report the link down either") &&
           assert_true(not evse::is_link_down(short_qualcomm, evse::LinkCheckMode::Qualcomm), name,
                       "same for Qualcomm: a truncated answer decides nothing");
}

// --- ev::detail guards --------------------------------------------------------------------------

messages::HomeplugMessage make_parm_cnf(RunId const& run_id) {
    messages::cm_slac_parm_cnf cnf{};
    copy_to_wire(cnf.run_id, run_id);
    return make_frame(&cnf, sizeof(cnf), defs::MMTYPE_CM_SLAC_PARAM_CNF, EVSE_MAC);
}

messages::HomeplugMessage make_match_cnf(RunId const& run_id, MacAddress const& src) {
    messages::cm_slac_match_cnf cnf{};
    copy_to_wire(cnf.run_id, run_id);
    return make_frame(&cnf, sizeof(cnf), defs::MMTYPE_CM_SLAC_MATCH_CNF, src);
}

bool test_is_slac_parm_cnf_checks_run_id() {
    const char* name = "test_is_slac_parm_cnf_checks_run_id";
    auto const run_id = make_run_id(0xA0);
    auto const other = make_run_id(0x50);
    auto const frame = make_parm_cnf(run_id);

    return assert_true(ev::is_slac_parm_cnf(frame, run_id), name, "our own run id must be accepted") &&
           assert_true(not ev::is_slac_parm_cnf(frame, other), name, "another run id must be rejected") &&
           assert_true(not ev::is_slac_parm_cnf(truncate(frame), run_id), name,
                       "a truncated confirmation must be rejected");
}

bool test_is_slac_match_cnf_source_rules() {
    const char* name = "test_is_slac_match_cnf_source_rules";
    auto const run_id = make_run_id(0xA0);
    auto const from_evse = make_match_cnf(run_id, EVSE_MAC);
    auto const from_other = make_match_cnf(run_id, OTHER_MAC);

    return assert_true(ev::is_slac_match_cnf(from_evse, run_id, EVSE_MAC), name,
                       "the EVSE we are matching with must be accepted") &&
           assert_true(not ev::is_slac_match_cnf(from_other, run_id, EVSE_MAC), name,
                       "a different source MAC must be rejected") &&
           assert_true(not ev::is_slac_match_cnf(from_evse, make_run_id(0x11), EVSE_MAC), name,
                       "a different run id must be rejected") &&
           assert_true(not ev::is_slac_match_cnf(truncate(from_evse), run_id, EVSE_MAC), name,
                       "a truncated confirmation must be rejected");
}

bool test_is_set_key_cnf_ignores_result_byte() {
    const char* name = "test_is_set_key_cnf_ignores_result_byte";
    messages::cm_set_key_cnf cnf{};
    cnf.result = 0x7F; // deliberately not a success code
    auto const frame = make_frame(&cnf, sizeof(cnf), defs::MMTYPE_CM_SET_KEY_CNF, EVSE_MAC);

    auto const wrong_type = make_frame(&cnf, sizeof(cnf), defs::MMTYPE_CM_SET_KEY_REQ, EVSE_MAC);

    return assert_true(ev::is_set_key_cnf(frame), name,
                       "the EV side accepts any well formed confirmation, result byte included") &&
           assert_true(not ev::is_set_key_cnf(wrong_type), name, "a request is not a confirmation") &&
           assert_true(not ev::is_set_key_cnf(truncate(frame)), name, "a truncated confirmation must be rejected");
}

bool test_is_atten_char_ind_for_run() {
    const char* name = "test_is_atten_char_ind_for_run";
    auto const run_id = make_run_id(0xA0);
    messages::cm_atten_char_ind ind{};
    copy_to_wire(ind.run_id, run_id);
    auto const frame = make_frame(&ind, sizeof(ind), defs::MMTYPE_CM_ATTEN_CHAR_IND, EVSE_MAC);

    return assert_true(ev::is_atten_char_ind(frame), name, "a well formed indication is recognised") &&
           assert_true(ev::is_atten_char_ind_for_run(frame, run_id), name, "our run id must be accepted") &&
           assert_true(not ev::is_atten_char_ind_for_run(frame, make_run_id(0x22)), name,
                       "another run id must be rejected") &&
           assert_true(not ev::is_atten_char_ind(truncate(frame)), name, "a truncated indication must be rejected");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 10>{
        std::make_pair("test_set_key_success_modes", test_set_key_success_modes),
        std::make_pair("test_format_nmk", test_format_nmk),
        std::make_pair("test_link_check_mode_for_vendor", test_link_check_mode_for_vendor),
        std::make_pair("test_link_status_up_and_down", test_link_status_up_and_down),
        std::make_pair("test_link_status_wrong_vendor_is_ignored", test_link_status_wrong_vendor_is_ignored),
        std::make_pair("test_truncated_link_status_is_neither_up_nor_down",
                       test_truncated_link_status_is_neither_up_nor_down),
        std::make_pair("test_is_slac_parm_cnf_checks_run_id", test_is_slac_parm_cnf_checks_run_id),
        std::make_pair("test_is_slac_match_cnf_source_rules", test_is_slac_match_cnf_source_rules),
        std::make_pair("test_is_set_key_cnf_ignores_result_byte", test_is_set_key_cnf_ignores_result_byte),
        std::make_pair("test_is_atten_char_ind_for_run", test_is_atten_char_ind_for_run),
    };

    int failed_count = 0;
    for (auto const& test : tests) {
        if (not test.second()) {
            std::printf("[FAIL] %s\n", test.first);
            ++failed_count;
        } else {
            std::printf("[PASS] %s\n", test.first);
        }
    }

    if (failed_count > 0) {
        std::printf("FAILED (%d)\n", failed_count);
        return EXIT_FAILURE;
    }
    std::printf("PASSED\n");
    return EXIT_SUCCESS;
}
