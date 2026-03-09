// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/slac_defs.hpp>

namespace everest::lib::slac::messages {

const int M_SOUND_TARGET_LEN = 6;
const int SENDER_ID_LEN = defs::STATION_ID_LEN;
const int SOURCE_ID_LEN = defs::STATION_ID_LEN;
const int RESP_ID_LEN = defs::STATION_ID_LEN;
const int PEV_ID_LEN = defs::STATION_ID_LEN;
const int EVSE_ID_LEN = defs::STATION_ID_LEN;

typedef struct {
    std::uint8_t application_type;         // fixed to 0x00, indicating 'pev-evse matching'
    std::uint8_t security_type;            // fixed to 0x00, indicating 'no security'
    std::uint8_t run_id[defs::RUN_ID_LEN]; // indentifier for a matching run
    // cipher fields are missing, because we restrict to security_type = 0x00
} __attribute__((packed)) cm_slac_parm_req;

typedef struct {
    std::uint8_t m_sound_target[M_SOUND_TARGET_LEN]; // fixed to 0xFFFFFFFFFFFF
    std::uint8_t num_sounds;                         // number of expected m-sounds
    std::uint8_t timeout;                            // corresponds to TT_EVSE_match_MNBC, in units of 100ms
    std::uint8_t resp_type;                          // fixed to 0x01, indicating 'other gp station'
    std::uint8_t forwarding_sta[ETH_ALEN];           // ev host mac address
    std::uint8_t application_type;                   // fixed to 0x00, indicating 'pev-evse matching'
    std::uint8_t security_type;                      // fixed to 0x00, indicating 'no security'
    std::uint8_t run_id[defs::RUN_ID_LEN];           // matching run identifier, corresponding to the request
    // cipher field is missing, because we restrict to security_type = 0x00
} __attribute__((packed)) cm_slac_parm_cnf;

typedef struct {
    std::uint8_t application_type;         // fixed to 0x00, indicating 'pev-evse matching'
    std::uint8_t security_type;            // fixed to 0x00, indicating 'no security'
    std::uint8_t num_sounds;               // number of expected m-sounds
    std::uint8_t timeout;                  // corresponds to TT_EVSE_match_MNBC
    std::uint8_t resp_type;                // fixed to 0x01, indicating 'other gp station'
    std::uint8_t forwarding_sta[ETH_ALEN]; // ev host mac address
    std::uint8_t run_id[defs::RUN_ID_LEN]; // indentifier for a matching run
} __attribute__((packed)) cm_start_atten_char_ind;

typedef struct {
    std::uint8_t application_type;         // fixed to 0x00, indicating 'pev-evse matching'
    std::uint8_t security_type;            // fixed to 0x00, indicating 'no security'
    std::uint8_t source_address[ETH_ALEN]; // mac address of EV host, which initiates matching
    std::uint8_t run_id[defs::RUN_ID_LEN]; // indentifier for a matching run
    std::uint8_t source_id[SOURCE_ID_LEN]; // unique id of the station, that sent the m-sounds
    std::uint8_t resp_id[RESP_ID_LEN];     // unique id of the station, that is sending this message
    std::uint8_t num_sounds;               // number of sounds used for attenuation profile
    struct {
        std::uint8_t num_groups;              // number of OFDM carrier groups
        std::uint8_t aag[defs::AAG_LIST_LEN]; // AAG_1 .. AAG_N
    } __attribute__((packed)) attenuation_profile;
} __attribute__((packed)) cm_atten_char_ind;

typedef struct {
    std::uint8_t application_type;         // fixed to 0x00, indicating 'pev-evse matching'
    std::uint8_t security_type;            // fixed to 0x00, indicating 'no security'
    std::uint8_t source_address[ETH_ALEN]; // mac address of EV host, which initiates matching
    std::uint8_t run_id[defs::RUN_ID_LEN]; // indentifier for a matching run
    std::uint8_t source_id[SOURCE_ID_LEN]; // unique id of the station, that sent the m-sounds
    std::uint8_t resp_id[RESP_ID_LEN];     // unique id of the station, that is sending this message
    std::uint8_t result;                   // fixed to 0x00, indicates successful SLAC process
} __attribute__((packed)) cm_atten_char_rsp;

typedef struct {
    std::uint8_t application_type;         // fixed to 0x00, indicating 'pev-evse matching'
    std::uint8_t security_type;            // fixed to 0x00, indicating 'no security'
    std::uint8_t sender_id[SENDER_ID_LEN]; // sender id, if application_type = 0x00, it should be the pev's vin code
    std::uint8_t remaining_sound_count;    // count of remaining sound messages
    std::uint8_t run_id[defs::RUN_ID_LEN]; // indentifier for a matching run
    std::uint8_t _reserved[8]; // note: this is to pad the run_id, which is defined to be 16 bytes for this message
    std::uint8_t random[16];   // random value
} __attribute__((packed)) cm_mnbc_sound_ind;

// note: this message doesn't seem to part of hpgp, it is defined in ISO15118-3
typedef struct {
    std::uint8_t pev_mac[ETH_ALEN]; // mac address of the EV host
    std::uint8_t num_groups;        // number of OFDM carrier groups
    std::uint8_t _reserved;
    std::uint8_t aag[defs::AAG_LIST_LEN]; // list of average attenuation for each group
} __attribute__((packed)) cm_atten_profile_ind;

typedef struct {
    std::uint8_t application_type;         // fixed to 0x00, indicating 'pev-evse matching'
    std::uint8_t security_type;            // fixed to 0x00, indicating 'no security'
    std::uint16_t mvf_length;              // fixed to 0x3e = 62 bytes following
    std::uint8_t pev_id[PEV_ID_LEN];       // vin code of PEV
    std::uint8_t pev_mac[ETH_ALEN];        // mac address of the EV host
    std::uint8_t evse_id[EVSE_ID_LEN];     // EVSE id
    std::uint8_t evse_mac[ETH_ALEN];       // mac address of the EVSE
    std::uint8_t run_id[defs::RUN_ID_LEN]; // indentifier for a matching run
    std::uint8_t _reserved[8]; // note: this is to pad the run_id, which is defined to be 16 bytes for this message
} __attribute__((packed)) cm_slac_match_req;

typedef struct {
    std::uint8_t application_type;         // fixed to 0x00, indicating 'pev-evse matching'
    std::uint8_t security_type;            // fixed to 0x00, indicating 'no security'
    std::uint16_t mvf_length;              // fixed to 0x56 = 86 bytes following
    std::uint8_t pev_id[PEV_ID_LEN];       // vin code of PEV
    std::uint8_t pev_mac[ETH_ALEN];        // mac address of the EV host
    std::uint8_t evse_id[EVSE_ID_LEN];     // EVSE id
    std::uint8_t evse_mac[ETH_ALEN];       // mac address of the EVSE
    std::uint8_t run_id[defs::RUN_ID_LEN]; // indentifier for a matching run
    std::uint8_t _rerserved[8]; // note: this is to pad the run_id, which is defined to be 16 bytes for this message
    std::uint8_t nid[defs::NID_LEN]; // network id derived from the nmk
    std::uint8_t _reserved2;         // note: this is to pad the nid, which is defined to be 8 bytes for this message
    std::uint8_t nmk[defs::NMK_LEN]; // private nmk of the EVSE
} __attribute__((packed)) cm_slac_match_cnf;

typedef struct {
    std::uint8_t signal_type; // fixed to 0x00: PEV S2 toggles on control pilot line
    std::uint8_t timer;       // in the first request response exchange: should be set to 0x00
                              // in the second request response exchange: 0x00 = 100ms, 0x01 = 200ms TT_EVSE_vald_toggle
    std::uint8_t result;      // in the first request response exchange: should be set to 0x01 = ready
                              // in the second request response exchange: should be set to 0x01 = ready
} __attribute__((packed)) cm_validate_req;

typedef struct {
    std::uint8_t signal_type; // fixed to 0x00: PEV S2 toggles on control pilot line
    std::uint8_t toggle_num;  // in the first request response exchange: should be set to 0x00
                              // in the second request response exchange: number of detected BC
                              // edges during TT_EVSE_vald_toggle
    std::uint8_t result;      // 0x00 = not ready, 0x01 = ready, 0x02 = success, 0x03 = failure, 0x04 = not required
} __attribute__((packed)) cm_validate_cnf;

typedef struct {
    std::uint8_t key_type;               // fixed to 0x01, indicating NMK
    std::uint32_t my_nonce;              // fixed to 0x00000000: encrypted payload not used
    std::uint32_t your_nonce;            // fixed to 0x00000000: encrypted payload not used
    std::uint8_t pid;                    // fixed to 0x04: HLE protocol
    std::uint16_t prn;                   // fixed to 0x0000: encrypted payload not used
    std::uint8_t pmn;                    // fixed to 0x00: encrypted payload not used
    std::uint8_t cco_capability;         // CCo capability according to the station role
    std::uint8_t nid[defs::NID_LEN];     // 54 LSBs = NID, 2 MSBs = 0b00
    std::uint8_t new_eks;                // fixed to 0x01: NMK
    std::uint8_t new_key[defs::NMK_LEN]; // new NMK
} __attribute__((packed)) cm_set_key_req;

typedef struct {
    std::uint8_t result; // 0x00 = success, 0x01 = failure, 0x02 - 0xFF = reserved
    std::uint32_t my_nonce;
    std::uint32_t your_nonce;
    std::uint8_t pid;
    std::uint16_t prn;
    std::uint8_t pmn;
    std::uint8_t cco_capability;
} __attribute__((packed)) cm_set_key_cnf;

namespace qualcomm {

typedef struct {
    std::uint8_t vendor_mme[3] = {0x00, 0xb0, 0x52}; // Qualcomm Vendor MME code
} __attribute__((packed)) cm_reset_device_req;

typedef struct {
    std::uint8_t vendor_mme[3]; // Vendor MME code
    std::uint8_t success;
} __attribute__((packed)) cm_reset_device_cnf;

typedef struct {
    std::uint8_t vendor_mme[3] = {0x00, 0xb0, 0x52}; // Qualcomm Vendor MME code
} __attribute__((packed)) link_status_req;

typedef struct {
    std::uint8_t vendor_mme[3]; // Vendor MME code
    std::uint8_t reserved;
    std::uint8_t link_status;
} __attribute__((packed)) link_status_cnf;

typedef struct {
    std::uint8_t vendor_mme[3] = {0x00, 0xb0, 0x52}; // Qualcomm Vendor MME code
    std::uint32_t cookie{0x12345};                   // some cookie we will also get in the reply
    std::uint8_t report_type{0};                     // binary report
} __attribute__((packed)) op_attr_req;

typedef struct {
    std::uint8_t vendor_mme[3]; // Vendor MME code
    std::uint16_t success;      // 0x00 means success
    std::uint32_t cookie;
    std::uint8_t report_type; // should be 0x00 (binary)
    std::uint16_t size;       // should be 118, otherwise we do not know the structure
    std::uint8_t hw_platform[16];
    std::uint8_t sw_platform[16];
    std::uint32_t version_major;
    std::uint32_t version_minor;
    std::uint32_t version_pib;
    std::uint32_t version_build;
    std::uint32_t reserved;
    std::uint8_t build_date[8];
    std::uint8_t release_type[12];
    std::uint8_t sdram_type;
    std::uint8_t reserved2;
    std::uint8_t line_freq_zc;
    std::uint32_t sdram_size;
    std::uint8_t authorization_mode;
} __attribute__((packed)) op_attr_cnf;

} // namespace qualcomm

namespace lumissil {

typedef struct {
    std::uint16_t version;
    std::uint32_t reserved;
    std::uint8_t request_id;
    std::uint8_t reserved2[12];
} __attribute__((packed)) lms_header;

typedef struct {
    std::uint8_t vendor_mme[3] = {0x00, 0x16, 0xE8}; // Lumissil Vendor MME code
    lms_header lms;
    std::uint8_t mode{0}; // Normal reset
} __attribute__((packed)) nscm_reset_device_req;

typedef struct {
    std::uint8_t vendor_mme[3] = {0x00, 0x16, 0xE8}; // Lumissil Vendor MME code
    lms_header lms;
} __attribute__((packed)) nscm_get_version_req;

typedef struct {
    std::uint8_t vendor_mme[3] = {0x00, 0x16, 0xE8}; // Lumissil Vendor MME code
    lms_header lms;
    std::uint16_t version_major;
    std::uint16_t version_minor;
    std::uint16_t version_patch;
    std::uint16_t version_build;
    std::uint16_t reserved;
} __attribute__((packed)) nscm_get_version_cnf;

typedef struct {
    std::uint8_t vendor_mme[3] = {0x00, 0x16, 0xE8}; // Lumissil Vendor MME code
    lms_header lms;
} __attribute__((packed)) nscm_get_d_link_status_req;

typedef struct {
    std::uint8_t vendor_mme[3] = {0x00, 0x16, 0xE8}; // Lumissil Vendor MME code
    lms_header lms;
    std::uint8_t link_status;
} __attribute__((packed)) nscm_get_d_link_status_cnf;

// There is no CNF for this reset packet

} // namespace lumissil

} // namespace everest::lib::slac::messages
