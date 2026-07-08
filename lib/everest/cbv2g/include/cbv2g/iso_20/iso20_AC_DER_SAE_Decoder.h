/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2022 - 2023 chargebyte GmbH
 * Copyright (C) 2022 - 2023 Contributors to EVerest
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
  * @file iso20_AC_DER_SAE_Decoder.h
  * @brief Description goes here
  *
  **/

#ifndef ISO20_AC_DER_SAE_DECODER_H
#define ISO20_AC_DER_SAE_DECODER_H

#ifdef __cplusplus
extern "C" {
#endif


#include "cbv2g/common/exi_bitstream.h"
#include "cbv2g/iso_20/iso20_AC_DER_SAE_Datatypes.h"


// main function for decoding
int decode_iso20_ac_der_sae_exiDocument(exi_bitstream_t* stream, struct iso20_ac_der_sae_exiDocument* exiDoc);
// decoding function for fragment
int decode_iso20_ac_der_sae_exiFragment(exi_bitstream_t* stream, struct iso20_ac_der_sae_exiFragment* exiFrag);
// decoding function for xmldsig fragment
int decode_iso20_ac_der_sae_xmldsigFragment(exi_bitstream_t* stream, struct iso20_ac_der_sae_xmldsigFragment* xmldsigFrag);

#ifdef __cplusplus
}
#endif

#endif /* ISO20_AC_DER_SAE_DECODER_H */

