/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright Pionix GmbH and Contributors to EVerest
 *
 * iso2_converter.c - ISO 15118-2 JSON/EXI converter
 *
 * Messages travel as {"V2G_Message": {"Header": ..., "Body": {"<Msg>": ...}}}.
 * A JSON object without the V2G_Message wrapper holds one bare element and is
 * coded as an ISO 15118-2 EXI fragment, which is what signature digests are
 * computed over. SignedInfo itself is signed in the xmldsig fragment grammar
 * (ISO 15118-2 Annex J), handled by iso2_xmldsig_encode/iso2_xmldsig_decode.
 *
 * Every element and attribute keeps its XSD name. hexBinary values are hex
 * strings, base64Binary values base64 strings, enumerations their XSD names,
 * and repeated elements JSON arrays.
 */

#include "cJSON.h"
#include "converters.h"
#include "json_utils.h"

#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* The largest integer that a JSON number in cJSON, a double, holds exactly. */
#define JSON_SAFE_INTEGER_MAX 9007199254740991.0

/*
 * libcbv2g writes these restricted integers as n-bit fields and truncates what
 * does not fit. The bounds are those of the fields, which the decoder also
 * delivers, so that every decoded value re-encodes unchanged. They are wider
 * than the XSD facets (-3..3, 0..100, 1..3).
 */
#define MULTIPLIER_MIN (-3)
#define MULTIPLIER_MAX 4
#define PERCENT_MAX    127
#define PHASES_MAX     4

/*
 * SAScheduleTupleID and SalesTariffID are 1..255 on the wire. libcbv2g keeps
 * them in a uint8_t, so the raw value 255 decodes as 0, and 0 encodes as 255.
 */
#define ID_8BIT_MIN 0

/* ============== JSON input helpers ============== */

static int input_error(const char* key, const char* problem) {
    set_error("ISO 15118-2: '%s' %s", key, problem);
    return CBV2G_ERROR_JSON_PARSE;
}

static int has_item(cJSON* parent, const char* key) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, key);
    return item != NULL && !cJSON_IsNull(item);
}

static int get_item(cJSON* parent, const char* key, cJSON** out) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (item == NULL || cJSON_IsNull(item)) {
        return input_error(key, "is missing");
    }
    *out = item;
    return CBV2G_SUCCESS;
}

static int get_object(cJSON* parent, const char* key, cJSON** out) {
    int rc = get_item(parent, key, out);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    if (!cJSON_IsObject(*out)) {
        return input_error(key, "must be an object");
    }
    return CBV2G_SUCCESS;
}

/* A repeated element is a JSON array of `min` to `max` entries. */
static int get_list(cJSON* parent, const char* key, size_t min, size_t max, cJSON** list, size_t* count) {
    int rc = get_item(parent, key, list);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    if (!cJSON_IsArray(*list)) {
        return input_error(key, "must be an array");
    }
    *count = (size_t)cJSON_GetArraySize(*list);
    if (*count < min || *count > max) {
        set_error("ISO 15118-2: '%s' holds %zu entries, allowed are %zu to %zu", key, *count, min, max);
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static cJSON* list_entry(cJSON* list, size_t index) {
    return cJSON_GetArrayItem(list, (int)index);
}

static int item_to_integer(cJSON* item, const char* key, double min, double max, int64_t* out) {
    if (!cJSON_IsNumber(item)) {
        return input_error(key, "must be a number");
    }
    const double value = item->valuedouble;
    if (!(value >= min && value <= max)) {
        set_error("ISO 15118-2: '%s' is %.17g, allowed are %.17g to %.17g", key, value, min, max);
        return CBV2G_ERROR_JSON_PARSE;
    }
    const int64_t integer = (int64_t)value;
    if ((double)integer != value) {
        return input_error(key, "must be an integer");
    }
    *out = integer;
    return CBV2G_SUCCESS;
}

static int read_integer(cJSON* parent, const char* key, double min, double max, int64_t* out) {
    cJSON* item = NULL;
    int rc = get_item(parent, key, &item);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    return item_to_integer(item, key, min, max, out);
}

static int read_int8(cJSON* parent, const char* key, int8_t min, int8_t max, int8_t* out) {
    int64_t value = 0;
    int rc = read_integer(parent, key, min, max, &value);
    if (rc == CBV2G_SUCCESS) {
        *out = (int8_t)value;
    }
    return rc;
}

static int read_uint8(cJSON* parent, const char* key, uint8_t min, uint8_t max, uint8_t* out) {
    int64_t value = 0;
    int rc = read_integer(parent, key, min, max, &value);
    if (rc == CBV2G_SUCCESS) {
        *out = (uint8_t)value;
    }
    return rc;
}

static int read_int16(cJSON* parent, const char* key, int16_t* out) {
    int64_t value = 0;
    int rc = read_integer(parent, key, INT16_MIN, INT16_MAX, &value);
    if (rc == CBV2G_SUCCESS) {
        *out = (int16_t)value;
    }
    return rc;
}

static int read_uint16(cJSON* parent, const char* key, uint16_t* out) {
    int64_t value = 0;
    int rc = read_integer(parent, key, 0, UINT16_MAX, &value);
    if (rc == CBV2G_SUCCESS) {
        *out = (uint16_t)value;
    }
    return rc;
}

static int read_int32(cJSON* parent, const char* key, int32_t* out) {
    int64_t value = 0;
    int rc = read_integer(parent, key, INT32_MIN, INT32_MAX, &value);
    if (rc == CBV2G_SUCCESS) {
        *out = (int32_t)value;
    }
    return rc;
}

static int read_uint32(cJSON* parent, const char* key, uint32_t* out) {
    int64_t value = 0;
    int rc = read_integer(parent, key, 0, UINT32_MAX, &value);
    if (rc == CBV2G_SUCCESS) {
        *out = (uint32_t)value;
    }
    return rc;
}

/*
 * Reads canonical decimal digits: an optional '-', no leading zeros, no "-0".
 * Fails when the magnitude exceeds `max_magnitude`.
 */
static int text_to_magnitude(const char* text, const char* key, uint64_t max_magnitude, int* negative,
                             uint64_t* magnitude) {
    *negative = text[0] == '-';
    const char* p = *negative ? text + 1 : text;
    if (*p < '0' || *p > '9' || (p[0] == '0' && p[1] != '\0') || (*negative && p[0] == '0')) {
        return input_error(key, "must be an integer");
    }
    *magnitude = 0;
    for (; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return input_error(key, "must be an integer");
        }
        const uint64_t digit = (uint64_t)(*p - '0');
        if (*magnitude > (max_magnitude - digit) / 10u) {
            return input_error(key, "is out of range");
        }
        *magnitude = *magnitude * 10u + digit;
    }
    return CBV2G_SUCCESS;
}

/*
 * The 64-bit values exceed what a JSON number keeps exactly. prepare_json turns
 * them into strings of their digits before cJSON parses them.
 */
static int read_int64(cJSON* parent, const char* key, int64_t* out) {
    cJSON* item = NULL;
    int negative = 0;
    uint64_t magnitude = 0;
    if (get_item(parent, key, &item)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    if (cJSON_IsNumber(item)) {
        return item_to_integer(item, key, -JSON_SAFE_INTEGER_MAX, JSON_SAFE_INTEGER_MAX, out);
    }
    if (!cJSON_IsString(item) ||
        text_to_magnitude(item->valuestring, key, (uint64_t)INT64_MAX + 1u, &negative, &magnitude)) {
        return input_error(key, "must be an integer");
    }
    if (!negative && magnitude > (uint64_t)INT64_MAX) {
        return input_error(key, "is out of range");
    }
    *out = negative ? (int64_t)(0u - magnitude) : (int64_t)magnitude;
    return CBV2G_SUCCESS;
}

static int read_uint64(cJSON* parent, const char* key, uint64_t* out) {
    cJSON* item = NULL;
    int negative = 0;
    if (get_item(parent, key, &item)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    if (cJSON_IsNumber(item)) {
        int64_t value = 0;
        if (item_to_integer(item, key, 0, JSON_SAFE_INTEGER_MAX, &value)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        *out = (uint64_t)value;
        return CBV2G_SUCCESS;
    }
    if (!cJSON_IsString(item) || text_to_magnitude(item->valuestring, key, UINT64_MAX, &negative, out) || negative) {
        return input_error(key, "must be a non-negative integer");
    }
    return CBV2G_SUCCESS;
}

static int read_bool(cJSON* parent, const char* key, int* out) {
    cJSON* item = NULL;
    int rc = get_item(parent, key, &item);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    if (!cJSON_IsBool(item)) {
        return input_error(key, "must be true or false");
    }
    *out = cJSON_IsTrue(item) ? 1 : 0;
    return CBV2G_SUCCESS;
}

/* `capacity` is the libcbv2g buffer size, which reserves one byte for a terminator. */
static int item_to_string(cJSON* item, const char* key, char* chars, uint16_t* len, size_t capacity) {
    if (!cJSON_IsString(item)) {
        return input_error(key, "must be a string");
    }
    const size_t n = strnlen(item->valuestring, capacity);
    if (n >= capacity) {
        set_error("ISO 15118-2: '%s' is longer than %zu characters", key, capacity - 1);
        return CBV2G_ERROR_JSON_PARSE;
    }
    memcpy(chars, item->valuestring, n);
    chars[n] = '\0';
    *len = (uint16_t)n;
    return CBV2G_SUCCESS;
}

static int read_string(cJSON* parent, const char* key, char* chars, uint16_t* len, size_t capacity) {
    cJSON* item = NULL;
    int rc = get_item(parent, key, &item);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    return item_to_string(item, key, chars, len, capacity);
}

static int is_base64_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
}

static unsigned base64_value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (unsigned)(c - 'A');
    }
    if (c >= 'a' && c <= 'z') {
        return (unsigned)(c - 'a' + 26);
    }
    if (c >= '0' && c <= '9') {
        return (unsigned)(c - '0' + 52);
    }
    return c == '+' ? 62u : 63u;
}

/* Canonical base64: padded, and the bits that padding leaves over are zero. */
static int is_valid_base64(const char* s, size_t n) {
    if (n % 4 != 0) {
        return 0;
    }
    size_t padding = 0;
    if (n >= 1 && s[n - 1] == '=') {
        padding++;
    }
    if (n >= 2 && s[n - 2] == '=') {
        padding++;
    }
    for (size_t i = 0; i < n - padding; i++) {
        if (!is_base64_char(s[i])) {
            return 0;
        }
    }
    if (padding == 1 && (base64_value(s[n - 2]) & 0x03u) != 0) {
        return 0;
    }
    if (padding == 2 && (base64_value(s[n - 3]) & 0x0Fu) != 0) {
        return 0;
    }
    return 1;
}

static int item_to_base64_bytes(cJSON* item, const char* key, uint8_t* bytes, uint16_t* len, size_t capacity) {
    if (!cJSON_IsString(item)) {
        return input_error(key, "must be a base64 string");
    }
    const size_t max_chars = ((capacity + 2) / 3) * 4;
    const size_t n = strnlen(item->valuestring, max_chars + 1);
    if (n > max_chars) {
        set_error("ISO 15118-2: '%s' holds more than %zu bytes", key, capacity);
        return CBV2G_ERROR_JSON_PARSE;
    }
    if (n == 0) {
        *len = 0;
        return CBV2G_SUCCESS;
    }
    if (!is_valid_base64(item->valuestring, n)) {
        return input_error(key, "is not valid base64");
    }
    const size_t decoded = base64_decode(item->valuestring, n, bytes, capacity);
    if (decoded == 0) {
        set_error("ISO 15118-2: '%s' holds more than %zu bytes", key, capacity);
        return CBV2G_ERROR_JSON_PARSE;
    }
    *len = (uint16_t)decoded;
    return CBV2G_SUCCESS;
}

static int read_base64(cJSON* parent, const char* key, uint8_t* bytes, uint16_t* len, size_t capacity) {
    cJSON* item = NULL;
    int rc = get_item(parent, key, &item);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    return item_to_base64_bytes(item, key, bytes, len, capacity);
}

static int read_hex(cJSON* parent, const char* key, uint8_t* bytes, uint16_t* len, size_t capacity) {
    cJSON* item = NULL;
    int rc = get_item(parent, key, &item);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    if (!cJSON_IsString(item)) {
        return input_error(key, "must be a hex string");
    }
    const size_t n = strnlen(item->valuestring, capacity * 2 + 1);
    if (n > capacity * 2) {
        set_error("ISO 15118-2: '%s' holds more than %zu bytes", key, capacity);
        return CBV2G_ERROR_JSON_PARSE;
    }
    if (n == 0) {
        *len = 0;
        return CBV2G_SUCCESS;
    }
    const size_t decoded = hex_decode(item->valuestring, n, bytes, capacity);
    if (decoded == 0) {
        return input_error(key, "is not valid hex");
    }
    *len = (uint16_t)decoded;
    return CBV2G_SUCCESS;
}

/* ============== JSON output helpers ============== */

static int output_error(const char* key) {
    set_error("ISO 15118-2: failed to generate JSON for '%s'", key);
    return CBV2G_ERROR_JSON_GENERATE;
}

static int add_object(cJSON* parent, const char* key, cJSON** out) {
    *out = cJSON_AddObjectToObject(parent, key);
    return *out != NULL ? CBV2G_SUCCESS : output_error(key);
}

static int add_array(cJSON* parent, const char* key, cJSON** out) {
    *out = cJSON_AddArrayToObject(parent, key);
    return *out != NULL ? CBV2G_SUCCESS : output_error(key);
}

static int append_object(cJSON* array, const char* key, cJSON** out) {
    *out = cJSON_CreateObject();
    if (*out == NULL || !cJSON_AddItemToArray(array, *out)) {
        cJSON_Delete(*out);
        return output_error(key);
    }
    return CBV2G_SUCCESS;
}

static int add_number(cJSON* parent, const char* key, double value) {
    return cJSON_AddNumberToObject(parent, key, value) != NULL ? CBV2G_SUCCESS : output_error(key);
}

/* 64-bit values are printed as raw digits so that they survive above 2^53. */
static int add_int64(cJSON* parent, const char* key, int64_t value) {
    char digits[24];
    snprintf(digits, sizeof(digits), "%" PRId64, value);
    return cJSON_AddRawToObject(parent, key, digits) != NULL ? CBV2G_SUCCESS : output_error(key);
}

static int add_uint64(cJSON* parent, const char* key, uint64_t value) {
    char digits[24];
    snprintf(digits, sizeof(digits), "%" PRIu64, value);
    return cJSON_AddRawToObject(parent, key, digits) != NULL ? CBV2G_SUCCESS : output_error(key);
}

static int add_bool(cJSON* parent, const char* key, int value) {
    return cJSON_AddBoolToObject(parent, key, value != 0) != NULL ? CBV2G_SUCCESS : output_error(key);
}

/* cJSON strings end at the first NUL, so a value holding NUL characters is written as an escaped raw string. */
static int add_string_with_nul(cJSON* parent, const char* key, const char* chars, uint16_t len) {
    char raw[2 + 6 * (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR) + 1];
    size_t k = 0;
    raw[k++] = '"';
    for (uint16_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)chars[i];
        if (c < 0x20 || c == '"' || c == '\\') {
            k += (size_t)snprintf(raw + k, sizeof(raw) - k, "\\u%04x", c);
        } else {
            raw[k++] = (char)c;
        }
    }
    raw[k++] = '"';
    raw[k] = '\0';
    return cJSON_AddRawToObject(parent, key, raw) != NULL ? CBV2G_SUCCESS : output_error(key);
}

static int add_string(cJSON* parent, const char* key, const char* chars, uint16_t len) {
    char buffer[EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR + 1];
    if (len >= sizeof(buffer)) {
        return output_error(key);
    }
    if (memchr(chars, '\0', len) != NULL) {
        return add_string_with_nul(parent, key, chars, len);
    }
    memcpy(buffer, chars, len);
    buffer[len] = '\0';
    return cJSON_AddStringToObject(parent, key, buffer) != NULL ? CBV2G_SUCCESS : output_error(key);
}

static int base64_text(const uint8_t* bytes, uint16_t len, char* text, size_t text_size) {
    if (len == 0) {
        text[0] = '\0';
        return CBV2G_SUCCESS;
    }
    return base64_encode(bytes, len, text, text_size) > 0 ? CBV2G_SUCCESS : CBV2G_ERROR_JSON_GENERATE;
}

static int add_base64(cJSON* parent, const char* key, const uint8_t* bytes, uint16_t len) {
    char text[((iso2_certificateType_BYTES_SIZE + 2) / 3) * 4 + 1];
    if (base64_text(bytes, len, text, sizeof(text)) != CBV2G_SUCCESS) {
        return output_error(key);
    }
    return cJSON_AddStringToObject(parent, key, text) != NULL ? CBV2G_SUCCESS : output_error(key);
}

static int append_base64(cJSON* array, const char* key, const uint8_t* bytes, uint16_t len) {
    char text[((iso2_certificateType_BYTES_SIZE + 2) / 3) * 4 + 1];
    if (base64_text(bytes, len, text, sizeof(text)) != CBV2G_SUCCESS) {
        return output_error(key);
    }
    cJSON* item = cJSON_CreateString(text);
    if (item == NULL || !cJSON_AddItemToArray(array, item)) {
        cJSON_Delete(item);
        return output_error(key);
    }
    return CBV2G_SUCCESS;
}

