/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright chargebyte GmbH and Contributors to EVerest
 */

/*****************************************************
 *
 * @author
 * @version
 *
 * The Code is generated! Changes may be overwritten.
 *
 *****************************************************/

/**
  * @file exi_basetypes.c
  * @brief Description goes here
  *
  **/

#include "cbv2g/common/exi_error_codes.h"
#include "cbv2g/common/exi_basetypes.h"
#include "cbv2g/common/exi_bitstream.h"


/*****************************************************************************
 * interface functions
 *****************************************************************************/
static size_t exi_basetypes_magnitude_octets(uint64_t magnitude)
{
    size_t count = 1;
    while ((magnitude >>= 7u) != 0)
    {
        count++;
    }
    return count;
}

static void exi_basetypes_write_magnitude(exi_unsigned_t* data, uint64_t magnitude)
{
    data->octets_count = 0;
    for (uint64_t remaining = magnitude; remaining != 0; remaining >>= 8u)
    {
        data->octets_count++;
    }
    for (size_t n = data->octets_count; n > 0; n--)
    {
        data->octets[n - 1] = (uint8_t)magnitude;
        magnitude >>= 8u;
    }
}

int exi_basetypes_convert_to_unsigned(exi_unsigned_t* exi_unsigned, uint32_t value, size_t max_octets)
{
    exi_basetypes_write_magnitude(exi_unsigned, value);
    return (exi_basetypes_magnitude_octets(value) <= max_octets) ?
        EXI_ERROR__NO_ERROR : EXI_ERROR__OCTET_COUNT_LARGER_THAN_TYPE_SUPPORTS;
}

int exi_basetypes_convert_64_to_unsigned(exi_unsigned_t* exi_unsigned, uint64_t value)
{
    exi_basetypes_write_magnitude(exi_unsigned, value);
    return EXI_ERROR__NO_ERROR;
}

static uint64_t exi_basetypes_read_magnitude(const exi_unsigned_t* data)
{
    size_t first = 0;
    while (first < data->octets_count && data->octets[first] == 0)
    {
        first++;
    }
    if (data->octets_count - first > sizeof(uint64_t))
    {
        // Saturate to a magnitude that neither signed destination can hold.
        return UINT64_MAX;
    }
    uint64_t magnitude = 0;
    for (size_t n = first; n < data->octets_count; n++)
    {
        magnitude = (magnitude << 8u) | data->octets[n];
    }
    return magnitude;
}

int exi_basetypes_convert_to_signed(exi_signed_t* exi_signed, int32_t value, size_t max_octets)
{
    uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) : (uint64_t)value;
    exi_signed->is_negative = value < 0;
    exi_basetypes_write_magnitude(&exi_signed->data, magnitude);
    return (exi_basetypes_magnitude_octets(magnitude) <= max_octets) ?
        EXI_ERROR__NO_ERROR : EXI_ERROR__OCTET_COUNT_LARGER_THAN_TYPE_SUPPORTS;
}

int exi_basetypes_convert_64_to_signed(exi_signed_t* exi_signed, int64_t value)
{
    uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) : (uint64_t)value;
    exi_signed->is_negative = value < 0;
    exi_basetypes_write_magnitude(&exi_signed->data, magnitude);
    return EXI_ERROR__NO_ERROR;
}

int exi_basetypes_convert_from_unsigned(const exi_unsigned_t* exi_unsigned, uint32_t* value, size_t max_octets)
{
    uint64_t magnitude;
    int error = exi_basetypes_convert_64_from_unsigned(exi_unsigned, &magnitude);
    if (error != EXI_ERROR__NO_ERROR)
    {
        return error;
    }
    if (exi_basetypes_magnitude_octets(magnitude) > max_octets)
    {
        return EXI_ERROR__OCTET_COUNT_LARGER_THAN_TYPE_SUPPORTS;
    }
    if (magnitude > UINT32_MAX)
    {
        return EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION;
    }
    *value = (uint32_t)magnitude;
    return EXI_ERROR__NO_ERROR;
}

int exi_basetypes_convert_64_from_unsigned(const exi_unsigned_t* exi_unsigned, uint64_t* value)
{
    size_t first = 0;
    while (first < exi_unsigned->octets_count && exi_unsigned->octets[first] == 0)
    {
        first++;
    }
    if (exi_unsigned->octets_count - first > sizeof(uint64_t))
    {
        return EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION;
    }
    *value = exi_basetypes_read_magnitude(exi_unsigned);
    return EXI_ERROR__NO_ERROR;
}

