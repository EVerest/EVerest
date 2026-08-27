#!/bin/sh
# Generates the PKI used by the libopcp unit tests (and the mock OPCP server):
#   V2G root (P-256) -> CPO sub-CA 1 -> CPO sub-CA 2 -> SECC leaf (P-256, ISO 15118-2)
#   V2G root (P-256) -> CPO sub-CA 1 -> CPO sub-CA 2 -> SECC leaf (P-521, ISO 15118-20)
# plus the CSRs of both leafs and the PKCS#7 blobs as the EST endpoints return them.
set -e
OUT=${1:-pki}
rm -rf "$OUT"
mkdir -p "$OUT"
cd "$OUT"

ext() {
cat > "$1.cnf" <<CNF
[ req ]
distinguished_name = dn
prompt = no
[ dn ]
C = DE
O = EVerest Test
CN = $2
DC = CPO
[ ca ]
basicConstraints = critical, CA:TRUE, pathlen:$3
keyUsage = critical, keyCertSign, cRLSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
[ leaf ]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature, keyAgreement
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
CNF
}

# Root
ext root "V2G Root CA Test" 2
openssl ecparam -name prime256v1 -genkey -noout -out root.key
openssl req -new -x509 -key root.key -sha256 -days 3650 -config root.cnf -extensions ca -out root.pem
# Sub CA 1
ext sub1 "CPO Sub1 CA Test" 1
openssl ecparam -name prime256v1 -genkey -noout -out sub1.key
openssl req -new -key sub1.key -sha256 -config sub1.cnf -out sub1.csr
openssl x509 -req -in sub1.csr -CA root.pem -CAkey root.key -CAcreateserial -sha256 -days 3000 \
    -extfile sub1.cnf -extensions ca -out sub1.pem
# Sub CA 2
ext sub2 "CPO Sub2 CA Test" 0
openssl ecparam -name prime256v1 -genkey -noout -out sub2.key
openssl req -new -key sub2.key -sha256 -config sub2.cnf -out sub2.csr
openssl x509 -req -in sub2.csr -CA sub1.pem -CAkey sub1.key -CAcreateserial -sha256 -days 2000 \
    -extfile sub2.cnf -extensions ca -out sub2.pem

# ISO 15118-2 leaf (P-256)
ext leaf2 "DE*PNX*E12345*1" 0
openssl ecparam -name prime256v1 -genkey -noout -out leaf2.key
openssl req -new -key leaf2.key -sha256 -config leaf2.cnf -out leaf2.csr
openssl x509 -req -in leaf2.csr -CA sub2.pem -CAkey sub2.key -CAcreateserial -sha256 -days 365 \
    -extfile leaf2.cnf -extensions leaf -out leaf2.pem

# ISO 15118-20 leaf (P-521)
ext leaf20 "DE-PNX-S-00000000000000000000000000000001-1" 0
openssl ecparam -name secp521r1 -genkey -noout -out leaf20.key
openssl req -new -key leaf20.key -sha512 -config leaf20.cnf -out leaf20.csr
openssl x509 -req -in leaf20.csr -CA sub2.pem -CAkey sub2.key -CAcreateserial -sha512 -days 365 \
    -extfile leaf20.cnf -extensions leaf -out leaf20.pem

# An unrelated leaf whose key does not match any CSR
ext other "DE*PNX*E99999" 0
openssl ecparam -name prime256v1 -genkey -noout -out other.key
openssl req -new -key other.key -sha256 -config other.cnf -out other.csr
openssl x509 -req -in other.csr -CA sub2.pem -CAkey sub2.key -CAcreateserial -sha256 -days 365 \
    -extfile other.cnf -extensions leaf -out other.pem

# PKCS#7 blobs, base64 (single line) as returned by EST
p7() { openssl crl2pkcs7 -nocrl "$@" -outform DER | openssl base64 -A; }
p7 -certfile leaf2.pem > leaf2.p7b64
p7 -certfile leaf20.pem > leaf20.p7b64
p7 -certfile sub2.pem -certfile sub1.pem > cacerts.p7b64
p7 -certfile sub2.pem -certfile sub1.pem -certfile root.pem > cacerts_with_root.p7b64
p7 -certfile leaf2.pem -certfile sub2.pem -certfile sub1.pem > leaf2_full.p7b64
openssl x509 -in root.pem -outform DER | openssl base64 -A > root.derb64