static int add_hex(cJSON* parent, const char* key, const uint8_t* bytes, uint16_t len) {
    char text[2 * iso2_sessionIDType_BYTES_SIZE + 1];
    if (len > iso2_sessionIDType_BYTES_SIZE) {
        return output_error(key);
    }
    text[0] = '\0';
    if (len > 0 && hex_encode(bytes, len, text, sizeof(text)) == 0) {
        return output_error(key);
    }
    for (char* c = text; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'f') {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    return cJSON_AddStringToObject(parent, key, text) != NULL ? CBV2G_SUCCESS : output_error(key);
}

/* ============== Enumerations ============== */

struct enum_name {
    int value;
    const char* name;
};

static const struct enum_name response_codes[] = {
    {iso2_responseCodeType_OK, "OK"},
    {iso2_responseCodeType_OK_NewSessionEstablished, "OK_NewSessionEstablished"},
    {iso2_responseCodeType_OK_OldSessionJoined, "OK_OldSessionJoined"},
    {iso2_responseCodeType_OK_CertificateExpiresSoon, "OK_CertificateExpiresSoon"},
    {iso2_responseCodeType_FAILED, "FAILED"},
    {iso2_responseCodeType_FAILED_SequenceError, "FAILED_SequenceError"},
    {iso2_responseCodeType_FAILED_ServiceIDInvalid, "FAILED_ServiceIDInvalid"},
    {iso2_responseCodeType_FAILED_UnknownSession, "FAILED_UnknownSession"},
    {iso2_responseCodeType_FAILED_ServiceSelectionInvalid, "FAILED_ServiceSelectionInvalid"},
    {iso2_responseCodeType_FAILED_PaymentSelectionInvalid, "FAILED_PaymentSelectionInvalid"},
    {iso2_responseCodeType_FAILED_CertificateExpired, "FAILED_CertificateExpired"},
    {iso2_responseCodeType_FAILED_SignatureError, "FAILED_SignatureError"},
    {iso2_responseCodeType_FAILED_NoCertificateAvailable, "FAILED_NoCertificateAvailable"},
    {iso2_responseCodeType_FAILED_CertChainError, "FAILED_CertChainError"},
    {iso2_responseCodeType_FAILED_ChallengeInvalid, "FAILED_ChallengeInvalid"},
    {iso2_responseCodeType_FAILED_ContractCanceled, "FAILED_ContractCanceled"},
    {iso2_responseCodeType_FAILED_WrongChargeParameter, "FAILED_WrongChargeParameter"},
    {iso2_responseCodeType_FAILED_PowerDeliveryNotApplied, "FAILED_PowerDeliveryNotApplied"},
    {iso2_responseCodeType_FAILED_TariffSelectionInvalid, "FAILED_TariffSelectionInvalid"},
    {iso2_responseCodeType_FAILED_ChargingProfileInvalid, "FAILED_ChargingProfileInvalid"},
    {iso2_responseCodeType_FAILED_MeteringSignatureNotValid, "FAILED_MeteringSignatureNotValid"},
    {iso2_responseCodeType_FAILED_NoChargeServiceSelected, "FAILED_NoChargeServiceSelected"},
    {iso2_responseCodeType_FAILED_WrongEnergyTransferMode, "FAILED_WrongEnergyTransferMode"},
    {iso2_responseCodeType_FAILED_ContactorError, "FAILED_ContactorError"},
    {iso2_responseCodeType_FAILED_CertificateNotAllowedAtThisEVSE, "FAILED_CertificateNotAllowedAtThisEVSE"},
    {iso2_responseCodeType_FAILED_CertificateRevoked, "FAILED_CertificateRevoked"},
};

static const struct enum_name unit_symbols[] = {
    {iso2_unitSymbolType_h, "h"},   {iso2_unitSymbolType_m, "m"}, {iso2_unitSymbolType_s, "s"},
    {iso2_unitSymbolType_A, "A"},   {iso2_unitSymbolType_V, "V"}, {iso2_unitSymbolType_W, "W"},
    {iso2_unitSymbolType_Wh, "Wh"},
};

static const struct enum_name cost_kinds[] = {
    {iso2_costKindType_relativePricePercentage, "relativePricePercentage"},
    {iso2_costKindType_RenewableGenerationPercentage, "RenewableGenerationPercentage"},
    {iso2_costKindType_CarbonDioxideEmission, "CarbonDioxideEmission"},
};

static const struct enum_name fault_codes[] = {
    {iso2_faultCodeType_ParsingError, "ParsingError"},
    {iso2_faultCodeType_NoTLSRootCertificatAvailable, "NoTLSRootCertificatAvailable"},
    {iso2_faultCodeType_UnknownError, "UnknownError"},
};

static const struct enum_name dc_ev_error_codes[] = {
    {iso2_DC_EVErrorCodeType_NO_ERROR, "NO_ERROR"},
    {iso2_DC_EVErrorCodeType_FAILED_RESSTemperatureInhibit, "FAILED_RESSTemperatureInhibit"},
    {iso2_DC_EVErrorCodeType_FAILED_EVShiftPosition, "FAILED_EVShiftPosition"},
    {iso2_DC_EVErrorCodeType_FAILED_ChargerConnectorLockFault, "FAILED_ChargerConnectorLockFault"},
    {iso2_DC_EVErrorCodeType_FAILED_EVRESSMalfunction, "FAILED_EVRESSMalfunction"},
    {iso2_DC_EVErrorCodeType_FAILED_ChargingCurrentdifferential, "FAILED_ChargingCurrentdifferential"},
    {iso2_DC_EVErrorCodeType_FAILED_ChargingVoltageOutOfRange, "FAILED_ChargingVoltageOutOfRange"},
    {iso2_DC_EVErrorCodeType_Reserved_A, "Reserved_A"},
    {iso2_DC_EVErrorCodeType_Reserved_B, "Reserved_B"},
    {iso2_DC_EVErrorCodeType_Reserved_C, "Reserved_C"},
    {iso2_DC_EVErrorCodeType_FAILED_ChargingSystemIncompatibility, "FAILED_ChargingSystemIncompatibility"},
    {iso2_DC_EVErrorCodeType_NoData, "NoData"},
};

static const struct enum_name evse_notifications[] = {
    {iso2_EVSENotificationType_None, "None"},
    {iso2_EVSENotificationType_StopCharging, "StopCharging"},
    {iso2_EVSENotificationType_ReNegotiation, "ReNegotiation"},
};

static const struct enum_name isolation_levels[] = {
    {iso2_isolationLevelType_Invalid, "Invalid"}, {iso2_isolationLevelType_Valid, "Valid"},
    {iso2_isolationLevelType_Warning, "Warning"}, {iso2_isolationLevelType_Fault, "Fault"},
    {iso2_isolationLevelType_No_IMD, "No_IMD"},
};

static const struct enum_name dc_evse_status_codes[] = {
    {iso2_DC_EVSEStatusCodeType_EVSE_NotReady, "EVSE_NotReady"},
    {iso2_DC_EVSEStatusCodeType_EVSE_Ready, "EVSE_Ready"},
    {iso2_DC_EVSEStatusCodeType_EVSE_Shutdown, "EVSE_Shutdown"},
    {iso2_DC_EVSEStatusCodeType_EVSE_UtilityInterruptEvent, "EVSE_UtilityInterruptEvent"},
    {iso2_DC_EVSEStatusCodeType_EVSE_IsolationMonitoringActive, "EVSE_IsolationMonitoringActive"},
    {iso2_DC_EVSEStatusCodeType_EVSE_EmergencyShutdown, "EVSE_EmergencyShutdown"},
    {iso2_DC_EVSEStatusCodeType_EVSE_Malfunction, "EVSE_Malfunction"},
    {iso2_DC_EVSEStatusCodeType_Reserved_8, "Reserved_8"},
    {iso2_DC_EVSEStatusCodeType_Reserved_9, "Reserved_9"},
    {iso2_DC_EVSEStatusCodeType_Reserved_A, "Reserved_A"},
    {iso2_DC_EVSEStatusCodeType_Reserved_B, "Reserved_B"},
    {iso2_DC_EVSEStatusCodeType_Reserved_C, "Reserved_C"},
};

static const struct enum_name payment_options[] = {
    {iso2_paymentOptionType_Contract, "Contract"},
    {iso2_paymentOptionType_ExternalPayment, "ExternalPayment"},
};

static const struct enum_name charge_progresses[] = {
    {iso2_chargeProgressType_Start, "Start"},
    {iso2_chargeProgressType_Stop, "Stop"},
    {iso2_chargeProgressType_Renegotiate, "Renegotiate"},
};

static const struct enum_name charging_sessions[] = {
    {iso2_chargingSessionType_Terminate, "Terminate"},
    {iso2_chargingSessionType_Pause, "Pause"},
};

static const struct enum_name evse_processings[] = {
    {iso2_EVSEProcessingType_Finished, "Finished"},
    {iso2_EVSEProcessingType_Ongoing, "Ongoing"},
    {iso2_EVSEProcessingType_Ongoing_WaitingForCustomerInteraction, "Ongoing_WaitingForCustomerInteraction"},
};

static const struct enum_name energy_transfer_modes[] = {
    {iso2_EnergyTransferModeType_AC_single_phase_core, "AC_single_phase_core"},
    {iso2_EnergyTransferModeType_AC_three_phase_core, "AC_three_phase_core"},
    {iso2_EnergyTransferModeType_DC_core, "DC_core"},
    {iso2_EnergyTransferModeType_DC_extended, "DC_extended"},
    {iso2_EnergyTransferModeType_DC_combo_core, "DC_combo_core"},
    {iso2_EnergyTransferModeType_DC_unique, "DC_unique"},
};

static const struct enum_name service_categories[] = {
    {iso2_serviceCategoryType_EVCharging, "EVCharging"},
    {iso2_serviceCategoryType_Internet, "Internet"},
    {iso2_serviceCategoryType_ContractCertificate, "ContractCertificate"},
    {iso2_serviceCategoryType_OtherCustom, "OtherCustom"},
};

static int item_to_enum(cJSON* item, const char* key, const struct enum_name* table, size_t count, int* out) {
    for (size_t i = 0; i < count; i++) {
        if (cJSON_IsString(item) && strcmp(item->valuestring, table[i].name) == 0) {
            *out = table[i].value;
            return CBV2G_SUCCESS;
        }
    }
    if (cJSON_IsString(item)) {
        set_error("ISO 15118-2: '%s' has unknown value '%s'", key, item->valuestring);
    } else {
        set_error("ISO 15118-2: '%s' must be a string", key);
    }
    return CBV2G_ERROR_JSON_PARSE;
}

static int read_enum(cJSON* parent, const char* key, const struct enum_name* table, size_t count, int* out) {
    cJSON* item = NULL;
    int rc = get_item(parent, key, &item);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    return item_to_enum(item, key, table, count, out);
}

static const char* enum_to_name(const struct enum_name* table, size_t count, int value) {
    for (size_t i = 0; i < count; i++) {
        if (table[i].value == value) {
            return table[i].name;
        }
    }
    return NULL;
}

static int add_enum(cJSON* parent, const char* key, const struct enum_name* table, size_t count, int value) {
    const char* name = enum_to_name(table, count, value);
    if (name == NULL) {
        set_error("ISO 15118-2: decoded '%s' has unknown value %d", key, value);
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return cJSON_AddStringToObject(parent, key, name) != NULL ? CBV2G_SUCCESS : output_error(key);
}

static int append_enum(cJSON* array, const char* key, const struct enum_name* table, size_t count, int value) {
    const char* name = enum_to_name(table, count, value);
    if (name == NULL) {
        set_error("ISO 15118-2: decoded '%s' has unknown value %d", key, value);
        return CBV2G_ERROR_JSON_GENERATE;
    }
    cJSON* item = cJSON_CreateString(name);
    if (item == NULL || !cJSON_AddItemToArray(array, item)) {
        cJSON_Delete(item);
        return output_error(key);
    }
    return CBV2G_SUCCESS;
}

static int read_response_code(cJSON* parent, iso2_responseCodeType* out) {
    int value = 0;
    int rc = read_enum(parent, "ResponseCode", response_codes, COUNT_OF(response_codes), &value);
    if (rc == CBV2G_SUCCESS) {
        *out = (iso2_responseCodeType)value;
    }
    return rc;
}

static int add_response_code(cJSON* parent, iso2_responseCodeType code) {
    return add_enum(parent, "ResponseCode", response_codes, COUNT_OF(response_codes), (int)code);
}

static int read_evse_processing(cJSON* parent, iso2_EVSEProcessingType* out) {
    int value = 0;
    int rc = read_enum(parent, "EVSEProcessing", evse_processings, COUNT_OF(evse_processings), &value);
    if (rc == CBV2G_SUCCESS) {
        *out = (iso2_EVSEProcessingType)value;
    }
    return rc;
}

static int add_evse_processing(cJSON* parent, iso2_EVSEProcessingType processing) {
    return add_enum(parent, "EVSEProcessing", evse_processings, COUNT_OF(evse_processings), (int)processing);
}

/* ============== Common types ============== */

static int read_physical_value(cJSON* parent, const char* key, struct iso2_PhysicalValueType* pv) {
    cJSON* json = NULL;
    int unit = 0;
    if (get_object(parent, key, &json) != CBV2G_SUCCESS ||
        read_int8(json, "Multiplier", MULTIPLIER_MIN, MULTIPLIER_MAX, &pv->Multiplier) ||
        read_enum(json, "Unit", unit_symbols, COUNT_OF(unit_symbols), &unit) || read_int16(json, "Value", &pv->Value)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    pv->Unit = (iso2_unitSymbolType)unit;
    return CBV2G_SUCCESS;
}

static int add_physical_value(cJSON* parent, const char* key, const struct iso2_PhysicalValueType* pv) {
    cJSON* json = NULL;
    if (add_object(parent, key, &json) || add_number(json, "Multiplier", pv->Multiplier) ||
        add_enum(json, "Unit", unit_symbols, COUNT_OF(unit_symbols), (int)pv->Unit) ||
        add_number(json, "Value", pv->Value)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int add_optional_physical_value(cJSON* parent, const char* key, const struct iso2_PhysicalValueType* pv,
                                       int is_used) {
    return is_used ? add_physical_value(parent, key, pv) : CBV2G_SUCCESS;
}

static int read_dc_ev_status(cJSON* parent, const char* key, struct iso2_DC_EVStatusType* status) {
    cJSON* json = NULL;
    int error_code = 0;
    if (get_object(parent, key, &json) || read_bool(json, "EVReady", &status->EVReady) ||
        read_enum(json, "EVErrorCode", dc_ev_error_codes, COUNT_OF(dc_ev_error_codes), &error_code) ||
        read_int8(json, "EVRESSSOC", 0, PERCENT_MAX, &status->EVRESSSOC)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    status->EVErrorCode = (iso2_DC_EVErrorCodeType)error_code;
    return CBV2G_SUCCESS;
}

static int add_dc_ev_status(cJSON* parent, const char* key, const struct iso2_DC_EVStatusType* status) {
    cJSON* json = NULL;
    if (add_object(parent, key, &json) || add_bool(json, "EVReady", status->EVReady) ||
        add_enum(json, "EVErrorCode", dc_ev_error_codes, COUNT_OF(dc_ev_error_codes), (int)status->EVErrorCode) ||
        add_number(json, "EVRESSSOC", status->EVRESSSOC)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int read_dc_evse_status(cJSON* parent, const char* key, struct iso2_DC_EVSEStatusType* status) {
    cJSON* json = NULL;
    int notification = 0;
    int status_code = 0;
    if (get_object(parent, key, &json) || read_uint16(json, "NotificationMaxDelay", &status->NotificationMaxDelay) ||
        read_enum(json, "EVSENotification", evse_notifications, COUNT_OF(evse_notifications), &notification) ||
        read_enum(json, "EVSEStatusCode", dc_evse_status_codes, COUNT_OF(dc_evse_status_codes), &status_code)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    status->EVSENotification = (iso2_EVSENotificationType)notification;
    status->EVSEStatusCode = (iso2_DC_EVSEStatusCodeType)status_code;
    status->EVSEIsolationStatus_isUsed = has_item(json, "EVSEIsolationStatus");
    if (status->EVSEIsolationStatus_isUsed) {
        int isolation = 0;
        if (read_enum(json, "EVSEIsolationStatus", isolation_levels, COUNT_OF(isolation_levels), &isolation)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        status->EVSEIsolationStatus = (iso2_isolationLevelType)isolation;
    }
    return CBV2G_SUCCESS;
}

static int add_dc_evse_status(cJSON* parent, const char* key, const struct iso2_DC_EVSEStatusType* status) {
    cJSON* json = NULL;
    if (add_object(parent, key, &json) || add_number(json, "NotificationMaxDelay", status->NotificationMaxDelay) ||
        add_enum(json, "EVSENotification", evse_notifications, COUNT_OF(evse_notifications),
                 (int)status->EVSENotification)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (status->EVSEIsolationStatus_isUsed && add_enum(json, "EVSEIsolationStatus", isolation_levels,
                                                       COUNT_OF(isolation_levels), (int)status->EVSEIsolationStatus)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (add_enum(json, "EVSEStatusCode", dc_evse_status_codes, COUNT_OF(dc_evse_status_codes),
                 (int)status->EVSEStatusCode)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int read_ac_evse_status(cJSON* parent, const char* key, struct iso2_AC_EVSEStatusType* status) {
    cJSON* json = NULL;
    int notification = 0;
    if (get_object(parent, key, &json) || read_uint16(json, "NotificationMaxDelay", &status->NotificationMaxDelay) ||
        read_enum(json, "EVSENotification", evse_notifications, COUNT_OF(evse_notifications), &notification) ||
        read_bool(json, "RCD", &status->RCD)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    status->EVSENotification = (iso2_EVSENotificationType)notification;
    return CBV2G_SUCCESS;
}

static int add_ac_evse_status(cJSON* parent, const char* key, const struct iso2_AC_EVSEStatusType* status) {
    cJSON* json = NULL;
    if (add_object(parent, key, &json) || add_number(json, "NotificationMaxDelay", status->NotificationMaxDelay) ||
        add_enum(json, "EVSENotification", evse_notifications, COUNT_OF(evse_notifications),
                 (int)status->EVSENotification) ||
        add_bool(json, "RCD", status->RCD)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

/*
 * Several responses carry exactly one of AC_EVSEStatus and DC_EVSEStatus. The
 * abstract EVSEStatus head of that substitution group is not accepted.
 */
static int read_evse_status_choice(cJSON* json, struct iso2_AC_EVSEStatusType* ac, struct iso2_DC_EVSEStatusType* dc,
                                   int* ac_used, int* dc_used) {
    *ac_used = has_item(json, "AC_EVSEStatus");
    *dc_used = has_item(json, "DC_EVSEStatus");
    if (*ac_used == *dc_used) {
        set_error("ISO 15118-2: exactly one of 'AC_EVSEStatus' and 'DC_EVSEStatus' is required");
        return CBV2G_ERROR_JSON_PARSE;
    }
    return *ac_used ? read_ac_evse_status(json, "AC_EVSEStatus", ac) : read_dc_evse_status(json, "DC_EVSEStatus", dc);
}

static int add_evse_status_choice(cJSON* json, const struct iso2_AC_EVSEStatusType* ac,
                                  const struct iso2_DC_EVSEStatusType* dc, int ac_used, int dc_used, int generic_used) {
    if (generic_used || ac_used == dc_used) {
        set_error("ISO 15118-2: decoded message carries no AC_EVSEStatus or DC_EVSEStatus");
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return ac_used ? add_ac_evse_status(json, "AC_EVSEStatus", ac) : add_dc_evse_status(json, "DC_EVSEStatus", dc);
}

static int read_meter_info(cJSON* parent, const char* key, struct iso2_MeterInfoType* info) {
    cJSON* json = NULL;
    if (get_object(parent, key, &json) || read_string(json, "MeterID", info->MeterID.characters,
                                                      &info->MeterID.charactersLen, iso2_MeterID_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    info->MeterReading_isUsed = has_item(json, "MeterReading");
    if (info->MeterReading_isUsed && read_uint64(json, "MeterReading", &info->MeterReading)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    info->SigMeterReading_isUsed = has_item(json, "SigMeterReading");
    if (info->SigMeterReading_isUsed &&
        read_base64(json, "SigMeterReading", info->SigMeterReading.bytes, &info->SigMeterReading.bytesLen,
                    iso2_sigMeterReadingType_BYTES_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    info->MeterStatus_isUsed = has_item(json, "MeterStatus");
    if (info->MeterStatus_isUsed && read_int16(json, "MeterStatus", &info->MeterStatus)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    info->TMeter_isUsed = has_item(json, "TMeter");
    if (info->TMeter_isUsed && read_int64(json, "TMeter", &info->TMeter)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int add_meter_info(cJSON* parent, const char* key, const struct iso2_MeterInfoType* info) {
    cJSON* json = NULL;
    if (add_object(parent, key, &json) ||
        add_string(json, "MeterID", info->MeterID.characters, info->MeterID.charactersLen) ||
        (info->MeterReading_isUsed && add_uint64(json, "MeterReading", info->MeterReading)) ||
        (info->SigMeterReading_isUsed &&
         add_base64(json, "SigMeterReading", info->SigMeterReading.bytes, info->SigMeterReading.bytesLen)) ||
        (info->MeterStatus_isUsed && add_number(json, "MeterStatus", info->MeterStatus)) ||
        (info->TMeter_isUsed && add_int64(json, "TMeter", info->TMeter))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int read_relative_time_interval(cJSON* parent, struct iso2_RelativeTimeIntervalType* interval) {
    cJSON* json = NULL;
    if (get_object(parent, "RelativeTimeInterval", &json) || read_uint32(json, "start", &interval->start)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    interval->duration_isUsed = has_item(json, "duration");
    if (interval->duration_isUsed && read_uint32(json, "duration", &interval->duration)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int add_relative_time_interval(cJSON* parent, const struct iso2_RelativeTimeIntervalType* interval,
                                      int is_used) {
    cJSON* json = NULL;
    if (!is_used) {
        set_error("ISO 15118-2: decoded entry carries no RelativeTimeInterval");
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (add_object(parent, "RelativeTimeInterval", &json) || add_number(json, "start", interval->start) ||
        (interval->duration_isUsed && add_number(json, "duration", interval->duration))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_sales_tariff(cJSON* json, struct iso2_SalesTariffType* tariff) {
    cJSON* entries = NULL;
    size_t entry_count = 0;
    tariff->Id_isUsed = has_item(json, "Id");
    if ((tariff->Id_isUsed &&
         read_string(json, "Id", tariff->Id.characters, &tariff->Id.charactersLen, iso2_Id_CHARACTER_SIZE)) ||
        read_uint8(json, "SalesTariffID", ID_8BIT_MIN, 255, &tariff->SalesTariffID)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    tariff->SalesTariffDescription_isUsed = has_item(json, "SalesTariffDescription");
    if (tariff->SalesTariffDescription_isUsed &&
        read_string(json, "SalesTariffDescription", tariff->SalesTariffDescription.characters,
                    &tariff->SalesTariffDescription.charactersLen, iso2_SalesTariffDescription_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    tariff->NumEPriceLevels_isUsed = has_item(json, "NumEPriceLevels");
    if (tariff->NumEPriceLevels_isUsed && read_uint8(json, "NumEPriceLevels", 0, 255, &tariff->NumEPriceLevels)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    if (get_list(json, "SalesTariffEntry", 1, iso2_SalesTariffEntryType_12_ARRAY_SIZE, &entries, &entry_count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    tariff->SalesTariffEntry.arrayLen = (uint16_t)entry_count;
    for (size_t i = 0; i < entry_count; i++) {
        cJSON* entry_json = list_entry(entries, i);
        struct iso2_SalesTariffEntryType* entry = &tariff->SalesTariffEntry.array[i];
        if (!cJSON_IsObject(entry_json)) {
            return input_error("SalesTariffEntry", "entries must be objects");
        }
        if (read_relative_time_interval(entry_json, &entry->RelativeTimeInterval)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        entry->RelativeTimeInterval_isUsed = 1;
        entry->EPriceLevel_isUsed = has_item(entry_json, "EPriceLevel");
        if (entry->EPriceLevel_isUsed && read_uint8(entry_json, "EPriceLevel", 0, 255, &entry->EPriceLevel)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        entry->ConsumptionCost.arrayLen = 0;
        if (!has_item(entry_json, "ConsumptionCost")) {
            continue;
        }
        cJSON* costs = NULL;
        size_t cost_count = 0;
        if (get_list(entry_json, "ConsumptionCost", 1, iso2_ConsumptionCostType_3_ARRAY_SIZE, &costs, &cost_count)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        entry->ConsumptionCost.arrayLen = (uint16_t)cost_count;
        for (size_t c = 0; c < cost_count; c++) {
            cJSON* consumption_json = list_entry(costs, c);
            struct iso2_ConsumptionCostType* consumption = &entry->ConsumptionCost.array[c];
            cJSON* cost_list = NULL;
            size_t n = 0;
            if (!cJSON_IsObject(consumption_json)) {
                return input_error("ConsumptionCost", "entries must be objects");
            }
            if (read_physical_value(consumption_json, "startValue", &consumption->startValue) ||
                get_list(consumption_json, "Cost", 1, iso2_CostType_3_ARRAY_SIZE, &cost_list, &n)) {
                return CBV2G_ERROR_JSON_PARSE;
            }
            consumption->Cost.arrayLen = (uint16_t)n;
            for (size_t k = 0; k < n; k++) {
                cJSON* cost_json = list_entry(cost_list, k);
                struct iso2_CostType* cost = &consumption->Cost.array[k];
                int kind = 0;
                if (!cJSON_IsObject(cost_json)) {
                    return input_error("Cost", "entries must be objects");
                }
                if (read_enum(cost_json, "costKind", cost_kinds, COUNT_OF(cost_kinds), &kind) ||
                    read_uint32(cost_json, "amount", &cost->amount)) {
                    return CBV2G_ERROR_JSON_PARSE;
                }
                cost->costKind = (iso2_costKindType)kind;
                cost->amountMultiplier_isUsed = has_item(cost_json, "amountMultiplier");
                if (cost->amountMultiplier_isUsed &&
                    read_int8(cost_json, "amountMultiplier", MULTIPLIER_MIN, MULTIPLIER_MAX, &cost->amountMultiplier)) {
                    return CBV2G_ERROR_JSON_PARSE;
                }
            }
        }
    }
    return CBV2G_SUCCESS;
}

static int sales_tariff_to_json(const struct iso2_SalesTariffType* tariff, cJSON* json) {
    cJSON* entries = NULL;
    if ((tariff->Id_isUsed && add_string(json, "Id", tariff->Id.characters, tariff->Id.charactersLen)) ||
        add_number(json, "SalesTariffID", tariff->SalesTariffID) ||
        (tariff->SalesTariffDescription_isUsed &&
         add_string(json, "SalesTariffDescription", tariff->SalesTariffDescription.characters,
                    tariff->SalesTariffDescription.charactersLen)) ||
        (tariff->NumEPriceLevels_isUsed && add_number(json, "NumEPriceLevels", tariff->NumEPriceLevels)) ||
        add_array(json, "SalesTariffEntry", &entries)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < tariff->SalesTariffEntry.arrayLen; i++) {
        const struct iso2_SalesTariffEntryType* entry = &tariff->SalesTariffEntry.array[i];
        cJSON* entry_json = NULL;
        if (append_object(entries, "SalesTariffEntry", &entry_json) ||
            add_relative_time_interval(entry_json, &entry->RelativeTimeInterval, entry->RelativeTimeInterval_isUsed) ||
            (entry->EPriceLevel_isUsed && add_number(entry_json, "EPriceLevel", entry->EPriceLevel))) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
        if (entry->ConsumptionCost.arrayLen == 0) {
            continue;
        }
        cJSON* costs = NULL;
        if (add_array(entry_json, "ConsumptionCost", &costs)) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
        for (uint16_t c = 0; c < entry->ConsumptionCost.arrayLen; c++) {
            const struct iso2_ConsumptionCostType* consumption = &entry->ConsumptionCost.array[c];
            cJSON* consumption_json = NULL;
            cJSON* cost_list = NULL;
            if (append_object(costs, "ConsumptionCost", &consumption_json) ||
                add_physical_value(consumption_json, "startValue", &consumption->startValue) ||
                add_array(consumption_json, "Cost", &cost_list)) {
                return CBV2G_ERROR_JSON_GENERATE;
            }
            for (uint16_t k = 0; k < consumption->Cost.arrayLen; k++) {
                const struct iso2_CostType* cost = &consumption->Cost.array[k];
                cJSON* cost_json = NULL;
                if (append_object(cost_list, "Cost", &cost_json) ||
                    add_enum(cost_json, "costKind", cost_kinds, COUNT_OF(cost_kinds), (int)cost->costKind) ||
                    add_number(cost_json, "amount", cost->amount) ||
                    (cost->amountMultiplier_isUsed &&
                     add_number(cost_json, "amountMultiplier", cost->amountMultiplier))) {
                    return CBV2G_ERROR_JSON_GENERATE;
                }
            }
        }
    }
    return CBV2G_SUCCESS;
}

static int read_sa_schedule_list(cJSON* parent, struct iso2_SAScheduleListType* list) {
    cJSON* json = NULL;
    cJSON* tuples = NULL;
    size_t tuple_count = 0;
    if (get_object(parent, "SAScheduleList", &json) ||
        get_list(json, "SAScheduleTuple", 1, iso2_SAScheduleTupleType_3_ARRAY_SIZE, &tuples, &tuple_count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    list->SAScheduleTuple.arrayLen = (uint16_t)tuple_count;
    for (size_t i = 0; i < tuple_count; i++) {
        cJSON* tuple_json = list_entry(tuples, i);
        struct iso2_SAScheduleTupleType* tuple = &list->SAScheduleTuple.array[i];
        cJSON* schedule = NULL;
        cJSON* entries = NULL;
        size_t entry_count = 0;
        if (!cJSON_IsObject(tuple_json)) {
            return input_error("SAScheduleTuple", "entries must be objects");
        }
        if (read_uint8(tuple_json, "SAScheduleTupleID", ID_8BIT_MIN, 255, &tuple->SAScheduleTupleID) ||
            get_object(tuple_json, "PMaxSchedule", &schedule) ||
            get_list(schedule, "PMaxScheduleEntry", 1, iso2_PMaxScheduleEntryType_12_ARRAY_SIZE, &entries,
                     &entry_count)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        tuple->PMaxSchedule.PMaxScheduleEntry.arrayLen = (uint16_t)entry_count;
        for (size_t j = 0; j < entry_count; j++) {
            cJSON* entry_json = list_entry(entries, j);
            struct iso2_PMaxScheduleEntryType* entry = &tuple->PMaxSchedule.PMaxScheduleEntry.array[j];
            if (!cJSON_IsObject(entry_json)) {
                return input_error("PMaxScheduleEntry", "entries must be objects");
            }
            if (read_relative_time_interval(entry_json, &entry->RelativeTimeInterval) ||
                read_physical_value(entry_json, "PMax", &entry->PMax)) {
                return CBV2G_ERROR_JSON_PARSE;
            }
            entry->RelativeTimeInterval_isUsed = 1;
        }
        tuple->SalesTariff_isUsed = has_item(tuple_json, "SalesTariff");
        if (tuple->SalesTariff_isUsed) {
            cJSON* tariff = NULL;
            if (get_object(tuple_json, "SalesTariff", &tariff) || json_to_sales_tariff(tariff, &tuple->SalesTariff)) {
                return CBV2G_ERROR_JSON_PARSE;
            }
        }
    }
    return CBV2G_SUCCESS;
}

static int add_sa_schedule_list(cJSON* parent, const struct iso2_SAScheduleListType* list) {
    cJSON* json = NULL;
    cJSON* tuples = NULL;
    if (add_object(parent, "SAScheduleList", &json) || add_array(json, "SAScheduleTuple", &tuples)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < list->SAScheduleTuple.arrayLen; i++) {
        const struct iso2_SAScheduleTupleType* tuple = &list->SAScheduleTuple.array[i];
        cJSON* tuple_json = NULL;
        cJSON* schedule = NULL;
        cJSON* entries = NULL;
        if (append_object(tuples, "SAScheduleTuple", &tuple_json) ||
            add_number(tuple_json, "SAScheduleTupleID", tuple->SAScheduleTupleID) ||
            add_object(tuple_json, "PMaxSchedule", &schedule) || add_array(schedule, "PMaxScheduleEntry", &entries)) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
        for (uint16_t j = 0; j < tuple->PMaxSchedule.PMaxScheduleEntry.arrayLen; j++) {
            const struct iso2_PMaxScheduleEntryType* entry = &tuple->PMaxSchedule.PMaxScheduleEntry.array[j];
            cJSON* entry_json = NULL;
            if (append_object(entries, "PMaxScheduleEntry", &entry_json) ||
                add_relative_time_interval(entry_json, &entry->RelativeTimeInterval,
                                           entry->RelativeTimeInterval_isUsed) ||
                add_physical_value(entry_json, "PMax", &entry->PMax)) {
                return CBV2G_ERROR_JSON_GENERATE;
            }
        }
        if (tuple->SalesTariff_isUsed) {
            cJSON* tariff = NULL;
            if (add_object(tuple_json, "SalesTariff", &tariff) || sales_tariff_to_json(&tuple->SalesTariff, tariff)) {
                return CBV2G_ERROR_JSON_GENERATE;
            }
        }
    }
    return CBV2G_SUCCESS;
}

static int read_charging_profile(cJSON* parent, struct iso2_ChargingProfileType* profile) {
    cJSON* json = NULL;
    cJSON* entries = NULL;
    size_t count = 0;
    if (get_object(parent, "ChargingProfile", &json) ||
        get_list(json, "ProfileEntry", 1, iso2_ProfileEntryType_24_ARRAY_SIZE, &entries, &count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    profile->ProfileEntry.arrayLen = (uint16_t)count;
    for (size_t i = 0; i < count; i++) {
        cJSON* entry_json = list_entry(entries, i);
        struct iso2_ProfileEntryType* entry = &profile->ProfileEntry.array[i];
        if (!cJSON_IsObject(entry_json)) {
            return input_error("ProfileEntry", "entries must be objects");
        }
        if (read_uint32(entry_json, "ChargingProfileEntryStart", &entry->ChargingProfileEntryStart) ||
            read_physical_value(entry_json, "ChargingProfileEntryMaxPower", &entry->ChargingProfileEntryMaxPower)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        entry->ChargingProfileEntryMaxNumberOfPhasesInUse_isUsed =
            has_item(entry_json, "ChargingProfileEntryMaxNumberOfPhasesInUse");
        if (entry->ChargingProfileEntryMaxNumberOfPhasesInUse_isUsed &&
            read_int8(entry_json, "ChargingProfileEntryMaxNumberOfPhasesInUse", 1, PHASES_MAX,
                      &entry->ChargingProfileEntryMaxNumberOfPhasesInUse)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
    }
    return CBV2G_SUCCESS;
}

static int add_charging_profile(cJSON* parent, const struct iso2_ChargingProfileType* profile) {
    cJSON* json = NULL;
    cJSON* entries = NULL;
    if (add_object(parent, "ChargingProfile", &json) || add_array(json, "ProfileEntry", &entries)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < profile->ProfileEntry.arrayLen; i++) {
        const struct iso2_ProfileEntryType* entry = &profile->ProfileEntry.array[i];
        cJSON* entry_json = NULL;
        if (append_object(entries, "ProfileEntry", &entry_json) ||
            add_number(entry_json, "ChargingProfileEntryStart", entry->ChargingProfileEntryStart) ||
            add_physical_value(entry_json, "ChargingProfileEntryMaxPower", &entry->ChargingProfileEntryMaxPower) ||
            (entry->ChargingProfileEntryMaxNumberOfPhasesInUse_isUsed &&
             add_number(entry_json, "ChargingProfileEntryMaxNumberOfPhasesInUse",
                        entry->ChargingProfileEntryMaxNumberOfPhasesInUse))) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
    }
    return CBV2G_SUCCESS;
}

static int read_service_attributes(cJSON* json, uint16_t* service_id, char* name, uint16_t* name_len, int* name_used,
                                   iso2_serviceCategoryType* category, char* scope, uint16_t* scope_len,
                                   int* scope_used, int* free_service) {
    int category_value = 0;
    if (read_uint16(json, "ServiceID", service_id) ||
        read_enum(json, "ServiceCategory", service_categories, COUNT_OF(service_categories), &category_value) ||
        read_bool(json, "FreeService", free_service)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    *category = (iso2_serviceCategoryType)category_value;
    *name_used = has_item(json, "ServiceName");
    if (*name_used && read_string(json, "ServiceName", name, name_len, iso2_ServiceName_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    *scope_used = has_item(json, "ServiceScope");
    if (*scope_used && read_string(json, "ServiceScope", scope, scope_len, iso2_ServiceScope_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int add_service_attributes(cJSON* json, uint16_t service_id, const char* name, uint16_t name_len, int name_used,
                                  iso2_serviceCategoryType category, const char* scope, uint16_t scope_len,
                                  int scope_used, int free_service) {
    if (add_number(json, "ServiceID", service_id) || (name_used && add_string(json, "ServiceName", name, name_len)) ||
        add_enum(json, "ServiceCategory", service_categories, COUNT_OF(service_categories), (int)category) ||
        (scope_used && add_string(json, "ServiceScope", scope, scope_len)) ||
        add_bool(json, "FreeService", free_service)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_certificate_chain(cJSON* json, struct iso2_CertificateChainType* chain) {
    chain->Id_isUsed = has_item(json, "Id");
    if ((chain->Id_isUsed &&
         read_string(json, "Id", chain->Id.characters, &chain->Id.charactersLen, iso2_Id_CHARACTER_SIZE)) ||
        read_base64(json, "Certificate", chain->Certificate.bytes, &chain->Certificate.bytesLen,
                    iso2_certificateType_BYTES_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    chain->SubCertificates_isUsed = has_item(json, "SubCertificates");
    if (!chain->SubCertificates_isUsed) {
        return CBV2G_SUCCESS;
    }
    cJSON* sub = NULL;
    cJSON* certificates = NULL;
    size_t count = 0;
    if (get_object(json, "SubCertificates", &sub) ||
        get_list(sub, "Certificate", 1, iso2_certificateType_4_ARRAY_SIZE, &certificates, &count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    chain->SubCertificates.Certificate.arrayLen = (uint16_t)count;
    for (size_t i = 0; i < count; i++) {
        if (item_to_base64_bytes(
                list_entry(certificates, i), "Certificate", chain->SubCertificates.Certificate.array[i].bytes,
                &chain->SubCertificates.Certificate.array[i].bytesLen, iso2_certificateType_BYTES_SIZE)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
    }
    return CBV2G_SUCCESS;
}

static int read_certificate_chain(cJSON* parent, const char* key, struct iso2_CertificateChainType* chain) {
    cJSON* json = NULL;
    if (get_object(parent, key, &json)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return json_to_certificate_chain(json, chain);
}

static int certificate_chain_to_json(const struct iso2_CertificateChainType* chain, cJSON* json) {
    if ((chain->Id_isUsed && add_string(json, "Id", chain->Id.characters, chain->Id.charactersLen)) ||
        add_base64(json, "Certificate", chain->Certificate.bytes, chain->Certificate.bytesLen)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (!chain->SubCertificates_isUsed) {
        return CBV2G_SUCCESS;
    }
    cJSON* sub = NULL;
    cJSON* certificates = NULL;
    if (add_object(json, "SubCertificates", &sub) || add_array(sub, "Certificate", &certificates)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < chain->SubCertificates.Certificate.arrayLen; i++) {
        if (append_base64(certificates, "Certificate", chain->SubCertificates.Certificate.array[i].bytes,
                          chain->SubCertificates.Certificate.array[i].bytesLen)) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
    }
    return CBV2G_SUCCESS;
}

static int add_certificate_chain(cJSON* parent, const char* key, const struct iso2_CertificateChainType* chain) {
    cJSON* json = NULL;
    if (add_object(parent, key, &json)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return certificate_chain_to_json(chain, json);
}

/* ============== Charge parameters ============== */

static int read_ac_ev_charge_parameter(cJSON* parent, struct iso2_AC_EVChargeParameterType* param) {
    cJSON* json = NULL;
    if (get_object(parent, "AC_EVChargeParameter", &json) || read_physical_value(json, "EAmount", &param->EAmount) ||
        read_physical_value(json, "EVMaxVoltage", &param->EVMaxVoltage) ||
        read_physical_value(json, "EVMaxCurrent", &param->EVMaxCurrent) ||
        read_physical_value(json, "EVMinCurrent", &param->EVMinCurrent)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    param->DepartureTime_isUsed = has_item(json, "DepartureTime");
    if (param->DepartureTime_isUsed && read_uint32(json, "DepartureTime", &param->DepartureTime)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int add_ac_ev_charge_parameter(cJSON* parent, const struct iso2_AC_EVChargeParameterType* param) {
    cJSON* json = NULL;
    if (add_object(parent, "AC_EVChargeParameter", &json) ||
        (param->DepartureTime_isUsed && add_number(json, "DepartureTime", param->DepartureTime)) ||
        add_physical_value(json, "EAmount", &param->EAmount) ||
        add_physical_value(json, "EVMaxVoltage", &param->EVMaxVoltage) ||
        add_physical_value(json, "EVMaxCurrent", &param->EVMaxCurrent) ||
        add_physical_value(json, "EVMinCurrent", &param->EVMinCurrent)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int read_dc_ev_charge_parameter(cJSON* parent, struct iso2_DC_EVChargeParameterType* param) {
    cJSON* json = NULL;
    if (get_object(parent, "DC_EVChargeParameter", &json) ||
        read_dc_ev_status(json, "DC_EVStatus", &param->DC_EVStatus) ||
        read_physical_value(json, "EVMaximumCurrentLimit", &param->EVMaximumCurrentLimit) ||
        read_physical_value(json, "EVMaximumVoltageLimit", &param->EVMaximumVoltageLimit)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    param->DepartureTime_isUsed = has_item(json, "DepartureTime");
    param->EVMaximumPowerLimit_isUsed = has_item(json, "EVMaximumPowerLimit");
    param->EVEnergyCapacity_isUsed = has_item(json, "EVEnergyCapacity");
    param->EVEnergyRequest_isUsed = has_item(json, "EVEnergyRequest");
    param->FullSOC_isUsed = has_item(json, "FullSOC");
    param->BulkSOC_isUsed = has_item(json, "BulkSOC");
    if ((param->DepartureTime_isUsed && read_uint32(json, "DepartureTime", &param->DepartureTime)) ||
        (param->EVMaximumPowerLimit_isUsed &&
         read_physical_value(json, "EVMaximumPowerLimit", &param->EVMaximumPowerLimit)) ||
        (param->EVEnergyCapacity_isUsed && read_physical_value(json, "EVEnergyCapacity", &param->EVEnergyCapacity)) ||
        (param->EVEnergyRequest_isUsed && read_physical_value(json, "EVEnergyRequest", &param->EVEnergyRequest)) ||
        (param->FullSOC_isUsed && read_int8(json, "FullSOC", 0, PERCENT_MAX, &param->FullSOC)) ||
        (param->BulkSOC_isUsed && read_int8(json, "BulkSOC", 0, PERCENT_MAX, &param->BulkSOC))) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int add_dc_ev_charge_parameter(cJSON* parent, const struct iso2_DC_EVChargeParameterType* param) {
    cJSON* json = NULL;
    if (add_object(parent, "DC_EVChargeParameter", &json) ||
        (param->DepartureTime_isUsed && add_number(json, "DepartureTime", param->DepartureTime)) ||
        add_dc_ev_status(json, "DC_EVStatus", &param->DC_EVStatus) ||
        add_physical_value(json, "EVMaximumCurrentLimit", &param->EVMaximumCurrentLimit) ||
        add_optional_physical_value(json, "EVMaximumPowerLimit", &param->EVMaximumPowerLimit,
                                    param->EVMaximumPowerLimit_isUsed) ||
        add_physical_value(json, "EVMaximumVoltageLimit", &param->EVMaximumVoltageLimit) ||
        add_optional_physical_value(json, "EVEnergyCapacity", &param->EVEnergyCapacity,
                                    param->EVEnergyCapacity_isUsed) ||
        add_optional_physical_value(json, "EVEnergyRequest", &param->EVEnergyRequest, param->EVEnergyRequest_isUsed) ||
        (param->FullSOC_isUsed && add_number(json, "FullSOC", param->FullSOC)) ||
        (param->BulkSOC_isUsed && add_number(json, "BulkSOC", param->BulkSOC))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int read_ac_evse_charge_parameter(cJSON* parent, struct iso2_AC_EVSEChargeParameterType* param) {
    cJSON* json = NULL;
    if (get_object(parent, "AC_EVSEChargeParameter", &json) ||
        read_ac_evse_status(json, "AC_EVSEStatus", &param->AC_EVSEStatus) ||
        read_physical_value(json, "EVSENominalVoltage", &param->EVSENominalVoltage) ||
        read_physical_value(json, "EVSEMaxCurrent", &param->EVSEMaxCurrent)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int add_ac_evse_charge_parameter(cJSON* parent, const struct iso2_AC_EVSEChargeParameterType* param) {
    cJSON* json = NULL;
    if (add_object(parent, "AC_EVSEChargeParameter", &json) ||
        add_ac_evse_status(json, "AC_EVSEStatus", &param->AC_EVSEStatus) ||
        add_physical_value(json, "EVSENominalVoltage", &param->EVSENominalVoltage) ||
        add_physical_value(json, "EVSEMaxCurrent", &param->EVSEMaxCurrent)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int read_dc_evse_charge_parameter(cJSON* parent, struct iso2_DC_EVSEChargeParameterType* param) {
    cJSON* json = NULL;
    if (get_object(parent, "DC_EVSEChargeParameter", &json) ||
        read_dc_evse_status(json, "DC_EVSEStatus", &param->DC_EVSEStatus) ||
        read_physical_value(json, "EVSEMaximumCurrentLimit", &param->EVSEMaximumCurrentLimit) ||
        read_physical_value(json, "EVSEMaximumPowerLimit", &param->EVSEMaximumPowerLimit) ||
        read_physical_value(json, "EVSEMaximumVoltageLimit", &param->EVSEMaximumVoltageLimit) ||
        read_physical_value(json, "EVSEMinimumCurrentLimit", &param->EVSEMinimumCurrentLimit) ||
        read_physical_value(json, "EVSEMinimumVoltageLimit", &param->EVSEMinimumVoltageLimit) ||
        read_physical_value(json, "EVSEPeakCurrentRipple", &param->EVSEPeakCurrentRipple)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    param->EVSECurrentRegulationTolerance_isUsed = has_item(json, "EVSECurrentRegulationTolerance");
    param->EVSEEnergyToBeDelivered_isUsed = has_item(json, "EVSEEnergyToBeDelivered");
    if ((param->EVSECurrentRegulationTolerance_isUsed &&
         read_physical_value(json, "EVSECurrentRegulationTolerance", &param->EVSECurrentRegulationTolerance)) ||
        (param->EVSEEnergyToBeDelivered_isUsed &&
         read_physical_value(json, "EVSEEnergyToBeDelivered", &param->EVSEEnergyToBeDelivered))) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int add_dc_evse_charge_parameter(cJSON* parent, const struct iso2_DC_EVSEChargeParameterType* param) {
    cJSON* json = NULL;
    if (add_object(parent, "DC_EVSEChargeParameter", &json) ||
        add_dc_evse_status(json, "DC_EVSEStatus", &param->DC_EVSEStatus) ||
        add_physical_value(json, "EVSEMaximumCurrentLimit", &param->EVSEMaximumCurrentLimit) ||
        add_physical_value(json, "EVSEMaximumPowerLimit", &param->EVSEMaximumPowerLimit) ||
        add_physical_value(json, "EVSEMaximumVoltageLimit", &param->EVSEMaximumVoltageLimit) ||
        add_physical_value(json, "EVSEMinimumCurrentLimit", &param->EVSEMinimumCurrentLimit) ||
        add_physical_value(json, "EVSEMinimumVoltageLimit", &param->EVSEMinimumVoltageLimit) ||
        add_optional_physical_value(json, "EVSECurrentRegulationTolerance", &param->EVSECurrentRegulationTolerance,
                                    param->EVSECurrentRegulationTolerance_isUsed) ||
        add_physical_value(json, "EVSEPeakCurrentRipple", &param->EVSEPeakCurrentRipple) ||
        add_optional_physical_value(json, "EVSEEnergyToBeDelivered", &param->EVSEEnergyToBeDelivered,
                                    param->EVSEEnergyToBeDelivered_isUsed)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

/* ============== Session setup and service selection ============== */

static int json_to_session_setup_req(cJSON* json, struct iso2_SessionSetupReqType* msg) {
    return read_hex(json, "EVCCID", msg->EVCCID.bytes, &msg->EVCCID.bytesLen, iso2_evccIDType_BYTES_SIZE);
}

static int session_setup_req_to_json(const struct iso2_SessionSetupReqType* msg, cJSON* json) {
    return add_hex(json, "EVCCID", msg->EVCCID.bytes, msg->EVCCID.bytesLen);
}

static int json_to_session_setup_res(cJSON* json, struct iso2_SessionSetupResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) ||
        read_string(json, "EVSEID", msg->EVSEID.characters, &msg->EVSEID.charactersLen, iso2_EVSEID_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->EVSETimeStamp_isUsed = has_item(json, "EVSETimeStamp");
    if (msg->EVSETimeStamp_isUsed && read_int64(json, "EVSETimeStamp", &msg->EVSETimeStamp)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int session_setup_res_to_json(const struct iso2_SessionSetupResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) ||
        add_string(json, "EVSEID", msg->EVSEID.characters, msg->EVSEID.charactersLen) ||
        (msg->EVSETimeStamp_isUsed && add_int64(json, "EVSETimeStamp", msg->EVSETimeStamp))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_service_discovery_req(cJSON* json, struct iso2_ServiceDiscoveryReqType* msg) {
    msg->ServiceScope_isUsed = has_item(json, "ServiceScope");
    if (msg->ServiceScope_isUsed && read_string(json, "ServiceScope", msg->ServiceScope.characters,
                                                &msg->ServiceScope.charactersLen, iso2_ServiceScope_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->ServiceCategory_isUsed = has_item(json, "ServiceCategory");
    if (msg->ServiceCategory_isUsed) {
        int category = 0;
        if (read_enum(json, "ServiceCategory", service_categories, COUNT_OF(service_categories), &category)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        msg->ServiceCategory = (iso2_serviceCategoryType)category;
    }
    return CBV2G_SUCCESS;
}

static int service_discovery_req_to_json(const struct iso2_ServiceDiscoveryReqType* msg, cJSON* json) {
    if ((msg->ServiceScope_isUsed &&
         add_string(json, "ServiceScope", msg->ServiceScope.characters, msg->ServiceScope.charactersLen)) ||
        (msg->ServiceCategory_isUsed && add_enum(json, "ServiceCategory", service_categories,
                                                 COUNT_OF(service_categories), (int)msg->ServiceCategory))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_service_discovery_res(cJSON* json, struct iso2_ServiceDiscoveryResType* msg) {
    cJSON* payment_list = NULL;
    cJSON* options = NULL;
    size_t option_count = 0;
    cJSON* charge_service = NULL;
    cJSON* supported = NULL;
    cJSON* modes = NULL;
    size_t mode_count = 0;
    int name_used = 0;
    int scope_used = 0;
    struct iso2_ChargeServiceType* service = &msg->ChargeService;

    if (read_response_code(json, &msg->ResponseCode) || get_object(json, "PaymentOptionList", &payment_list) ||
        get_list(payment_list, "PaymentOption", 1, iso2_paymentOptionType_2_ARRAY_SIZE, &options, &option_count) ||
        get_object(json, "ChargeService", &charge_service) ||
        read_service_attributes(charge_service, &service->ServiceID, service->ServiceName.characters,
                                &service->ServiceName.charactersLen, &name_used, &service->ServiceCategory,
                                service->ServiceScope.characters, &service->ServiceScope.charactersLen, &scope_used,
                                &service->FreeService) ||
        get_object(charge_service, "SupportedEnergyTransferMode", &supported) ||
        get_list(supported, "EnergyTransferMode", 1, iso2_EnergyTransferModeType_6_ARRAY_SIZE, &modes, &mode_count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    service->ServiceName_isUsed = name_used;
    service->ServiceScope_isUsed = scope_used;

    msg->PaymentOptionList.PaymentOption.arrayLen = (uint16_t)option_count;
    for (size_t i = 0; i < option_count; i++) {
        int option = 0;
        if (item_to_enum(list_entry(options, i), "PaymentOption", payment_options, COUNT_OF(payment_options),
                         &option)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        msg->PaymentOptionList.PaymentOption.array[i] = (iso2_paymentOptionType)option;
    }
    service->SupportedEnergyTransferMode.EnergyTransferMode.arrayLen = (uint16_t)mode_count;
    for (size_t i = 0; i < mode_count; i++) {
        int mode = 0;
        if (item_to_enum(list_entry(modes, i), "EnergyTransferMode", energy_transfer_modes,
                         COUNT_OF(energy_transfer_modes), &mode)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        service->SupportedEnergyTransferMode.EnergyTransferMode.array[i] = (iso2_EnergyTransferModeType)mode;
    }

    msg->ServiceList_isUsed = has_item(json, "ServiceList");
    if (!msg->ServiceList_isUsed) {
        return CBV2G_SUCCESS;
    }
    cJSON* service_list = NULL;
    cJSON* services = NULL;
    size_t service_count = 0;
    if (get_object(json, "ServiceList", &service_list) ||
        get_list(service_list, "Service", 1, iso2_ServiceType_8_ARRAY_SIZE, &services, &service_count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->ServiceList.Service.arrayLen = (uint16_t)service_count;
    for (size_t i = 0; i < service_count; i++) {
        cJSON* entry = list_entry(services, i);
        struct iso2_ServiceType* s = &msg->ServiceList.Service.array[i];
        if (!cJSON_IsObject(entry)) {
            return input_error("Service", "entries must be objects");
        }
        if (read_service_attributes(entry, &s->ServiceID, s->ServiceName.characters, &s->ServiceName.charactersLen,
                                    &name_used, &s->ServiceCategory, s->ServiceScope.characters,
                                    &s->ServiceScope.charactersLen, &scope_used, &s->FreeService)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        s->ServiceName_isUsed = name_used;
        s->ServiceScope_isUsed = scope_used;
    }
    return CBV2G_SUCCESS;
}

static int service_discovery_res_to_json(const struct iso2_ServiceDiscoveryResType* msg, cJSON* json) {
    const struct iso2_ChargeServiceType* service = &msg->ChargeService;
    cJSON* payment_list = NULL;
    cJSON* options = NULL;
    cJSON* charge_service = NULL;
    cJSON* supported = NULL;
    cJSON* modes = NULL;
    if (add_response_code(json, msg->ResponseCode) || add_object(json, "PaymentOptionList", &payment_list) ||
        add_array(payment_list, "PaymentOption", &options)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < msg->PaymentOptionList.PaymentOption.arrayLen; i++) {
        if (append_enum(options, "PaymentOption", payment_options, COUNT_OF(payment_options),
                        (int)msg->PaymentOptionList.PaymentOption.array[i])) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
    }
    if (add_object(json, "ChargeService", &charge_service) ||
        add_service_attributes(
            charge_service, service->ServiceID, service->ServiceName.characters, service->ServiceName.charactersLen,
            service->ServiceName_isUsed, service->ServiceCategory, service->ServiceScope.characters,
            service->ServiceScope.charactersLen, service->ServiceScope_isUsed, service->FreeService) ||
        add_object(charge_service, "SupportedEnergyTransferMode", &supported) ||
        add_array(supported, "EnergyTransferMode", &modes)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < service->SupportedEnergyTransferMode.EnergyTransferMode.arrayLen; i++) {
        if (append_enum(modes, "EnergyTransferMode", energy_transfer_modes, COUNT_OF(energy_transfer_modes),
                        (int)service->SupportedEnergyTransferMode.EnergyTransferMode.array[i])) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
    }
    if (!msg->ServiceList_isUsed) {
        return CBV2G_SUCCESS;
    }
    cJSON* service_list = NULL;
    cJSON* services = NULL;
    if (add_object(json, "ServiceList", &service_list) || add_array(service_list, "Service", &services)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < msg->ServiceList.Service.arrayLen; i++) {
        const struct iso2_ServiceType* s = &msg->ServiceList.Service.array[i];
        cJSON* entry = NULL;
        if (append_object(services, "Service", &entry) ||
            add_service_attributes(entry, s->ServiceID, s->ServiceName.characters, s->ServiceName.charactersLen,
                                   s->ServiceName_isUsed, s->ServiceCategory, s->ServiceScope.characters,
                                   s->ServiceScope.charactersLen, s->ServiceScope_isUsed, s->FreeService)) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
    }
    return CBV2G_SUCCESS;
}

static int json_to_service_detail_req(cJSON* json, struct iso2_ServiceDetailReqType* msg) {
    return read_uint16(json, "ServiceID", &msg->ServiceID);
}

static int service_detail_req_to_json(const struct iso2_ServiceDetailReqType* msg, cJSON* json) {
    return add_number(json, "ServiceID", msg->ServiceID);
}

static const char* const parameter_value_keys[] = {"boolValue", "byteValue",     "shortValue",
                                                   "intValue",  "physicalValue", "stringValue"};

static int json_to_parameter(cJSON* json, struct iso2_ParameterType* parameter) {
    size_t present = 0;
    for (size_t i = 0; i < COUNT_OF(parameter_value_keys); i++) {
        present += has_item(json, parameter_value_keys[i]) ? 1 : 0;
    }
    if (present != 1) {
        set_error("ISO 15118-2: a Parameter needs exactly one of boolValue, byteValue, shortValue, intValue, "
                  "physicalValue and stringValue");
        return CBV2G_ERROR_JSON_PARSE;
    }
    if (read_string(json, "Name", parameter->Name.characters, &parameter->Name.charactersLen,
                    iso2_Name_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    parameter->boolValue_isUsed = has_item(json, "boolValue");
    parameter->byteValue_isUsed = has_item(json, "byteValue");
    parameter->shortValue_isUsed = has_item(json, "shortValue");
    parameter->intValue_isUsed = has_item(json, "intValue");
    parameter->physicalValue_isUsed = has_item(json, "physicalValue");
    parameter->stringValue_isUsed = has_item(json, "stringValue");
    if ((parameter->boolValue_isUsed && read_bool(json, "boolValue", &parameter->boolValue)) ||
        (parameter->byteValue_isUsed && read_int8(json, "byteValue", INT8_MIN, INT8_MAX, &parameter->byteValue)) ||
        (parameter->shortValue_isUsed && read_int16(json, "shortValue", &parameter->shortValue)) ||
        (parameter->intValue_isUsed && read_int32(json, "intValue", &parameter->intValue)) ||
        (parameter->physicalValue_isUsed && read_physical_value(json, "physicalValue", &parameter->physicalValue)) ||
        (parameter->stringValue_isUsed &&
         read_string(json, "stringValue", parameter->stringValue.characters, &parameter->stringValue.charactersLen,
                     iso2_stringValue_CHARACTER_SIZE))) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int parameter_to_json(const struct iso2_ParameterType* parameter, cJSON* json) {
    if (add_string(json, "Name", parameter->Name.characters, parameter->Name.charactersLen) ||
        (parameter->boolValue_isUsed && add_bool(json, "boolValue", parameter->boolValue)) ||
        (parameter->byteValue_isUsed && add_number(json, "byteValue", parameter->byteValue)) ||
        (parameter->shortValue_isUsed && add_number(json, "shortValue", parameter->shortValue)) ||
        (parameter->intValue_isUsed && add_number(json, "intValue", parameter->intValue)) ||
        (parameter->physicalValue_isUsed && add_physical_value(json, "physicalValue", &parameter->physicalValue)) ||
        (parameter->stringValue_isUsed &&
         add_string(json, "stringValue", parameter->stringValue.characters, parameter->stringValue.charactersLen))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_service_detail_res(cJSON* json, struct iso2_ServiceDetailResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) || read_uint16(json, "ServiceID", &msg->ServiceID)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->ServiceParameterList_isUsed = has_item(json, "ServiceParameterList");
    if (!msg->ServiceParameterList_isUsed) {
        return CBV2G_SUCCESS;
    }
    cJSON* list = NULL;
    cJSON* sets = NULL;
    size_t set_count = 0;
    if (get_object(json, "ServiceParameterList", &list) ||
        get_list(list, "ParameterSet", 1, iso2_ParameterSetType_5_ARRAY_SIZE, &sets, &set_count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->ServiceParameterList.ParameterSet.arrayLen = (uint16_t)set_count;
    for (size_t i = 0; i < set_count; i++) {
        cJSON* set_json = list_entry(sets, i);
        struct iso2_ParameterSetType* set = &msg->ServiceParameterList.ParameterSet.array[i];
        cJSON* parameters = NULL;
        size_t parameter_count = 0;
        if (!cJSON_IsObject(set_json)) {
            return input_error("ParameterSet", "entries must be objects");
        }
        if (read_int16(set_json, "ParameterSetID", &set->ParameterSetID) ||
            get_list(set_json, "Parameter", 1, iso2_ParameterType_16_ARRAY_SIZE, &parameters, &parameter_count)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        set->Parameter.arrayLen = (uint16_t)parameter_count;
        for (size_t j = 0; j < parameter_count; j++) {
            cJSON* parameter_json = list_entry(parameters, j);
            if (!cJSON_IsObject(parameter_json)) {
                return input_error("Parameter", "entries must be objects");
            }
            if (json_to_parameter(parameter_json, &set->Parameter.array[j])) {
                return CBV2G_ERROR_JSON_PARSE;
            }
        }
    }
    return CBV2G_SUCCESS;
}

static int service_detail_res_to_json(const struct iso2_ServiceDetailResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) || add_number(json, "ServiceID", msg->ServiceID)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (!msg->ServiceParameterList_isUsed) {
        return CBV2G_SUCCESS;
    }
    cJSON* list = NULL;
    cJSON* sets = NULL;
    if (add_object(json, "ServiceParameterList", &list) || add_array(list, "ParameterSet", &sets)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < msg->ServiceParameterList.ParameterSet.arrayLen; i++) {
        const struct iso2_ParameterSetType* set = &msg->ServiceParameterList.ParameterSet.array[i];
        cJSON* set_json = NULL;
        cJSON* parameters = NULL;
        if (append_object(sets, "ParameterSet", &set_json) ||
            add_number(set_json, "ParameterSetID", set->ParameterSetID) ||
            add_array(set_json, "Parameter", &parameters)) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
        for (uint16_t j = 0; j < set->Parameter.arrayLen; j++) {
            cJSON* parameter_json = NULL;
            if (append_object(parameters, "Parameter", &parameter_json) ||
                parameter_to_json(&set->Parameter.array[j], parameter_json)) {
                return CBV2G_ERROR_JSON_GENERATE;
            }
        }
    }
    return CBV2G_SUCCESS;
}

static int json_to_payment_service_selection_req(cJSON* json, struct iso2_PaymentServiceSelectionReqType* msg) {
    cJSON* list = NULL;
    cJSON* services = NULL;
    size_t count = 0;
    int option = 0;
    if (read_enum(json, "SelectedPaymentOption", payment_options, COUNT_OF(payment_options), &option) ||
        get_object(json, "SelectedServiceList", &list) ||
        get_list(list, "SelectedService", 1, iso2_SelectedServiceType_16_ARRAY_SIZE, &services, &count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->SelectedPaymentOption = (iso2_paymentOptionType)option;
    msg->SelectedServiceList.SelectedService.arrayLen = (uint16_t)count;
    for (size_t i = 0; i < count; i++) {
        cJSON* entry = list_entry(services, i);
        struct iso2_SelectedServiceType* service = &msg->SelectedServiceList.SelectedService.array[i];
        if (!cJSON_IsObject(entry)) {
            return input_error("SelectedService", "entries must be objects");
        }
        if (read_uint16(entry, "ServiceID", &service->ServiceID)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        service->ParameterSetID_isUsed = has_item(entry, "ParameterSetID");
        if (service->ParameterSetID_isUsed && read_int16(entry, "ParameterSetID", &service->ParameterSetID)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
    }
    return CBV2G_SUCCESS;
}

static int payment_service_selection_req_to_json(const struct iso2_PaymentServiceSelectionReqType* msg, cJSON* json) {
    cJSON* list = NULL;
    cJSON* services = NULL;
    if (add_enum(json, "SelectedPaymentOption", payment_options, COUNT_OF(payment_options),
                 (int)msg->SelectedPaymentOption) ||
        add_object(json, "SelectedServiceList", &list) || add_array(list, "SelectedService", &services)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < msg->SelectedServiceList.SelectedService.arrayLen; i++) {
        const struct iso2_SelectedServiceType* service = &msg->SelectedServiceList.SelectedService.array[i];
        cJSON* entry = NULL;
        if (append_object(services, "SelectedService", &entry) || add_number(entry, "ServiceID", service->ServiceID) ||
            (service->ParameterSetID_isUsed && add_number(entry, "ParameterSetID", service->ParameterSetID))) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
    }
    return CBV2G_SUCCESS;
}

static int json_to_payment_service_selection_res(cJSON* json, struct iso2_PaymentServiceSelectionResType* msg) {
    return read_response_code(json, &msg->ResponseCode);
}

static int payment_service_selection_res_to_json(const struct iso2_PaymentServiceSelectionResType* msg, cJSON* json) {
    return add_response_code(json, msg->ResponseCode);
}

/* ============== Authorization ============== */

static int json_to_payment_details_req(cJSON* json, struct iso2_PaymentDetailsReqType* msg) {
    if (read_string(json, "eMAID", msg->eMAID.characters, &msg->eMAID.charactersLen, iso2_eMAID_CHARACTER_SIZE) ||
        read_certificate_chain(json, "ContractSignatureCertChain", &msg->ContractSignatureCertChain)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int payment_details_req_to_json(const struct iso2_PaymentDetailsReqType* msg, cJSON* json) {
    if (add_string(json, "eMAID", msg->eMAID.characters, msg->eMAID.charactersLen) ||
        add_certificate_chain(json, "ContractSignatureCertChain", &msg->ContractSignatureCertChain)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_payment_details_res(cJSON* json, struct iso2_PaymentDetailsResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) ||
        read_base64(json, "GenChallenge", msg->GenChallenge.bytes, &msg->GenChallenge.bytesLen,
                    iso2_genChallengeType_BYTES_SIZE) ||
        read_int64(json, "EVSETimeStamp", &msg->EVSETimeStamp)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int payment_details_res_to_json(const struct iso2_PaymentDetailsResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) ||
        add_base64(json, "GenChallenge", msg->GenChallenge.bytes, msg->GenChallenge.bytesLen) ||
        add_int64(json, "EVSETimeStamp", msg->EVSETimeStamp)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_authorization_req(cJSON* json, struct iso2_AuthorizationReqType* msg) {
    msg->Id_isUsed = has_item(json, "Id");
    msg->GenChallenge_isUsed = has_item(json, "GenChallenge");
    if ((msg->Id_isUsed &&
         read_string(json, "Id", msg->Id.characters, &msg->Id.charactersLen, iso2_Id_CHARACTER_SIZE)) ||
        (msg->GenChallenge_isUsed && read_base64(json, "GenChallenge", msg->GenChallenge.bytes,
                                                 &msg->GenChallenge.bytesLen, iso2_genChallengeType_BYTES_SIZE))) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int authorization_req_to_json(const struct iso2_AuthorizationReqType* msg, cJSON* json) {
    if ((msg->Id_isUsed && add_string(json, "Id", msg->Id.characters, msg->Id.charactersLen)) ||
        (msg->GenChallenge_isUsed &&
         add_base64(json, "GenChallenge", msg->GenChallenge.bytes, msg->GenChallenge.bytesLen))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_authorization_res(cJSON* json, struct iso2_AuthorizationResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) || read_evse_processing(json, &msg->EVSEProcessing)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int authorization_res_to_json(const struct iso2_AuthorizationResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) || add_evse_processing(json, msg->EVSEProcessing)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

/* ============== Charge parameter discovery and power delivery ============== */

static int json_to_charge_parameter_discovery_req(cJSON* json, struct iso2_ChargeParameterDiscoveryReqType* msg) {
    int mode = 0;
    if (read_enum(json, "RequestedEnergyTransferMode", energy_transfer_modes, COUNT_OF(energy_transfer_modes), &mode)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->RequestedEnergyTransferMode = (iso2_EnergyTransferModeType)mode;
    msg->MaxEntriesSAScheduleTuple_isUsed = has_item(json, "MaxEntriesSAScheduleTuple");
    if (msg->MaxEntriesSAScheduleTuple_isUsed &&
        read_uint16(json, "MaxEntriesSAScheduleTuple", &msg->MaxEntriesSAScheduleTuple)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->AC_EVChargeParameter_isUsed = has_item(json, "AC_EVChargeParameter");
    msg->DC_EVChargeParameter_isUsed = has_item(json, "DC_EVChargeParameter");
    if (msg->AC_EVChargeParameter_isUsed == msg->DC_EVChargeParameter_isUsed) {
        set_error("ISO 15118-2: exactly one of 'AC_EVChargeParameter' and 'DC_EVChargeParameter' is required");
        return CBV2G_ERROR_JSON_PARSE;
    }
    return msg->AC_EVChargeParameter_isUsed ? read_ac_ev_charge_parameter(json, &msg->AC_EVChargeParameter)
                                            : read_dc_ev_charge_parameter(json, &msg->DC_EVChargeParameter);
}

static int charge_parameter_discovery_req_to_json(const struct iso2_ChargeParameterDiscoveryReqType* msg, cJSON* json) {
    if ((msg->MaxEntriesSAScheduleTuple_isUsed &&
         add_number(json, "MaxEntriesSAScheduleTuple", msg->MaxEntriesSAScheduleTuple)) ||
        add_enum(json, "RequestedEnergyTransferMode", energy_transfer_modes, COUNT_OF(energy_transfer_modes),
                 (int)msg->RequestedEnergyTransferMode)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (msg->EVChargeParameter_isUsed || msg->AC_EVChargeParameter_isUsed == msg->DC_EVChargeParameter_isUsed) {
        set_error("ISO 15118-2: decoded ChargeParameterDiscoveryReq carries no AC or DC charge parameter");
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return msg->AC_EVChargeParameter_isUsed ? add_ac_ev_charge_parameter(json, &msg->AC_EVChargeParameter)
                                            : add_dc_ev_charge_parameter(json, &msg->DC_EVChargeParameter);
}

static int json_to_charge_parameter_discovery_res(cJSON* json, struct iso2_ChargeParameterDiscoveryResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) || read_evse_processing(json, &msg->EVSEProcessing)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->SAScheduleList_isUsed = has_item(json, "SAScheduleList");
    if (msg->SAScheduleList_isUsed && read_sa_schedule_list(json, &msg->SAScheduleList)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->AC_EVSEChargeParameter_isUsed = has_item(json, "AC_EVSEChargeParameter");
    msg->DC_EVSEChargeParameter_isUsed = has_item(json, "DC_EVSEChargeParameter");
    if (msg->AC_EVSEChargeParameter_isUsed == msg->DC_EVSEChargeParameter_isUsed) {
        set_error("ISO 15118-2: exactly one of 'AC_EVSEChargeParameter' and 'DC_EVSEChargeParameter' is required");
        return CBV2G_ERROR_JSON_PARSE;
    }
    return msg->AC_EVSEChargeParameter_isUsed ? read_ac_evse_charge_parameter(json, &msg->AC_EVSEChargeParameter)
                                              : read_dc_evse_charge_parameter(json, &msg->DC_EVSEChargeParameter);
}

static int charge_parameter_discovery_res_to_json(const struct iso2_ChargeParameterDiscoveryResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) || add_evse_processing(json, msg->EVSEProcessing) ||
        (msg->SAScheduleList_isUsed && add_sa_schedule_list(json, &msg->SAScheduleList))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (msg->SASchedules_isUsed || msg->EVSEChargeParameter_isUsed ||
        msg->AC_EVSEChargeParameter_isUsed == msg->DC_EVSEChargeParameter_isUsed) {
        set_error("ISO 15118-2: decoded ChargeParameterDiscoveryRes carries no AC or DC charge parameter");
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return msg->AC_EVSEChargeParameter_isUsed ? add_ac_evse_charge_parameter(json, &msg->AC_EVSEChargeParameter)
                                              : add_dc_evse_charge_parameter(json, &msg->DC_EVSEChargeParameter);
}

static int json_to_power_delivery_req(cJSON* json, struct iso2_PowerDeliveryReqType* msg) {
    int progress = 0;
    if (read_enum(json, "ChargeProgress", charge_progresses, COUNT_OF(charge_progresses), &progress) ||
        read_uint8(json, "SAScheduleTupleID", ID_8BIT_MIN, 255, &msg->SAScheduleTupleID)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->ChargeProgress = (iso2_chargeProgressType)progress;
    msg->ChargingProfile_isUsed = has_item(json, "ChargingProfile");
    if (msg->ChargingProfile_isUsed && read_charging_profile(json, &msg->ChargingProfile)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->DC_EVPowerDeliveryParameter_isUsed = has_item(json, "DC_EVPowerDeliveryParameter");
    if (!msg->DC_EVPowerDeliveryParameter_isUsed) {
        return CBV2G_SUCCESS;
    }
    struct iso2_DC_EVPowerDeliveryParameterType* param = &msg->DC_EVPowerDeliveryParameter;
    cJSON* param_json = NULL;
    if (get_object(json, "DC_EVPowerDeliveryParameter", &param_json) ||
        read_dc_ev_status(param_json, "DC_EVStatus", &param->DC_EVStatus) ||
        read_bool(param_json, "ChargingComplete", &param->ChargingComplete)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    param->BulkChargingComplete_isUsed = has_item(param_json, "BulkChargingComplete");
    if (param->BulkChargingComplete_isUsed &&
        read_bool(param_json, "BulkChargingComplete", &param->BulkChargingComplete)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int power_delivery_req_to_json(const struct iso2_PowerDeliveryReqType* msg, cJSON* json) {
    if (add_enum(json, "ChargeProgress", charge_progresses, COUNT_OF(charge_progresses), (int)msg->ChargeProgress) ||
        add_number(json, "SAScheduleTupleID", msg->SAScheduleTupleID) ||
        (msg->ChargingProfile_isUsed && add_charging_profile(json, &msg->ChargingProfile))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (msg->EVPowerDeliveryParameter_isUsed) {
        set_error("ISO 15118-2: decoded PowerDeliveryReq carries an abstract EVPowerDeliveryParameter");
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (!msg->DC_EVPowerDeliveryParameter_isUsed) {
        return CBV2G_SUCCESS;
    }
    const struct iso2_DC_EVPowerDeliveryParameterType* param = &msg->DC_EVPowerDeliveryParameter;
    cJSON* param_json = NULL;
    if (add_object(json, "DC_EVPowerDeliveryParameter", &param_json) ||
        add_dc_ev_status(param_json, "DC_EVStatus", &param->DC_EVStatus) ||
        (param->BulkChargingComplete_isUsed &&
         add_bool(param_json, "BulkChargingComplete", param->BulkChargingComplete)) ||
        add_bool(param_json, "ChargingComplete", param->ChargingComplete)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_power_delivery_res(cJSON* json, struct iso2_PowerDeliveryResType* msg) {
    int ac_used = 0;
    int dc_used = 0;
    if (read_response_code(json, &msg->ResponseCode) ||
        read_evse_status_choice(json, &msg->AC_EVSEStatus, &msg->DC_EVSEStatus, &ac_used, &dc_used)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->AC_EVSEStatus_isUsed = ac_used;
    msg->DC_EVSEStatus_isUsed = dc_used;
    return CBV2G_SUCCESS;
}

static int power_delivery_res_to_json(const struct iso2_PowerDeliveryResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) ||
        add_evse_status_choice(json, &msg->AC_EVSEStatus, &msg->DC_EVSEStatus, msg->AC_EVSEStatus_isUsed,
                               msg->DC_EVSEStatus_isUsed, msg->EVSEStatus_isUsed)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

/* ============== Charging loop ============== */

static int json_to_charging_status_req(cJSON* json, struct iso2_ChargingStatusReqType* msg) {
    (void)json;
    (void)msg;
    return CBV2G_SUCCESS;
}

static int charging_status_req_to_json(const struct iso2_ChargingStatusReqType* msg, cJSON* json) {
    (void)msg;
    (void)json;
    return CBV2G_SUCCESS;
}

static int json_to_charging_status_res(cJSON* json, struct iso2_ChargingStatusResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) ||
        read_string(json, "EVSEID", msg->EVSEID.characters, &msg->EVSEID.charactersLen, iso2_EVSEID_CHARACTER_SIZE) ||
        read_uint8(json, "SAScheduleTupleID", ID_8BIT_MIN, 255, &msg->SAScheduleTupleID) ||
        read_ac_evse_status(json, "AC_EVSEStatus", &msg->AC_EVSEStatus)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->EVSEMaxCurrent_isUsed = has_item(json, "EVSEMaxCurrent");
    msg->MeterInfo_isUsed = has_item(json, "MeterInfo");
    msg->ReceiptRequired_isUsed = has_item(json, "ReceiptRequired");
    if ((msg->EVSEMaxCurrent_isUsed && read_physical_value(json, "EVSEMaxCurrent", &msg->EVSEMaxCurrent)) ||
        (msg->MeterInfo_isUsed && read_meter_info(json, "MeterInfo", &msg->MeterInfo)) ||
        (msg->ReceiptRequired_isUsed && read_bool(json, "ReceiptRequired", &msg->ReceiptRequired))) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int charging_status_res_to_json(const struct iso2_ChargingStatusResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) ||
        add_string(json, "EVSEID", msg->EVSEID.characters, msg->EVSEID.charactersLen) ||
        add_number(json, "SAScheduleTupleID", msg->SAScheduleTupleID) ||
        add_optional_physical_value(json, "EVSEMaxCurrent", &msg->EVSEMaxCurrent, msg->EVSEMaxCurrent_isUsed) ||
        (msg->MeterInfo_isUsed && add_meter_info(json, "MeterInfo", &msg->MeterInfo)) ||
        (msg->ReceiptRequired_isUsed && add_bool(json, "ReceiptRequired", msg->ReceiptRequired)) ||
        add_ac_evse_status(json, "AC_EVSEStatus", &msg->AC_EVSEStatus)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_metering_receipt_req(cJSON* json, struct iso2_MeteringReceiptReqType* msg) {
    msg->Id_isUsed = has_item(json, "Id");
    msg->SAScheduleTupleID_isUsed = has_item(json, "SAScheduleTupleID");
    if ((msg->Id_isUsed &&
         read_string(json, "Id", msg->Id.characters, &msg->Id.charactersLen, iso2_Id_CHARACTER_SIZE)) ||
        read_hex(json, "SessionID", msg->SessionID.bytes, &msg->SessionID.bytesLen, iso2_sessionIDType_BYTES_SIZE) ||
        (msg->SAScheduleTupleID_isUsed &&
         read_uint8(json, "SAScheduleTupleID", ID_8BIT_MIN, 255, &msg->SAScheduleTupleID)) ||
        read_meter_info(json, "MeterInfo", &msg->MeterInfo)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int metering_receipt_req_to_json(const struct iso2_MeteringReceiptReqType* msg, cJSON* json) {
    if ((msg->Id_isUsed && add_string(json, "Id", msg->Id.characters, msg->Id.charactersLen)) ||
        add_hex(json, "SessionID", msg->SessionID.bytes, msg->SessionID.bytesLen) ||
        (msg->SAScheduleTupleID_isUsed && add_number(json, "SAScheduleTupleID", msg->SAScheduleTupleID)) ||
        add_meter_info(json, "MeterInfo", &msg->MeterInfo)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_metering_receipt_res(cJSON* json, struct iso2_MeteringReceiptResType* msg) {
    int ac_used = 0;
    int dc_used = 0;
    if (read_response_code(json, &msg->ResponseCode) ||
        read_evse_status_choice(json, &msg->AC_EVSEStatus, &msg->DC_EVSEStatus, &ac_used, &dc_used)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->AC_EVSEStatus_isUsed = ac_used;
    msg->DC_EVSEStatus_isUsed = dc_used;
    return CBV2G_SUCCESS;
}

static int metering_receipt_res_to_json(const struct iso2_MeteringReceiptResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) ||
        add_evse_status_choice(json, &msg->AC_EVSEStatus, &msg->DC_EVSEStatus, msg->AC_EVSEStatus_isUsed,
                               msg->DC_EVSEStatus_isUsed, msg->EVSEStatus_isUsed)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_cable_check_req(cJSON* json, struct iso2_CableCheckReqType* msg) {
    return read_dc_ev_status(json, "DC_EVStatus", &msg->DC_EVStatus);
}

static int cable_check_req_to_json(const struct iso2_CableCheckReqType* msg, cJSON* json) {
    return add_dc_ev_status(json, "DC_EVStatus", &msg->DC_EVStatus);
}

static int json_to_cable_check_res(cJSON* json, struct iso2_CableCheckResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) ||
        read_dc_evse_status(json, "DC_EVSEStatus", &msg->DC_EVSEStatus) ||
        read_evse_processing(json, &msg->EVSEProcessing)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int cable_check_res_to_json(const struct iso2_CableCheckResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) || add_dc_evse_status(json, "DC_EVSEStatus", &msg->DC_EVSEStatus) ||
        add_evse_processing(json, msg->EVSEProcessing)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_pre_charge_req(cJSON* json, struct iso2_PreChargeReqType* msg) {
    if (read_dc_ev_status(json, "DC_EVStatus", &msg->DC_EVStatus) ||
        read_physical_value(json, "EVTargetVoltage", &msg->EVTargetVoltage) ||
        read_physical_value(json, "EVTargetCurrent", &msg->EVTargetCurrent)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int pre_charge_req_to_json(const struct iso2_PreChargeReqType* msg, cJSON* json) {
    if (add_dc_ev_status(json, "DC_EVStatus", &msg->DC_EVStatus) ||
        add_physical_value(json, "EVTargetVoltage", &msg->EVTargetVoltage) ||
        add_physical_value(json, "EVTargetCurrent", &msg->EVTargetCurrent)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_pre_charge_res(cJSON* json, struct iso2_PreChargeResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) ||
        read_dc_evse_status(json, "DC_EVSEStatus", &msg->DC_EVSEStatus) ||
        read_physical_value(json, "EVSEPresentVoltage", &msg->EVSEPresentVoltage)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int pre_charge_res_to_json(const struct iso2_PreChargeResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) || add_dc_evse_status(json, "DC_EVSEStatus", &msg->DC_EVSEStatus) ||
        add_physical_value(json, "EVSEPresentVoltage", &msg->EVSEPresentVoltage)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_current_demand_req(cJSON* json, struct iso2_CurrentDemandReqType* msg) {
    if (read_dc_ev_status(json, "DC_EVStatus", &msg->DC_EVStatus) ||
        read_physical_value(json, "EVTargetCurrent", &msg->EVTargetCurrent) ||
        read_bool(json, "ChargingComplete", &msg->ChargingComplete) ||
        read_physical_value(json, "EVTargetVoltage", &msg->EVTargetVoltage)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->EVMaximumVoltageLimit_isUsed = has_item(json, "EVMaximumVoltageLimit");
    msg->EVMaximumCurrentLimit_isUsed = has_item(json, "EVMaximumCurrentLimit");
    msg->EVMaximumPowerLimit_isUsed = has_item(json, "EVMaximumPowerLimit");
    msg->BulkChargingComplete_isUsed = has_item(json, "BulkChargingComplete");
    msg->RemainingTimeToFullSoC_isUsed = has_item(json, "RemainingTimeToFullSoC");
    msg->RemainingTimeToBulkSoC_isUsed = has_item(json, "RemainingTimeToBulkSoC");
    if ((msg->EVMaximumVoltageLimit_isUsed &&
         read_physical_value(json, "EVMaximumVoltageLimit", &msg->EVMaximumVoltageLimit)) ||
        (msg->EVMaximumCurrentLimit_isUsed &&
         read_physical_value(json, "EVMaximumCurrentLimit", &msg->EVMaximumCurrentLimit)) ||
        (msg->EVMaximumPowerLimit_isUsed &&
         read_physical_value(json, "EVMaximumPowerLimit", &msg->EVMaximumPowerLimit)) ||
        (msg->BulkChargingComplete_isUsed && read_bool(json, "BulkChargingComplete", &msg->BulkChargingComplete)) ||
        (msg->RemainingTimeToFullSoC_isUsed &&
         read_physical_value(json, "RemainingTimeToFullSoC", &msg->RemainingTimeToFullSoC)) ||
        (msg->RemainingTimeToBulkSoC_isUsed &&
         read_physical_value(json, "RemainingTimeToBulkSoC", &msg->RemainingTimeToBulkSoC))) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int current_demand_req_to_json(const struct iso2_CurrentDemandReqType* msg, cJSON* json) {
    if (add_dc_ev_status(json, "DC_EVStatus", &msg->DC_EVStatus) ||
        add_physical_value(json, "EVTargetCurrent", &msg->EVTargetCurrent) ||
        add_optional_physical_value(json, "EVMaximumVoltageLimit", &msg->EVMaximumVoltageLimit,
                                    msg->EVMaximumVoltageLimit_isUsed) ||
        add_optional_physical_value(json, "EVMaximumCurrentLimit", &msg->EVMaximumCurrentLimit,
                                    msg->EVMaximumCurrentLimit_isUsed) ||
        add_optional_physical_value(json, "EVMaximumPowerLimit", &msg->EVMaximumPowerLimit,
                                    msg->EVMaximumPowerLimit_isUsed) ||
        (msg->BulkChargingComplete_isUsed && add_bool(json, "BulkChargingComplete", msg->BulkChargingComplete)) ||
        add_bool(json, "ChargingComplete", msg->ChargingComplete) ||
        add_optional_physical_value(json, "RemainingTimeToFullSoC", &msg->RemainingTimeToFullSoC,
                                    msg->RemainingTimeToFullSoC_isUsed) ||
        add_optional_physical_value(json, "RemainingTimeToBulkSoC", &msg->RemainingTimeToBulkSoC,
                                    msg->RemainingTimeToBulkSoC_isUsed) ||
        add_physical_value(json, "EVTargetVoltage", &msg->EVTargetVoltage)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_current_demand_res(cJSON* json, struct iso2_CurrentDemandResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) ||
        read_dc_evse_status(json, "DC_EVSEStatus", &msg->DC_EVSEStatus) ||
        read_physical_value(json, "EVSEPresentVoltage", &msg->EVSEPresentVoltage) ||
        read_physical_value(json, "EVSEPresentCurrent", &msg->EVSEPresentCurrent) ||
        read_bool(json, "EVSECurrentLimitAchieved", &msg->EVSECurrentLimitAchieved) ||
        read_bool(json, "EVSEVoltageLimitAchieved", &msg->EVSEVoltageLimitAchieved) ||
        read_bool(json, "EVSEPowerLimitAchieved", &msg->EVSEPowerLimitAchieved) ||
        read_string(json, "EVSEID", msg->EVSEID.characters, &msg->EVSEID.charactersLen, iso2_EVSEID_CHARACTER_SIZE) ||
        read_uint8(json, "SAScheduleTupleID", ID_8BIT_MIN, 255, &msg->SAScheduleTupleID)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->EVSEMaximumVoltageLimit_isUsed = has_item(json, "EVSEMaximumVoltageLimit");
    msg->EVSEMaximumCurrentLimit_isUsed = has_item(json, "EVSEMaximumCurrentLimit");
    msg->EVSEMaximumPowerLimit_isUsed = has_item(json, "EVSEMaximumPowerLimit");
    msg->MeterInfo_isUsed = has_item(json, "MeterInfo");
    msg->ReceiptRequired_isUsed = has_item(json, "ReceiptRequired");
    if ((msg->EVSEMaximumVoltageLimit_isUsed &&
         read_physical_value(json, "EVSEMaximumVoltageLimit", &msg->EVSEMaximumVoltageLimit)) ||
        (msg->EVSEMaximumCurrentLimit_isUsed &&
         read_physical_value(json, "EVSEMaximumCurrentLimit", &msg->EVSEMaximumCurrentLimit)) ||
        (msg->EVSEMaximumPowerLimit_isUsed &&
         read_physical_value(json, "EVSEMaximumPowerLimit", &msg->EVSEMaximumPowerLimit)) ||
        (msg->MeterInfo_isUsed && read_meter_info(json, "MeterInfo", &msg->MeterInfo)) ||
        (msg->ReceiptRequired_isUsed && read_bool(json, "ReceiptRequired", &msg->ReceiptRequired))) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int current_demand_res_to_json(const struct iso2_CurrentDemandResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) || add_dc_evse_status(json, "DC_EVSEStatus", &msg->DC_EVSEStatus) ||
        add_physical_value(json, "EVSEPresentVoltage", &msg->EVSEPresentVoltage) ||
        add_physical_value(json, "EVSEPresentCurrent", &msg->EVSEPresentCurrent) ||
        add_bool(json, "EVSECurrentLimitAchieved", msg->EVSECurrentLimitAchieved) ||
        add_bool(json, "EVSEVoltageLimitAchieved", msg->EVSEVoltageLimitAchieved) ||
        add_bool(json, "EVSEPowerLimitAchieved", msg->EVSEPowerLimitAchieved) ||
        add_optional_physical_value(json, "EVSEMaximumVoltageLimit", &msg->EVSEMaximumVoltageLimit,
                                    msg->EVSEMaximumVoltageLimit_isUsed) ||
        add_optional_physical_value(json, "EVSEMaximumCurrentLimit", &msg->EVSEMaximumCurrentLimit,
                                    msg->EVSEMaximumCurrentLimit_isUsed) ||
        add_optional_physical_value(json, "EVSEMaximumPowerLimit", &msg->EVSEMaximumPowerLimit,
                                    msg->EVSEMaximumPowerLimit_isUsed) ||
        add_string(json, "EVSEID", msg->EVSEID.characters, msg->EVSEID.charactersLen) ||
        add_number(json, "SAScheduleTupleID", msg->SAScheduleTupleID) ||
        (msg->MeterInfo_isUsed && add_meter_info(json, "MeterInfo", &msg->MeterInfo)) ||
        (msg->ReceiptRequired_isUsed && add_bool(json, "ReceiptRequired", msg->ReceiptRequired))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_welding_detection_req(cJSON* json, struct iso2_WeldingDetectionReqType* msg) {
    return read_dc_ev_status(json, "DC_EVStatus", &msg->DC_EVStatus);
}

static int welding_detection_req_to_json(const struct iso2_WeldingDetectionReqType* msg, cJSON* json) {
    return add_dc_ev_status(json, "DC_EVStatus", &msg->DC_EVStatus);
}

static int json_to_welding_detection_res(cJSON* json, struct iso2_WeldingDetectionResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) ||
        read_dc_evse_status(json, "DC_EVSEStatus", &msg->DC_EVSEStatus) ||
        read_physical_value(json, "EVSEPresentVoltage", &msg->EVSEPresentVoltage)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int welding_detection_res_to_json(const struct iso2_WeldingDetectionResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) || add_dc_evse_status(json, "DC_EVSEStatus", &msg->DC_EVSEStatus) ||
        add_physical_value(json, "EVSEPresentVoltage", &msg->EVSEPresentVoltage)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_session_stop_req(cJSON* json, struct iso2_SessionStopReqType* msg) {
    int session = 0;
    if (read_enum(json, "ChargingSession", charging_sessions, COUNT_OF(charging_sessions), &session)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->ChargingSession = (iso2_chargingSessionType)session;
    return CBV2G_SUCCESS;
}

static int session_stop_req_to_json(const struct iso2_SessionStopReqType* msg, cJSON* json) {
    return add_enum(json, "ChargingSession", charging_sessions, COUNT_OF(charging_sessions), (int)msg->ChargingSession);
}

static int json_to_session_stop_res(cJSON* json, struct iso2_SessionStopResType* msg) {
    return read_response_code(json, &msg->ResponseCode);
}

static int session_stop_res_to_json(const struct iso2_SessionStopResType* msg, cJSON* json) {
    return add_response_code(json, msg->ResponseCode);
}

/* ============== xmldsig ============== */

static int read_algorithm(cJSON* parent, const char* key, char* chars, uint16_t* len) {
    cJSON* json = NULL;
    if (get_object(parent, key, &json) || read_string(json, "Algorithm", chars, len, iso2_Algorithm_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int add_algorithm(cJSON* parent, const char* key, const char* chars, uint16_t len, int any_used) {
    cJSON* json = NULL;
    if (any_used) {
        set_error("ISO 15118-2: decoded %s carries content that is not supported", key);
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (add_object(parent, key, &json) || add_string(json, "Algorithm", chars, len)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_reference(cJSON* json, struct iso2_ReferenceType* reference) {
    reference->Id_isUsed = has_item(json, "Id");
    reference->URI_isUsed = has_item(json, "URI");
    reference->Type_isUsed = has_item(json, "Type");
    reference->Transforms_isUsed = has_item(json, "Transforms");
    if ((reference->Id_isUsed &&
         read_string(json, "Id", reference->Id.characters, &reference->Id.charactersLen, iso2_Id_CHARACTER_SIZE)) ||
        (reference->URI_isUsed &&
         read_string(json, "URI", reference->URI.characters, &reference->URI.charactersLen, iso2_URI_CHARACTER_SIZE)) ||
        (reference->Type_isUsed && read_string(json, "Type", reference->Type.characters, &reference->Type.charactersLen,
                                               iso2_Type_CHARACTER_SIZE)) ||
        read_algorithm(json, "DigestMethod", reference->DigestMethod.Algorithm.characters,
                       &reference->DigestMethod.Algorithm.charactersLen) ||
        read_base64(json, "DigestValue", reference->DigestValue.bytes, &reference->DigestValue.bytesLen,
                    iso2_DigestValueType_BYTES_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    if (!reference->Transforms_isUsed) {
        return CBV2G_SUCCESS;
    }
    /* libcbv2g holds a single Transform per Reference. */
    struct iso2_TransformType* transform = &reference->Transforms.Transform;
    cJSON* transforms = NULL;
    cJSON* list = NULL;
    size_t count = 0;
    if (get_object(json, "Transforms", &transforms) || get_list(transforms, "Transform", 1, 1, &list, &count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    cJSON* transform_json = list_entry(list, 0);
    if (!cJSON_IsObject(transform_json)) {
        return input_error("Transform", "entries must be objects");
    }
    if (read_string(transform_json, "Algorithm", transform->Algorithm.characters, &transform->Algorithm.charactersLen,
                    iso2_Algorithm_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    transform->XPath_isUsed = has_item(transform_json, "XPath");
    if (transform->XPath_isUsed && read_string(transform_json, "XPath", transform->XPath.characters,
                                               &transform->XPath.charactersLen, iso2_XPath_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int reference_to_json(const struct iso2_ReferenceType* reference, cJSON* json) {
    if ((reference->Id_isUsed && add_string(json, "Id", reference->Id.characters, reference->Id.charactersLen)) ||
        (reference->URI_isUsed && add_string(json, "URI", reference->URI.characters, reference->URI.charactersLen)) ||
        (reference->Type_isUsed &&
         add_string(json, "Type", reference->Type.characters, reference->Type.charactersLen))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (reference->Transforms_isUsed) {
        const struct iso2_TransformType* transform = &reference->Transforms.Transform;
        cJSON* transforms = NULL;
        cJSON* list = NULL;
        cJSON* transform_json = NULL;
        if (transform->ANY_isUsed) {
            set_error("ISO 15118-2: decoded Transform carries content that is not supported");
            return CBV2G_ERROR_JSON_GENERATE;
        }
        if (add_object(json, "Transforms", &transforms) || add_array(transforms, "Transform", &list) ||
            append_object(list, "Transform", &transform_json) ||
            add_string(transform_json, "Algorithm", transform->Algorithm.characters,
                       transform->Algorithm.charactersLen) ||
            (transform->XPath_isUsed &&
             add_string(transform_json, "XPath", transform->XPath.characters, transform->XPath.charactersLen))) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
    }
    if (add_algorithm(json, "DigestMethod", reference->DigestMethod.Algorithm.characters,
                      reference->DigestMethod.Algorithm.charactersLen, reference->DigestMethod.ANY_isUsed) ||
        add_base64(json, "DigestValue", reference->DigestValue.bytes, reference->DigestValue.bytesLen)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_signed_info(cJSON* json, struct iso2_SignedInfoType* info) {
    cJSON* references = NULL;
    size_t count = 0;
    if (has_item(json, "SignatureMethod")) {
        cJSON* method = NULL;
        if (get_object(json, "SignatureMethod", &method) == CBV2G_SUCCESS && has_item(method, "HMACOutputLength")) {
            return input_error("HMACOutputLength", "is not supported");
        }
    }
    info->Id_isUsed = has_item(json, "Id");
    if ((info->Id_isUsed &&
         read_string(json, "Id", info->Id.characters, &info->Id.charactersLen, iso2_Id_CHARACTER_SIZE)) ||
        read_algorithm(json, "CanonicalizationMethod", info->CanonicalizationMethod.Algorithm.characters,
                       &info->CanonicalizationMethod.Algorithm.charactersLen) ||
        read_algorithm(json, "SignatureMethod", info->SignatureMethod.Algorithm.characters,
                       &info->SignatureMethod.Algorithm.charactersLen) ||
        get_list(json, "Reference", 1, iso2_ReferenceType_4_ARRAY_SIZE, &references, &count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    info->Reference.arrayLen = (uint16_t)count;
    for (size_t i = 0; i < count; i++) {
        cJSON* reference = list_entry(references, i);
        if (!cJSON_IsObject(reference)) {
            return input_error("Reference", "entries must be objects");
        }
        if (json_to_reference(reference, &info->Reference.array[i])) {
            return CBV2G_ERROR_JSON_PARSE;
        }
    }
    return CBV2G_SUCCESS;
}

static int signed_info_to_json(const struct iso2_SignedInfoType* info, cJSON* json) {
    cJSON* references = NULL;
    if (info->SignatureMethod.HMACOutputLength_isUsed) {
        set_error("ISO 15118-2: decoded SignatureMethod carries an HMACOutputLength, which is not supported");
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if ((info->Id_isUsed && add_string(json, "Id", info->Id.characters, info->Id.charactersLen)) ||
        add_algorithm(json, "CanonicalizationMethod", info->CanonicalizationMethod.Algorithm.characters,
                      info->CanonicalizationMethod.Algorithm.charactersLen, info->CanonicalizationMethod.ANY_isUsed) ||
        add_algorithm(json, "SignatureMethod", info->SignatureMethod.Algorithm.characters,
                      info->SignatureMethod.Algorithm.charactersLen, info->SignatureMethod.ANY_isUsed) ||
        add_array(json, "Reference", &references)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < info->Reference.arrayLen; i++) {
        cJSON* reference = NULL;
        if (append_object(references, "Reference", &reference) ||
            reference_to_json(&info->Reference.array[i], reference)) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
    }
    return CBV2G_SUCCESS;
}

/* SignatureValue is {"value": "<base64>"} with an optional "Id". */
static int read_signature_value(cJSON* parent, struct iso2_SignatureValueType* value) {
    cJSON* item = NULL;
    if (get_object(parent, "SignatureValue", &item)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    value->Id_isUsed = has_item(item, "Id");
    if (value->Id_isUsed &&
        read_string(item, "Id", value->Id.characters, &value->Id.charactersLen, iso2_Id_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return read_base64(item, "value", value->CONTENT.bytes, &value->CONTENT.bytesLen,
                       iso2_SignatureValueType_BYTES_SIZE);
}

static int read_signature(cJSON* parent, struct iso2_SignatureType* signature) {
    cJSON* json = NULL;
    cJSON* signed_info = NULL;
    if (get_object(parent, "Signature", &json)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    if (has_item(json, "KeyInfo") || has_item(json, "Object")) {
        return input_error("Signature", "KeyInfo and Object are not supported");
    }
    signature->Id_isUsed = has_item(json, "Id");
    signature->KeyInfo_isUsed = 0;
    signature->Object_isUsed = 0;
    if ((signature->Id_isUsed &&
         read_string(json, "Id", signature->Id.characters, &signature->Id.charactersLen, iso2_Id_CHARACTER_SIZE)) ||
        get_object(json, "SignedInfo", &signed_info) || json_to_signed_info(signed_info, &signature->SignedInfo) ||
        read_signature_value(json, &signature->SignatureValue)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int add_signature(cJSON* parent, const struct iso2_SignatureType* signature) {
    cJSON* json = NULL;
    cJSON* signed_info = NULL;
    cJSON* value = NULL;
    if (signature->KeyInfo_isUsed || signature->Object_isUsed) {
        set_error("ISO 15118-2: decoded Signature carries KeyInfo or Object, which is not supported");
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (add_object(parent, "Signature", &json) ||
        (signature->Id_isUsed && add_string(json, "Id", signature->Id.characters, signature->Id.charactersLen)) ||
        add_object(json, "SignedInfo", &signed_info) || signed_info_to_json(&signature->SignedInfo, signed_info) ||
        add_object(json, "SignatureValue", &value) ||
        (signature->SignatureValue.Id_isUsed && add_string(value, "Id", signature->SignatureValue.Id.characters,
                                                           signature->SignatureValue.Id.charactersLen)) ||
        add_base64(value, "value", signature->SignatureValue.CONTENT.bytes,
                   signature->SignatureValue.CONTENT.bytesLen)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

/* ============== Header ============== */

static int json_to_header(cJSON* json, struct iso2_MessageHeaderType* header) {
    init_iso2_MessageHeaderType(header);
    if (read_hex(json, "SessionID", header->SessionID.bytes, &header->SessionID.bytesLen,
                 iso2_sessionIDType_BYTES_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    header->Notification_isUsed = has_item(json, "Notification");
    if (header->Notification_isUsed) {
        struct iso2_NotificationType* notification = &header->Notification;
        cJSON* notification_json = NULL;
        int code = 0;
        if (get_object(json, "Notification", &notification_json) ||
            read_enum(notification_json, "FaultCode", fault_codes, COUNT_OF(fault_codes), &code)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        notification->FaultCode = (iso2_faultCodeType)code;
        notification->FaultMsg_isUsed = has_item(notification_json, "FaultMsg");
        if (notification->FaultMsg_isUsed &&
            read_string(notification_json, "FaultMsg", notification->FaultMsg.characters,
                        &notification->FaultMsg.charactersLen, iso2_FaultMsg_CHARACTER_SIZE)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
    }
    header->Signature_isUsed = has_item(json, "Signature");
    if (header->Signature_isUsed && read_signature(json, &header->Signature)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int header_to_json(const struct iso2_MessageHeaderType* header, cJSON* json) {
    if (add_hex(json, "SessionID", header->SessionID.bytes, header->SessionID.bytesLen)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    if (header->Notification_isUsed) {
        const struct iso2_NotificationType* notification = &header->Notification;
        cJSON* notification_json = NULL;
        if (add_object(json, "Notification", &notification_json) ||
            add_enum(notification_json, "FaultCode", fault_codes, COUNT_OF(fault_codes),
                     (int)notification->FaultCode) ||
            (notification->FaultMsg_isUsed &&
             add_string(notification_json, "FaultMsg", notification->FaultMsg.characters,
                        notification->FaultMsg.charactersLen))) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
    }
    if (header->Signature_isUsed && add_signature(json, &header->Signature)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

/* ============== Certificate installation and update ============== */

/*
 * Serial numbers have at most 20 octets (RFC 5280, 4.1.2.2). They are handed to
 * libcbv2g in a wider buffer: its conversion to EXI octets reads one byte
 * before a magnitude that fills the whole buffer.
 */
#define SERIAL_MAX_OCTETS         25
#define SERIAL_SIGNIFICANT_OCTETS 20

/*
 * X509SerialNumber is an xs:integer of up to 20 octets. It travels as a JSON
 * integer, which prepare_json turns into a string of decimal digits
 * before cJSON can round it to a double. EXI codes a negative integer N as the
 * magnitude |N| - 1 (EXI 1.0, 7.1.5).
 */
static int item_to_serial_number(cJSON* item, exi_signed_t* serial) {
    char digits[3 * SERIAL_MAX_OCTETS + 2];
    const char* text = NULL;
    if (cJSON_IsString(item)) {
        text = item->valuestring;
    } else if (cJSON_IsNumber(item)) {
        int64_t value = 0;
        if (item_to_integer(item, "X509SerialNumber", -JSON_SAFE_INTEGER_MAX, JSON_SAFE_INTEGER_MAX, &value)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        snprintf(digits, sizeof(digits), "%" PRId64, value);
        text = digits;
    } else {
        return input_error("X509SerialNumber", "must be an integer");
    }

    const int negative = text[0] == '-';
    const char* p = negative ? text + 1 : text;
    uint8_t magnitude[SERIAL_MAX_OCTETS] = {0};
    if (*p == '\0' || (p[0] == '0' && p[1] != '\0')) {
        return input_error("X509SerialNumber", "must be an integer");
    }
    for (; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return input_error("X509SerialNumber", "must be an integer");
        }
        unsigned carry = (unsigned)(*p - '0');
        for (int i = SERIAL_MAX_OCTETS - 1; i >= 0; i--) {
            const unsigned v = magnitude[i] * 10u + carry;
            magnitude[i] = (uint8_t)(v & 0xFF);
            carry = v >> 8;
        }
        if (carry != 0) {
            return input_error("X509SerialNumber", "is longer than 20 octets");
        }
    }
    for (int i = 0; i < SERIAL_MAX_OCTETS - SERIAL_SIGNIFICANT_OCTETS; i++) {
        if (magnitude[i] != 0) {
            return input_error("X509SerialNumber", "is longer than 20 octets");
        }
    }
    if (negative) {
        int nonzero = 0;
        for (int i = 0; i < SERIAL_MAX_OCTETS; i++) {
            nonzero |= magnitude[i];
        }
        if (!nonzero) {
            return input_error("X509SerialNumber", "must be an integer");
        }
        for (int i = SERIAL_MAX_OCTETS - 1; i >= 0; i--) {
            if (magnitude[i]-- != 0) {
                break;
            }
        }
    }
    memset(serial, 0, sizeof(*serial));
    serial->is_negative = negative ? 1 : 0;
    memcpy(serial->data.octets, magnitude, SERIAL_MAX_OCTETS);
    serial->data.octets_count = SERIAL_MAX_OCTETS;
    return CBV2G_SUCCESS;
}

static int add_serial_number(cJSON* parent, const exi_signed_t* serial) {
    uint8_t magnitude[EXI_BASETYPES_MAX_OCTETS_SUPPORTED + 1] = {0};
    char reversed[3 * (EXI_BASETYPES_MAX_OCTETS_SUPPORTED + 1) + 2];
    char digits[sizeof(reversed) + 1];
    size_t count = serial->data.octets_count;
    size_t n = 0;

    size_t significant = count;
    for (size_t i = 0; i < count && serial->data.octets[i] == 0; i++) {
        significant--;
    }
    if (count > EXI_BASETYPES_MAX_OCTETS_SUPPORTED || significant > SERIAL_SIGNIFICANT_OCTETS) {
        set_error("ISO 15118-2: decoded X509SerialNumber is longer than 20 octets");
        return CBV2G_ERROR_JSON_GENERATE;
    }
    memcpy(magnitude + sizeof(magnitude) - count, serial->data.octets, count);
    if (serial->is_negative) {
        for (int i = (int)sizeof(magnitude) - 1; i >= 0; i--) {
            if (++magnitude[i] != 0) {
                break;
            }
        }
    }
    int nonzero = 1;
    while (nonzero) {
        unsigned remainder = 0;
        nonzero = 0;
        for (size_t i = 0; i < sizeof(magnitude); i++) {
            const unsigned v = (remainder << 8) | magnitude[i];
            magnitude[i] = (uint8_t)(v / 10u);
            remainder = v % 10u;
            nonzero |= magnitude[i];
        }
        reversed[n++] = (char)('0' + remainder);
    }
    size_t k = 0;
    if (serial->is_negative) {
        digits[k++] = '-';
    }
    while (n > 0) {
        digits[k++] = reversed[--n];
    }
    digits[k] = '\0';
    return cJSON_AddRawToObject(parent, "X509SerialNumber", digits) != NULL ? CBV2G_SUCCESS
                                                                            : output_error("X509SerialNumber");
}

static int read_root_certificate_ids(cJSON* parent, struct iso2_ListOfRootCertificateIDsType* ids) {
    cJSON* json = NULL;
    cJSON* list = NULL;
    size_t count = 0;
    if (get_object(parent, "ListOfRootCertificateIDs", &json) ||
        get_list(json, "RootCertificateID", 1, iso2_X509IssuerSerialType_5_ARRAY_SIZE, &list, &count)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    ids->RootCertificateID.arrayLen = (uint16_t)count;
    for (size_t i = 0; i < count; i++) {
        cJSON* entry = list_entry(list, i);
        struct iso2_X509IssuerSerialType* id = &ids->RootCertificateID.array[i];
        cJSON* serial = NULL;
        if (!cJSON_IsObject(entry)) {
            return input_error("RootCertificateID", "entries must be objects");
        }
        if (read_string(entry, "X509IssuerName", id->X509IssuerName.characters, &id->X509IssuerName.charactersLen,
                        iso2_X509IssuerName_CHARACTER_SIZE) ||
            get_item(entry, "X509SerialNumber", &serial) || item_to_serial_number(serial, &id->X509SerialNumber)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
    }
    return CBV2G_SUCCESS;
}

static int add_root_certificate_ids(cJSON* parent, const struct iso2_ListOfRootCertificateIDsType* ids) {
    cJSON* json = NULL;
    cJSON* list = NULL;
    if (add_object(parent, "ListOfRootCertificateIDs", &json) || add_array(json, "RootCertificateID", &list)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    for (uint16_t i = 0; i < ids->RootCertificateID.arrayLen; i++) {
        const struct iso2_X509IssuerSerialType* id = &ids->RootCertificateID.array[i];
        cJSON* entry = NULL;
        if (append_object(list, "RootCertificateID", &entry) ||
            add_string(entry, "X509IssuerName", id->X509IssuerName.characters, id->X509IssuerName.charactersLen) ||
            add_serial_number(entry, &id->X509SerialNumber)) {
            return CBV2G_ERROR_JSON_GENERATE;
        }
    }
    return CBV2G_SUCCESS;
}

/* ContractSignatureEncryptedPrivateKey and DHpublickey: {"Id": ..., "value": "<base64>"}. */
static int json_to_id_and_bytes(cJSON* json, char* id, uint16_t* id_len, uint8_t* bytes, uint16_t* bytes_len,
                                size_t capacity) {
    if (read_string(json, "Id", id, id_len, iso2_Id_CHARACTER_SIZE) ||
        read_base64(json, "value", bytes, bytes_len, capacity)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int id_and_bytes_to_json(cJSON* json, const char* id, uint16_t id_len, const uint8_t* bytes,
                                uint16_t bytes_len) {
    if (add_string(json, "Id", id, id_len) || add_base64(json, "value", bytes, bytes_len)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int read_encrypted_private_key(cJSON* parent, struct iso2_ContractSignatureEncryptedPrivateKeyType* key) {
    cJSON* json = NULL;
    if (get_object(parent, "ContractSignatureEncryptedPrivateKey", &json)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return json_to_id_and_bytes(json, key->Id.characters, &key->Id.charactersLen, key->CONTENT.bytes,
                                &key->CONTENT.bytesLen, iso2_ContractSignatureEncryptedPrivateKeyType_BYTES_SIZE);
}

static int add_encrypted_private_key(cJSON* parent, const struct iso2_ContractSignatureEncryptedPrivateKeyType* key) {
    cJSON* json = NULL;
    if (add_object(parent, "ContractSignatureEncryptedPrivateKey", &json)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return id_and_bytes_to_json(json, key->Id.characters, key->Id.charactersLen, key->CONTENT.bytes,
                                key->CONTENT.bytesLen);
}

static int read_dh_public_key(cJSON* parent, struct iso2_DiffieHellmanPublickeyType* key) {
    cJSON* json = NULL;
    if (get_object(parent, "DHpublickey", &json)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return json_to_id_and_bytes(json, key->Id.characters, &key->Id.charactersLen, key->CONTENT.bytes,
                                &key->CONTENT.bytesLen, iso2_DiffieHellmanPublickeyType_BYTES_SIZE);
}

static int add_dh_public_key(cJSON* parent, const struct iso2_DiffieHellmanPublickeyType* key) {
    cJSON* json = NULL;
    if (add_object(parent, "DHpublickey", &json)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return id_and_bytes_to_json(json, key->Id.characters, key->Id.charactersLen, key->CONTENT.bytes,
                                key->CONTENT.bytesLen);
}

/* The eMAID element of EMAIDType: {"Id": ..., "value": "<eMAID>"}. */
static int read_emaid_element(cJSON* parent, struct iso2_EMAIDType* emaid) {
    cJSON* json = NULL;
    if (get_object(parent, "eMAID", &json) ||
        read_string(json, "Id", emaid->Id.characters, &emaid->Id.charactersLen, iso2_Id_CHARACTER_SIZE) ||
        read_string(json, "value", emaid->CONTENT.characters, &emaid->CONTENT.charactersLen,
                    iso2_CONTENT_CHARACTER_SIZE)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int add_emaid_element(cJSON* parent, const struct iso2_EMAIDType* emaid) {
    cJSON* json = NULL;
    if (add_object(parent, "eMAID", &json) || add_string(json, "Id", emaid->Id.characters, emaid->Id.charactersLen) ||
        add_string(json, "value", emaid->CONTENT.characters, emaid->CONTENT.charactersLen)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_certificate_installation_req(cJSON* json, struct iso2_CertificateInstallationReqType* msg) {
    if (read_string(json, "Id", msg->Id.characters, &msg->Id.charactersLen, iso2_Id_CHARACTER_SIZE) ||
        read_base64(json, "OEMProvisioningCert", msg->OEMProvisioningCert.bytes, &msg->OEMProvisioningCert.bytesLen,
                    iso2_certificateType_BYTES_SIZE) ||
        read_root_certificate_ids(json, &msg->ListOfRootCertificateIDs)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int certificate_installation_req_to_json(const struct iso2_CertificateInstallationReqType* msg, cJSON* json) {
    if (add_string(json, "Id", msg->Id.characters, msg->Id.charactersLen) ||
        add_base64(json, "OEMProvisioningCert", msg->OEMProvisioningCert.bytes, msg->OEMProvisioningCert.bytesLen) ||
        add_root_certificate_ids(json, &msg->ListOfRootCertificateIDs)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_certificate_installation_res(cJSON* json, struct iso2_CertificateInstallationResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) ||
        read_certificate_chain(json, "SAProvisioningCertificateChain", &msg->SAProvisioningCertificateChain) ||
        read_certificate_chain(json, "ContractSignatureCertChain", &msg->ContractSignatureCertChain) ||
        read_encrypted_private_key(json, &msg->ContractSignatureEncryptedPrivateKey) ||
        read_dh_public_key(json, &msg->DHpublickey) || read_emaid_element(json, &msg->eMAID)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int certificate_installation_res_to_json(const struct iso2_CertificateInstallationResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) ||
        add_certificate_chain(json, "SAProvisioningCertificateChain", &msg->SAProvisioningCertificateChain) ||
        add_certificate_chain(json, "ContractSignatureCertChain", &msg->ContractSignatureCertChain) ||
        add_encrypted_private_key(json, &msg->ContractSignatureEncryptedPrivateKey) ||
        add_dh_public_key(json, &msg->DHpublickey) || add_emaid_element(json, &msg->eMAID)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_certificate_update_req(cJSON* json, struct iso2_CertificateUpdateReqType* msg) {
    if (read_string(json, "Id", msg->Id.characters, &msg->Id.charactersLen, iso2_Id_CHARACTER_SIZE) ||
        read_certificate_chain(json, "ContractSignatureCertChain", &msg->ContractSignatureCertChain) ||
        read_string(json, "eMAID", msg->eMAID.characters, &msg->eMAID.charactersLen, iso2_eMAID_CHARACTER_SIZE) ||
        read_root_certificate_ids(json, &msg->ListOfRootCertificateIDs)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int certificate_update_req_to_json(const struct iso2_CertificateUpdateReqType* msg, cJSON* json) {
    if (add_string(json, "Id", msg->Id.characters, msg->Id.charactersLen) ||
        add_certificate_chain(json, "ContractSignatureCertChain", &msg->ContractSignatureCertChain) ||
        add_string(json, "eMAID", msg->eMAID.characters, msg->eMAID.charactersLen) ||
        add_root_certificate_ids(json, &msg->ListOfRootCertificateIDs)) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_certificate_update_res(cJSON* json, struct iso2_CertificateUpdateResType* msg) {
    if (read_response_code(json, &msg->ResponseCode) ||
        read_certificate_chain(json, "SAProvisioningCertificateChain", &msg->SAProvisioningCertificateChain) ||
        read_certificate_chain(json, "ContractSignatureCertChain", &msg->ContractSignatureCertChain) ||
        read_encrypted_private_key(json, &msg->ContractSignatureEncryptedPrivateKey) ||
        read_dh_public_key(json, &msg->DHpublickey) || read_emaid_element(json, &msg->eMAID)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    msg->RetryCounter_isUsed = has_item(json, "RetryCounter");
    if (msg->RetryCounter_isUsed && read_int16(json, "RetryCounter", &msg->RetryCounter)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int certificate_update_res_to_json(const struct iso2_CertificateUpdateResType* msg, cJSON* json) {
    if (add_response_code(json, msg->ResponseCode) ||
        add_certificate_chain(json, "SAProvisioningCertificateChain", &msg->SAProvisioningCertificateChain) ||
        add_certificate_chain(json, "ContractSignatureCertChain", &msg->ContractSignatureCertChain) ||
        add_encrypted_private_key(json, &msg->ContractSignatureEncryptedPrivateKey) ||
        add_dh_public_key(json, &msg->DHpublickey) || add_emaid_element(json, &msg->eMAID) ||
        (msg->RetryCounter_isUsed && add_number(json, "RetryCounter", msg->RetryCounter))) {
        return CBV2G_ERROR_JSON_GENERATE;
    }
    return CBV2G_SUCCESS;
}

/* ============== Body dispatch ============== */

/*
 * Converts the single message in a Body object. Initialises the matching
 * union member and sets its _isUsed flag before the converter runs.
 */
static int json_to_body(cJSON* body, struct iso2_BodyType* out) {
    if (cJSON_GetArraySize(body) != 1) {
        set_error("ISO 15118-2: Body must hold exactly one message");
        return CBV2G_ERROR_JSON_PARSE;
    }
    cJSON* msg = body->child;
    const char* name = msg->string;
    if (!cJSON_IsObject(msg)) {
        return input_error(name, "must be an object");
    }

    if (strcmp(name, "SessionSetupReq") == 0) {
        init_iso2_SessionSetupReqType(&out->SessionSetupReq);
        out->SessionSetupReq_isUsed = 1;
        return json_to_session_setup_req(msg, &out->SessionSetupReq);
    }
    if (strcmp(name, "SessionSetupRes") == 0) {
        init_iso2_SessionSetupResType(&out->SessionSetupRes);
        out->SessionSetupRes_isUsed = 1;
        return json_to_session_setup_res(msg, &out->SessionSetupRes);
    }
    if (strcmp(name, "ServiceDiscoveryReq") == 0) {
        init_iso2_ServiceDiscoveryReqType(&out->ServiceDiscoveryReq);
        out->ServiceDiscoveryReq_isUsed = 1;
        return json_to_service_discovery_req(msg, &out->ServiceDiscoveryReq);
    }
    if (strcmp(name, "ServiceDiscoveryRes") == 0) {
        init_iso2_ServiceDiscoveryResType(&out->ServiceDiscoveryRes);
        out->ServiceDiscoveryRes_isUsed = 1;
        return json_to_service_discovery_res(msg, &out->ServiceDiscoveryRes);
    }
    if (strcmp(name, "ServiceDetailReq") == 0) {
        init_iso2_ServiceDetailReqType(&out->ServiceDetailReq);
        out->ServiceDetailReq_isUsed = 1;
        return json_to_service_detail_req(msg, &out->ServiceDetailReq);
    }
    if (strcmp(name, "ServiceDetailRes") == 0) {
        init_iso2_ServiceDetailResType(&out->ServiceDetailRes);
        out->ServiceDetailRes_isUsed = 1;
        return json_to_service_detail_res(msg, &out->ServiceDetailRes);
    }
    if (strcmp(name, "PaymentServiceSelectionReq") == 0) {
        init_iso2_PaymentServiceSelectionReqType(&out->PaymentServiceSelectionReq);
        out->PaymentServiceSelectionReq_isUsed = 1;
        return json_to_payment_service_selection_req(msg, &out->PaymentServiceSelectionReq);
    }
    if (strcmp(name, "PaymentServiceSelectionRes") == 0) {
        init_iso2_PaymentServiceSelectionResType(&out->PaymentServiceSelectionRes);
        out->PaymentServiceSelectionRes_isUsed = 1;
        return json_to_payment_service_selection_res(msg, &out->PaymentServiceSelectionRes);
    }
    if (strcmp(name, "PaymentDetailsReq") == 0) {
        init_iso2_PaymentDetailsReqType(&out->PaymentDetailsReq);
        out->PaymentDetailsReq_isUsed = 1;
        return json_to_payment_details_req(msg, &out->PaymentDetailsReq);
    }
    if (strcmp(name, "PaymentDetailsRes") == 0) {
        init_iso2_PaymentDetailsResType(&out->PaymentDetailsRes);
        out->PaymentDetailsRes_isUsed = 1;
        return json_to_payment_details_res(msg, &out->PaymentDetailsRes);
    }
    if (strcmp(name, "AuthorizationReq") == 0) {
        init_iso2_AuthorizationReqType(&out->AuthorizationReq);
        out->AuthorizationReq_isUsed = 1;
        return json_to_authorization_req(msg, &out->AuthorizationReq);
    }
    if (strcmp(name, "AuthorizationRes") == 0) {
        init_iso2_AuthorizationResType(&out->AuthorizationRes);
        out->AuthorizationRes_isUsed = 1;
        return json_to_authorization_res(msg, &out->AuthorizationRes);
    }
    if (strcmp(name, "ChargeParameterDiscoveryReq") == 0) {
        init_iso2_ChargeParameterDiscoveryReqType(&out->ChargeParameterDiscoveryReq);
        out->ChargeParameterDiscoveryReq_isUsed = 1;
        return json_to_charge_parameter_discovery_req(msg, &out->ChargeParameterDiscoveryReq);
    }
    if (strcmp(name, "ChargeParameterDiscoveryRes") == 0) {
        init_iso2_ChargeParameterDiscoveryResType(&out->ChargeParameterDiscoveryRes);
        out->ChargeParameterDiscoveryRes_isUsed = 1;
        return json_to_charge_parameter_discovery_res(msg, &out->ChargeParameterDiscoveryRes);
    }
    if (strcmp(name, "PowerDeliveryReq") == 0) {
        init_iso2_PowerDeliveryReqType(&out->PowerDeliveryReq);
        out->PowerDeliveryReq_isUsed = 1;
        return json_to_power_delivery_req(msg, &out->PowerDeliveryReq);
    }
    if (strcmp(name, "PowerDeliveryRes") == 0) {
        init_iso2_PowerDeliveryResType(&out->PowerDeliveryRes);
        out->PowerDeliveryRes_isUsed = 1;
        return json_to_power_delivery_res(msg, &out->PowerDeliveryRes);
    }
    if (strcmp(name, "ChargingStatusReq") == 0) {
        init_iso2_ChargingStatusReqType(&out->ChargingStatusReq);
        out->ChargingStatusReq_isUsed = 1;
        return json_to_charging_status_req(msg, &out->ChargingStatusReq);
    }
    if (strcmp(name, "ChargingStatusRes") == 0) {
        init_iso2_ChargingStatusResType(&out->ChargingStatusRes);
        out->ChargingStatusRes_isUsed = 1;
        return json_to_charging_status_res(msg, &out->ChargingStatusRes);
    }
    if (strcmp(name, "MeteringReceiptReq") == 0) {
        init_iso2_MeteringReceiptReqType(&out->MeteringReceiptReq);
        out->MeteringReceiptReq_isUsed = 1;
        return json_to_metering_receipt_req(msg, &out->MeteringReceiptReq);
    }
    if (strcmp(name, "MeteringReceiptRes") == 0) {
        init_iso2_MeteringReceiptResType(&out->MeteringReceiptRes);
        out->MeteringReceiptRes_isUsed = 1;
        return json_to_metering_receipt_res(msg, &out->MeteringReceiptRes);
    }
    if (strcmp(name, "CableCheckReq") == 0) {
        init_iso2_CableCheckReqType(&out->CableCheckReq);
        out->CableCheckReq_isUsed = 1;
        return json_to_cable_check_req(msg, &out->CableCheckReq);
    }
    if (strcmp(name, "CableCheckRes") == 0) {
        init_iso2_CableCheckResType(&out->CableCheckRes);
        out->CableCheckRes_isUsed = 1;
        return json_to_cable_check_res(msg, &out->CableCheckRes);
    }
    if (strcmp(name, "PreChargeReq") == 0) {
        init_iso2_PreChargeReqType(&out->PreChargeReq);
        out->PreChargeReq_isUsed = 1;
        return json_to_pre_charge_req(msg, &out->PreChargeReq);
    }
    if (strcmp(name, "PreChargeRes") == 0) {
        init_iso2_PreChargeResType(&out->PreChargeRes);
        out->PreChargeRes_isUsed = 1;
        return json_to_pre_charge_res(msg, &out->PreChargeRes);
    }
    if (strcmp(name, "CurrentDemandReq") == 0) {
        init_iso2_CurrentDemandReqType(&out->CurrentDemandReq);
        out->CurrentDemandReq_isUsed = 1;
        return json_to_current_demand_req(msg, &out->CurrentDemandReq);
    }
    if (strcmp(name, "CurrentDemandRes") == 0) {
        init_iso2_CurrentDemandResType(&out->CurrentDemandRes);
        out->CurrentDemandRes_isUsed = 1;
        return json_to_current_demand_res(msg, &out->CurrentDemandRes);
    }
    if (strcmp(name, "WeldingDetectionReq") == 0) {
        init_iso2_WeldingDetectionReqType(&out->WeldingDetectionReq);
        out->WeldingDetectionReq_isUsed = 1;
        return json_to_welding_detection_req(msg, &out->WeldingDetectionReq);
    }
    if (strcmp(name, "WeldingDetectionRes") == 0) {
        init_iso2_WeldingDetectionResType(&out->WeldingDetectionRes);
        out->WeldingDetectionRes_isUsed = 1;
        return json_to_welding_detection_res(msg, &out->WeldingDetectionRes);
    }
    if (strcmp(name, "SessionStopReq") == 0) {
        init_iso2_SessionStopReqType(&out->SessionStopReq);
        out->SessionStopReq_isUsed = 1;
        return json_to_session_stop_req(msg, &out->SessionStopReq);
    }
    if (strcmp(name, "SessionStopRes") == 0) {
        init_iso2_SessionStopResType(&out->SessionStopRes);
        out->SessionStopRes_isUsed = 1;
        return json_to_session_stop_res(msg, &out->SessionStopRes);
    }
    if (strcmp(name, "CertificateInstallationReq") == 0) {
        init_iso2_CertificateInstallationReqType(&out->CertificateInstallationReq);
        out->CertificateInstallationReq_isUsed = 1;
        return json_to_certificate_installation_req(msg, &out->CertificateInstallationReq);
    }
    if (strcmp(name, "CertificateInstallationRes") == 0) {
        init_iso2_CertificateInstallationResType(&out->CertificateInstallationRes);
        out->CertificateInstallationRes_isUsed = 1;
        return json_to_certificate_installation_res(msg, &out->CertificateInstallationRes);
    }
    if (strcmp(name, "CertificateUpdateReq") == 0) {
        init_iso2_CertificateUpdateReqType(&out->CertificateUpdateReq);
        out->CertificateUpdateReq_isUsed = 1;
        return json_to_certificate_update_req(msg, &out->CertificateUpdateReq);
    }
    if (strcmp(name, "CertificateUpdateRes") == 0) {
        init_iso2_CertificateUpdateResType(&out->CertificateUpdateRes);
        out->CertificateUpdateRes_isUsed = 1;
        return json_to_certificate_update_res(msg, &out->CertificateUpdateRes);
    }

    set_error("ISO 15118-2: unknown message '%s' in Body", name);
    return CBV2G_ERROR_UNKNOWN_MESSAGE;
}

static int generated(int failed) {
    return failed ? CBV2G_ERROR_JSON_GENERATE : CBV2G_SUCCESS;
}

static int body_to_json(const struct iso2_BodyType* in, cJSON* body) {
    cJSON* msg = NULL;

    if (in->SessionSetupReq_isUsed) {
        return generated(add_object(body, "SessionSetupReq", &msg) ||
                         session_setup_req_to_json(&in->SessionSetupReq, msg));
    }
    if (in->SessionSetupRes_isUsed) {
        return generated(add_object(body, "SessionSetupRes", &msg) ||
                         session_setup_res_to_json(&in->SessionSetupRes, msg));
    }
    if (in->ServiceDiscoveryReq_isUsed) {
        return generated(add_object(body, "ServiceDiscoveryReq", &msg) ||
                         service_discovery_req_to_json(&in->ServiceDiscoveryReq, msg));
    }
    if (in->ServiceDiscoveryRes_isUsed) {
        return generated(add_object(body, "ServiceDiscoveryRes", &msg) ||
                         service_discovery_res_to_json(&in->ServiceDiscoveryRes, msg));
    }
    if (in->ServiceDetailReq_isUsed) {
        return generated(add_object(body, "ServiceDetailReq", &msg) ||
                         service_detail_req_to_json(&in->ServiceDetailReq, msg));
    }
    if (in->ServiceDetailRes_isUsed) {
        return generated(add_object(body, "ServiceDetailRes", &msg) ||
                         service_detail_res_to_json(&in->ServiceDetailRes, msg));
    }
    if (in->PaymentServiceSelectionReq_isUsed) {
        return generated(add_object(body, "PaymentServiceSelectionReq", &msg) ||
                         payment_service_selection_req_to_json(&in->PaymentServiceSelectionReq, msg));
    }
    if (in->PaymentServiceSelectionRes_isUsed) {
        return generated(add_object(body, "PaymentServiceSelectionRes", &msg) ||
                         payment_service_selection_res_to_json(&in->PaymentServiceSelectionRes, msg));
    }
    if (in->PaymentDetailsReq_isUsed) {
        return generated(add_object(body, "PaymentDetailsReq", &msg) ||
                         payment_details_req_to_json(&in->PaymentDetailsReq, msg));
    }
    if (in->PaymentDetailsRes_isUsed) {
        return generated(add_object(body, "PaymentDetailsRes", &msg) ||
                         payment_details_res_to_json(&in->PaymentDetailsRes, msg));
    }
    if (in->AuthorizationReq_isUsed) {
        return generated(add_object(body, "AuthorizationReq", &msg) ||
                         authorization_req_to_json(&in->AuthorizationReq, msg));
    }
    if (in->AuthorizationRes_isUsed) {
        return generated(add_object(body, "AuthorizationRes", &msg) ||
                         authorization_res_to_json(&in->AuthorizationRes, msg));
    }
    if (in->ChargeParameterDiscoveryReq_isUsed) {
        return generated(add_object(body, "ChargeParameterDiscoveryReq", &msg) ||
                         charge_parameter_discovery_req_to_json(&in->ChargeParameterDiscoveryReq, msg));
    }
    if (in->ChargeParameterDiscoveryRes_isUsed) {
        return generated(add_object(body, "ChargeParameterDiscoveryRes", &msg) ||
                         charge_parameter_discovery_res_to_json(&in->ChargeParameterDiscoveryRes, msg));
    }
    if (in->PowerDeliveryReq_isUsed) {
        return generated(add_object(body, "PowerDeliveryReq", &msg) ||
                         power_delivery_req_to_json(&in->PowerDeliveryReq, msg));
    }
    if (in->PowerDeliveryRes_isUsed) {
        return generated(add_object(body, "PowerDeliveryRes", &msg) ||
                         power_delivery_res_to_json(&in->PowerDeliveryRes, msg));
    }
    if (in->ChargingStatusReq_isUsed) {
        return generated(add_object(body, "ChargingStatusReq", &msg) ||
                         charging_status_req_to_json(&in->ChargingStatusReq, msg));
    }
    if (in->ChargingStatusRes_isUsed) {
        return generated(add_object(body, "ChargingStatusRes", &msg) ||
                         charging_status_res_to_json(&in->ChargingStatusRes, msg));
    }
    if (in->MeteringReceiptReq_isUsed) {
        return generated(add_object(body, "MeteringReceiptReq", &msg) ||
                         metering_receipt_req_to_json(&in->MeteringReceiptReq, msg));
    }
    if (in->MeteringReceiptRes_isUsed) {
        return generated(add_object(body, "MeteringReceiptRes", &msg) ||
                         metering_receipt_res_to_json(&in->MeteringReceiptRes, msg));
    }
    if (in->CableCheckReq_isUsed) {
        return generated(add_object(body, "CableCheckReq", &msg) || cable_check_req_to_json(&in->CableCheckReq, msg));
    }
    if (in->CableCheckRes_isUsed) {
        return generated(add_object(body, "CableCheckRes", &msg) || cable_check_res_to_json(&in->CableCheckRes, msg));
    }
    if (in->PreChargeReq_isUsed) {
        return generated(add_object(body, "PreChargeReq", &msg) || pre_charge_req_to_json(&in->PreChargeReq, msg));
    }
    if (in->PreChargeRes_isUsed) {
        return generated(add_object(body, "PreChargeRes", &msg) || pre_charge_res_to_json(&in->PreChargeRes, msg));
    }
    if (in->CurrentDemandReq_isUsed) {
        return generated(add_object(body, "CurrentDemandReq", &msg) ||
                         current_demand_req_to_json(&in->CurrentDemandReq, msg));
    }
    if (in->CurrentDemandRes_isUsed) {
        return generated(add_object(body, "CurrentDemandRes", &msg) ||
                         current_demand_res_to_json(&in->CurrentDemandRes, msg));
    }
    if (in->WeldingDetectionReq_isUsed) {
        return generated(add_object(body, "WeldingDetectionReq", &msg) ||
                         welding_detection_req_to_json(&in->WeldingDetectionReq, msg));
    }
    if (in->WeldingDetectionRes_isUsed) {
        return generated(add_object(body, "WeldingDetectionRes", &msg) ||
                         welding_detection_res_to_json(&in->WeldingDetectionRes, msg));
    }
    if (in->SessionStopReq_isUsed) {
        return generated(add_object(body, "SessionStopReq", &msg) ||
                         session_stop_req_to_json(&in->SessionStopReq, msg));
    }
    if (in->SessionStopRes_isUsed) {
        return generated(add_object(body, "SessionStopRes", &msg) ||
                         session_stop_res_to_json(&in->SessionStopRes, msg));
    }
    if (in->CertificateInstallationReq_isUsed) {
        return generated(add_object(body, "CertificateInstallationReq", &msg) ||
                         certificate_installation_req_to_json(&in->CertificateInstallationReq, msg));
    }
    if (in->CertificateInstallationRes_isUsed) {
        return generated(add_object(body, "CertificateInstallationRes", &msg) ||
                         certificate_installation_res_to_json(&in->CertificateInstallationRes, msg));
    }
    if (in->CertificateUpdateReq_isUsed) {
        return generated(add_object(body, "CertificateUpdateReq", &msg) ||
                         certificate_update_req_to_json(&in->CertificateUpdateReq, msg));
    }
    if (in->CertificateUpdateRes_isUsed) {
        return generated(add_object(body, "CertificateUpdateRes", &msg) ||
                         certificate_update_res_to_json(&in->CertificateUpdateRes, msg));
    }

    set_error("ISO 15118-2: decoded Body holds no supported message");
    return CBV2G_ERROR_JSON_GENERATE;
}

/* ============== Fragments ============== */

static int json_to_fragment(const char* name, cJSON* json, struct iso2_exiFragment* fragment) {
    if (strcmp(name, "AuthorizationReq") == 0) {
        init_iso2_AuthorizationReqType(&fragment->AuthorizationReq);
        fragment->AuthorizationReq_isUsed = 1;
        return json_to_authorization_req(json, &fragment->AuthorizationReq);
    }
    if (strcmp(name, "MeteringReceiptReq") == 0) {
        init_iso2_MeteringReceiptReqType(&fragment->MeteringReceiptReq);
        fragment->MeteringReceiptReq_isUsed = 1;
        return json_to_metering_receipt_req(json, &fragment->MeteringReceiptReq);
    }
    if (strcmp(name, "CertificateInstallationReq") == 0) {
        init_iso2_CertificateInstallationReqType(&fragment->CertificateInstallationReq);
        fragment->CertificateInstallationReq_isUsed = 1;
        return json_to_certificate_installation_req(json, &fragment->CertificateInstallationReq);
    }
    if (strcmp(name, "CertificateUpdateReq") == 0) {
        init_iso2_CertificateUpdateReqType(&fragment->CertificateUpdateReq);
        fragment->CertificateUpdateReq_isUsed = 1;
        return json_to_certificate_update_req(json, &fragment->CertificateUpdateReq);
    }
    if (strcmp(name, "ContractSignatureCertChain") == 0) {
        init_iso2_CertificateChainType(&fragment->ContractSignatureCertChain);
        fragment->ContractSignatureCertChain_isUsed = 1;
        return json_to_certificate_chain(json, &fragment->ContractSignatureCertChain);
    }
    if (strcmp(name, "ContractSignatureEncryptedPrivateKey") == 0) {
        struct iso2_ContractSignatureEncryptedPrivateKeyType* key = &fragment->ContractSignatureEncryptedPrivateKey;
        init_iso2_ContractSignatureEncryptedPrivateKeyType(key);
        fragment->ContractSignatureEncryptedPrivateKey_isUsed = 1;
        return json_to_id_and_bytes(json, key->Id.characters, &key->Id.charactersLen, key->CONTENT.bytes,
                                    &key->CONTENT.bytesLen, iso2_ContractSignatureEncryptedPrivateKeyType_BYTES_SIZE);
    }
    if (strcmp(name, "DHpublickey") == 0) {
        struct iso2_DiffieHellmanPublickeyType* key = &fragment->DHpublickey;
        init_iso2_DiffieHellmanPublickeyType(key);
        fragment->DHpublickey_isUsed = 1;
        return json_to_id_and_bytes(json, key->Id.characters, &key->Id.charactersLen, key->CONTENT.bytes,
                                    &key->CONTENT.bytesLen, iso2_DiffieHellmanPublickeyType_BYTES_SIZE);
    }
    if (strcmp(name, "eMAID") == 0) {
        struct iso2_eMAIDElementFragment* emaid = &fragment->eMAID;
        fragment->eMAID_isUsed = 1;
        emaid->Id_isUsed = 1;
        emaid->CONTENT_isUsed = 1;
        if (read_string(json, "Id", emaid->Id.characters, &emaid->Id.charactersLen,
                        iso2_eMAIDElementFragment_Id_CHARACTER_SIZE) ||
            read_string(json, "value", emaid->CONTENT.characters, &emaid->CONTENT.charactersLen,
                        iso2_eMAIDElementFragment_CONTENT_CHARACTER_SIZE)) {
            return CBV2G_ERROR_JSON_PARSE;
        }
        return CBV2G_SUCCESS;
    }
    if (strcmp(name, "SalesTariff") == 0) {
        init_iso2_SalesTariffType(&fragment->SalesTariff);
        fragment->SalesTariff_isUsed = 1;
        return json_to_sales_tariff(json, &fragment->SalesTariff);
    }
    if (strcmp(name, "SignedInfo") == 0) {
        init_iso2_SignedInfoType(&fragment->SignedInfo);
        fragment->SignedInfo_isUsed = 1;
        return json_to_signed_info(json, &fragment->SignedInfo);
    }
    set_error("ISO 15118-2: '%s' is neither V2G_Message nor a supported EXI fragment", name);
    return CBV2G_ERROR_UNKNOWN_MESSAGE;
}

/* ============== Entry points ============== */

/* Values that a JSON number in cJSON, a double, cannot hold exactly. */
static const char* const exact_integer_keys[] = {"\"X509SerialNumber\"", "\"MeterReading\"", "\"TMeter\"",
                                                 "\"EVSETimeStamp\""};

static size_t exact_integer_key_at(const char* json) {
    for (size_t i = 0; i < COUNT_OF(exact_integer_keys); i++) {
        const size_t len = strlen(exact_integer_keys[i]);
        if (strncmp(json, exact_integer_keys[i], len) == 0) {
            return len;
        }
    }
    return 0;
}

/*
 * Copies `json` for cJSON_Parse. A number given for one of exact_integer_keys
 * becomes a string of the same characters, so that cJSON does not round it.
 * A \u0000 escape is refused: cJSON would end the string there. The caller
 * frees *out with cJSON_free.
 */
static int prepare_json(const char* json, char** out) {
    const size_t len = strlen(json);
    /* The shortest key, "TMeter" in quotes, is 8 characters; each match adds two quotes. */
    char* copy = cJSON_malloc(len + 2 * (len / 8 + 1) + 1);
    if (copy == NULL) {
        set_error("ISO 15118-2: out of memory");
        return CBV2G_ERROR_INTERNAL;
    }
    size_t in = 0;
    size_t o = 0;
    while (in < len) {
        if (json[in] != '"') {
            copy[o++] = json[in++];
            continue;
        }
        const size_t key_len = exact_integer_key_at(json + in);
        if (key_len > 0) {
            memcpy(copy + o, json + in, key_len);
            in += key_len;
            o += key_len;
            while (json[in] == ' ' || json[in] == '\t' || json[in] == '\n' || json[in] == '\r' || json[in] == ':') {
                copy[o++] = json[in++];
            }
            if (json[in] == '-' || (json[in] >= '0' && json[in] <= '9')) {
                copy[o++] = '"';
                while (json[in] == '-' || json[in] == '+' || json[in] == '.' || json[in] == 'e' || json[in] == 'E' ||
                       (json[in] >= '0' && json[in] <= '9')) {
                    copy[o++] = json[in++];
                }
                copy[o++] = '"';
            }
            continue;
        }
        copy[o++] = json[in++];
        while (in < len && json[in] != '"') {
            if (json[in] == '\\' && strncmp(json + in, "\\u0000", 6) == 0) {
                cJSON_free(copy);
                set_error("ISO 15118-2: strings must not contain NUL characters");
                return CBV2G_ERROR_JSON_PARSE;
            }
            if (json[in] == '\\' && in + 1 < len) {
                copy[o++] = json[in++];
            }
            copy[o++] = json[in++];
        }
        if (in < len) {
            copy[o++] = json[in++];
        }
    }
    copy[o] = '\0';
    *out = copy;
    return CBV2G_SUCCESS;
}

/* Parses `json_str` into a root object that holds exactly one member. */
static int parse_single_member(const char* json_str, cJSON** root) {
    char* prepared = NULL;
    const int rc = prepare_json(json_str, &prepared);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    *root = cJSON_Parse(prepared);
    if (*root == NULL) {
        /* The error position points into `prepared`, so report before releasing it. */
        set_error("Failed to parse JSON: %s", cJSON_GetErrorPtr());
        cJSON_free(prepared);
        return CBV2G_ERROR_JSON_PARSE;
    }
    cJSON_free(prepared);
    if (!cJSON_IsObject(*root) || cJSON_GetArraySize(*root) != 1 || !cJSON_IsObject((*root)->child)) {
        set_error("ISO 15118-2: JSON must be an object holding exactly one element object");
        cJSON_Delete(*root);
        *root = NULL;
        return CBV2G_ERROR_JSON_PARSE;
    }
    return CBV2G_SUCCESS;
}

static int json_to_document(cJSON* v2g_message, struct iso2_exiDocument* doc) {
    cJSON* header = NULL;
    cJSON* body = NULL;
    if (get_object(v2g_message, "Header", &header) || json_to_header(header, &doc->V2G_Message.Header) ||
        get_object(v2g_message, "Body", &body)) {
        return CBV2G_ERROR_JSON_PARSE;
    }
    init_iso2_BodyType(&doc->V2G_Message.Body);
    return json_to_body(body, &doc->V2G_Message.Body);
}

static int finish_encoding(int exi_result, exi_bitstream_t* stream, size_t* out_len) {
    if (exi_result != 0) {
        set_error("EXI encoding failed with error code: %d", exi_result);
        return CBV2G_ERROR_ENCODING_FAILED;
    }
    *out_len = exi_bitstream_get_length(stream);
    return CBV2G_SUCCESS;
}

/*
 * Encodes a V2G_Message document or, for any other single top-level element,
 * an ISO 15118-2 EXI fragment.
 */
int iso2_encode(const char* json_str, uint8_t* out, size_t out_size, size_t* out_len) {
    cJSON* root = NULL;
    int rc = parse_single_member(json_str, &root);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    cJSON* element = root->child;
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, out, out_size, 0, NULL);

    if (strcmp(element->string, "V2G_Message") == 0) {
        struct iso2_exiDocument doc;
        memset(&doc, 0, sizeof(doc));
        rc = json_to_document(element, &doc);
        if (rc == CBV2G_SUCCESS) {
            rc = finish_encoding(encode_iso2_exiDocument(&stream, &doc), &stream, out_len);
        }
    } else {
        struct iso2_exiFragment fragment;
        memset(&fragment, 0, sizeof(fragment));
        init_iso2_exiFragment(&fragment);
        rc = json_to_fragment(element->string, element, &fragment);
        if (rc == CBV2G_SUCCESS) {
            rc = finish_encoding(encode_iso2_exiFragment(&stream, &fragment), &stream, out_len);
        }
    }
    cJSON_Delete(root);
    return rc;
}

/* Bounded write of the serialised JSON to the caller's buffer (CWE-120 / CWE-126). */
static int write_json(cJSON* root, char* out, size_t out_size) {
    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        set_error("Failed to serialize JSON");
        return CBV2G_ERROR_JSON_GENERATE;
    }
    const int written = snprintf(out, out_size, "%s", json_str);
    cJSON_free(json_str);
    if (written < 0 || (size_t)written >= out_size) {
        set_error("Output buffer too small: need %d, have %zu", written + 1, out_size);
        return CBV2G_ERROR_BUFFER_TOO_SMALL;
    }
    return CBV2G_SUCCESS;
}

int iso2_decode(const uint8_t* exi, size_t exi_len, char* out, size_t out_size) {
    struct iso2_exiDocument doc;
    memset(&doc, 0, sizeof(doc));
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, (uint8_t*)exi, exi_len, 0, NULL);

    const int exi_result = decode_iso2_exiDocument(&stream, &doc);
    if (exi_result != 0) {
        set_error("EXI decoding failed with error code: %d", exi_result);
        return CBV2G_ERROR_DECODING_FAILED;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* v2g_message = NULL;
    cJSON* header = NULL;
    cJSON* body = NULL;
    int rc = root == NULL ? output_error("V2G_Message") : CBV2G_SUCCESS;
    if (rc == CBV2G_SUCCESS) {
        rc = add_object(root, "V2G_Message", &v2g_message);
    }
    if (rc == CBV2G_SUCCESS) {
        rc = add_object(v2g_message, "Header", &header);
    }
    if (rc == CBV2G_SUCCESS) {
        rc = header_to_json(&doc.V2G_Message.Header, header);
    }
    if (rc == CBV2G_SUCCESS) {
        rc = add_object(v2g_message, "Body", &body);
    }
    if (rc == CBV2G_SUCCESS) {
        rc = body_to_json(&doc.V2G_Message.Body, body);
    }
    if (rc == CBV2G_SUCCESS) {
        rc = write_json(root, out, out_size);
    }
    cJSON_Delete(root);
    return rc;
}

/*
 * SignedInfo is canonicalised with the fragment grammar of the xmldsig schema
 * alone (ISO 15118-2 Annex J); its signature verifies only over those bytes.
 */
int iso2_xmldsig_encode(const char* json_str, uint8_t* out, size_t out_size, size_t* out_len) {
    cJSON* root = NULL;
    int rc = parse_single_member(json_str, &root);
    if (rc != CBV2G_SUCCESS) {
        return rc;
    }
    cJSON* element = root->child;
    if (strcmp(element->string, "SignedInfo") != 0) {
        set_error("ISO 15118-2: '%s' is not a supported xmldsig fragment", element->string);
        cJSON_Delete(root);
        return CBV2G_ERROR_UNKNOWN_MESSAGE;
    }

    struct iso2_xmldsigFragment fragment;
    memset(&fragment, 0, sizeof(fragment));
    init_iso2_xmldsigFragment(&fragment);
    init_iso2_SignedInfoType(&fragment.SignedInfo);
    fragment.SignedInfo_isUsed = 1;
    rc = json_to_signed_info(element, &fragment.SignedInfo);
    if (rc == CBV2G_SUCCESS) {
        exi_bitstream_t stream;
        exi_bitstream_init(&stream, out, out_size, 0, NULL);
        rc = finish_encoding(encode_iso2_xmldsigFragment(&stream, &fragment), &stream, out_len);
    }
    cJSON_Delete(root);
    return rc;
}

int iso2_xmldsig_decode(const uint8_t* exi, size_t exi_len, char* out, size_t out_size) {
    struct iso2_xmldsigFragment fragment;
    memset(&fragment, 0, sizeof(fragment));
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, (uint8_t*)exi, exi_len, 0, NULL);

    const int exi_result = decode_iso2_xmldsigFragment(&stream, &fragment);
    if (exi_result != 0) {
        set_error("EXI decoding failed with error code: %d", exi_result);
        return CBV2G_ERROR_DECODING_FAILED;
    }
    if (!fragment.SignedInfo_isUsed) {
        set_error("ISO 15118-2: decoded xmldsig fragment is not a SignedInfo");
        return CBV2G_ERROR_UNKNOWN_MESSAGE;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* signed_info = NULL;
    int rc = root == NULL ? output_error("SignedInfo") : add_object(root, "SignedInfo", &signed_info);
    if (rc == CBV2G_SUCCESS) {
        rc = signed_info_to_json(&fragment.SignedInfo, signed_info);
    }
    if (rc == CBV2G_SUCCESS) {
        rc = write_json(root, out, out_size);
    }
    cJSON_Delete(root);
    return rc;
}