int exi_basetypes_convert_from_signed(const exi_signed_t* exi_signed, int32_t* value, size_t max_octets)
{
    uint64_t magnitude = exi_basetypes_read_magnitude(&exi_signed->data);
    if (exi_basetypes_magnitude_octets(magnitude) > max_octets)
    {
        return EXI_ERROR__OCTET_COUNT_LARGER_THAN_TYPE_SUPPORTS;
    }
    if (magnitude > INT32_MAX)
    {
        return EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION;
    }
    *value = exi_signed->is_negative ? -1 - (int32_t)magnitude : (int32_t)magnitude;
    return EXI_ERROR__NO_ERROR;
}

int exi_basetypes_convert_64_from_signed(const exi_signed_t* exi_signed, int64_t* value)
{
    uint64_t magnitude = exi_basetypes_read_magnitude(&exi_signed->data);
    if (magnitude > INT64_MAX)
    {
        return EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION;
    }
    *value = exi_signed->is_negative ? -1 - (int64_t)magnitude : (int64_t)magnitude;
    return EXI_ERROR__NO_ERROR;
}

static void _reverse_array(uint8_t* data, size_t data_size)
{
    if (!data_size)
    {
        return;
    }

    size_t i = 0;
    size_t j = data_size - 1;
    while (i < j) {
        const uint8_t temp = data[i];
        data[i] = data[j];
        data[j] = temp;
        i++;
        j--;
    }
}

int exi_basetypes_convert_bytes_from_unsigned(const exi_unsigned_t* exi_unsigned, uint8_t* data, size_t* data_len, size_t data_size)
{
    // raw EXI 7/8 integer byte stream with flags (exi_unsigned) to API exi_unsigned_t representation (data, data_len)
    const uint8_t* current_octet = exi_unsigned->octets;
    uint16_t temp = 0;
    *data_len = 0;
    size_t total_offset = 0;

    for (size_t n = 0; n < exi_unsigned->octets_count; n++) {
        temp += ((uint16_t)(*current_octet & EXI_BASETYPES_OCTET_SEQ_VALUE_MASK) << total_offset);
        total_offset += 7;
        if (total_offset >= 8) {
            if (*data_len >= data_size) {
                return EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION;
            }
            total_offset -= 8;
            data[*data_len] = temp & 0xFF;
            (*data_len)++;
            temp >>= 8;
        }
        current_octet++;
    }
    if (total_offset != 0 && (temp & 0xFF) != 0) {
        if (*data_len >= data_size) {
            return EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION;
        }
        data[*data_len] = temp & 0xFF;
        (*data_len)++;
    }
    _reverse_array(data, *data_len);
    return EXI_ERROR__NO_ERROR;
}

int exi_basetypes_convert_bytes_to_unsigned(exi_unsigned_t* exi_unsigned, const uint8_t* data, size_t data_len)
{
    // API exi_unsigned_t representation (data, data_len) to raw EXI 7/8 integer byte stream with flags (exi_unsigned)
    uint8_t *current_octet = &exi_unsigned->octets[0];
    uint16_t dummy = 0;
    uint8_t dummy_count = 0;
    exi_unsigned->octets_count = 0;

    // first, determine the number of relevant bits (or octets), to have a termination criterion
    size_t bytenum;
    for (bytenum = 0; bytenum < data_len; bytenum++) {
        if (data[bytenum] != 0)
            break;
    }
    if (bytenum == data_len) {
        // special case: all zeros
        *current_octet = 0;
        exi_unsigned->octets_count = 1;
        return EXI_ERROR__NO_ERROR;
    }
    // bytenum is now index of big-endian first relevant byte
    // number of total input relevant bits t is (data_len - bytenum - 1) * 8 plus number of relevant bits in data[bytenum]
    uint8_t byte = data[bytenum];
    int bits_in_byte = 0;
    while (byte) {
        bits_in_byte++;
        byte >>= 1;
    }

    const int total_relevant_input_bits = (data_len - bytenum - 1) * 8 + bits_in_byte;
    const size_t exi_expected_octets_count = (total_relevant_input_bits + 6) / 7; // integer division ceil

    size_t incount = 0;
    for (size_t outcount = 0; outcount < exi_expected_octets_count; outcount++) {
        if (dummy_count < 7) {
            // fill dummy when more flushable bits are needed
            dummy |= (data[data_len - incount - 1] << dummy_count);
            dummy_count += 8;
            incount++;
        }
        *current_octet = (uint8_t)(dummy & EXI_BASETYPES_OCTET_SEQ_VALUE_MASK);
        exi_unsigned->octets_count++;
        if (exi_unsigned->octets_count < exi_expected_octets_count) {
            *current_octet |= EXI_BASETYPES_OCTET_SEQ_FLAG_MASK;
        } else {
            break;
        }
        current_octet++;
        dummy >>= 7u;
        dummy_count -= 7;
    }

    return EXI_ERROR__NO_ERROR;
}


