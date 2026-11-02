# cbv2g_json_wrapper

A native C shared library that wraps [libcbv2g](https://github.com/EVerest/libcbv2g)
with a JSON-based API for EXI encode/decode.

The wrapper is designed to replace the Java-based EXIficient codec used by the
EVerest EV-side simulation in [Josev](https://github.com/EVerest/ext-switchev-iso15118).
The intended consumer is `iso15118.shared.cbv2g_exi_codec.Cbv2gEXICodec`, which
loads `libcbv2g_json_wrapper.so` via `ctypes`.

See RFC [EVerest/ext-switchev-iso15118#58](https://github.com/EVerest/ext-switchev-iso15118/issues/58).

## Public API

```c
int cbv2g_encode(const char* json_message,
                 const char* namespace,
                 uint8_t* output_buffer,
                 size_t buffer_size,
                 size_t* output_length);

int cbv2g_decode(const uint8_t* exi_data,
                 size_t exi_length,
                 const char* namespace,
                 char* output_json,
                 size_t buffer_size);

const char* cbv2g_get_version(void);
const char* cbv2g_get_last_error(void);
void cbv2g_clear_error(void);
```

The `namespace` argument identifies the V2G schema. Supported namespaces are
listed in `include/cbv2g_json_wrapper.h`.

## Supported protocols

| Protocol | Namespace | Status |
|----------|-----------|--------|
| App Handshake (SAP) | `urn:iso:15118:2:2010:AppProtocol` | Supported |
| DIN 70121 | `urn:din:70121:2012:MsgDef` | Supported |
| ISO 15118-2 (incl. PnC) | `urn:iso:15118:2:2013:MsgDef` | Supported |
| xmldsig `SignedInfo` (PnC signatures) | `http://www.w3.org/2000/09/xmldsig#` | Supported |

ISO 15118-20 support is planned as a follow-up PR.

### ISO 15118-2

The JSON follows the shape Josev exchanges with its Java codec. Elements and
attributes keep their XSD names, enumerations their XSD literals, repeated
elements are arrays, hexBinary values are hex strings (decoded in upper case)
and base64Binary values padded base64. Simple content that carries an `Id`
attribute is an object with `Id` and `value`, as in `SignatureValue`, `eMAID`,
`DHpublickey` and `ContractSignatureEncryptedPrivateKey`. A substitution group
appears as its concrete member, e.g. `AC_EVSEStatus` or `DC_EVChargeParameter`.

With the `MsgDef` namespace, a JSON object whose single key is `V2G_Message`
is a complete message. Any other single key encodes that element as an
ISO 15118-2 EXI fragment, which is what signature digests are computed over:
`AuthorizationReq`, `MeteringReceiptReq`, `CertificateInstallationReq`,
`CertificateUpdateReq`, `ContractSignatureCertChain`,
`ContractSignatureEncryptedPrivateKey`, `DHpublickey`, `eMAID`, `SalesTariff`
and `SignedInfo`.

The signature itself is computed over `SignedInfo` coded in the fragment
grammar of the xmldsig schema (ISO 15118-2 Annex J), so `{"SignedInfo": ...}`
is encoded and decoded with the xmldsig namespace.

Input is validated, not coerced. A missing mandatory element (the `Header`
and its `SessionID` included), an enumeration given as anything but one of its
literals, a repeated element given as anything but an array, a string or byte
value that does not fit, malformed or non-canonical hex or base64, or an integer
outside the range of its EXI field is reported as `CBV2G_ERROR_JSON_PARSE` with
a message in `cbv2g_get_last_error()`. Members the schema does not know are
ignored.

`X509SerialNumber` (up to 20 octets), `MeterReading`, `TMeter` and
`EVSETimeStamp` are read and written as exact decimal digits, beyond the
precision of a JSON number in cJSON.

Known limitations:

- A string holding NUL characters decodes to `\u0000` escapes, but encoding
  refuses such a string, because cJSON ends strings at the first NUL.
- `KeyInfo` and `Object` in a `Signature`, and `HMACOutputLength` in a
  `SignatureMethod`, are not supported. ISO 15118-2 does not use them.
- Array sizes are those of libcbv2g, which are smaller than the schema allows
  for some elements, e.g. 12 `PMaxScheduleEntry` per schedule.

## Build

### As part of everest-core (default)

The wrapper is built as part of EVerest's `applications/` tree when
`-DEVEREST_BUILD_APPLICATIONS=ON` is set (the default). It links against
the in-tree `everest::cbv2g::*` targets exported by `lib/everest/cbv2g`, so no
external dependency setup is required.

## Tests

Tests use GoogleTest and are built with the rest of the unit tests when
`-DBUILD_TESTING=ON` is set:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target cbv2g_test_apphand cbv2g_test_din cbv2g_test_iso2
ctest --test-dir build -R cbv2g_test_ --output-on-failure
```

## Third-party

- `third_party/cJSON/` — cJSON 1.7.19 by Dave Gamble (MIT). See its bundled
  `LICENSE` file. Used internally for JSON parsing and serialisation.
