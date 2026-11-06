#!/bin/sh
# Regenerates the test certificates. They are for the tests only and protect nothing: the CA keys are deleted
# once the leaves are signed, and every leaf key is committed.
set -e
cd "$(dirname "$0")"
O="${OPENSSL:-/opt/homebrew/opt/openssl@3/bin/openssl}"

gen_ca() { # file common-name
    "$O" req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout "$1.key" -out "$1.pem" -days 36500 -subj "/CN=$2" 2>/dev/null
}
gen_leaf() { # file common-name subjectAltName ca [extendedKeyUsage]
    "$O" req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout "$1.key" -out "$1.csr" -subj "/CN=$2" 2>/dev/null
    printf "subjectAltName=%s\nbasicConstraints=CA:FALSE\nextendedKeyUsage=%s\n" "$3" "${5:-serverAuth}" > "$1.ext"
    "$O" x509 -req -in "$1.csr" -CA "$4.pem" -CAkey "$4.key" -CAcreateserial -out "$1.pem" -days 36500 -extfile "$1.ext" 2>/dev/null
    rm "$1.csr" "$1.ext"
}

gen_ca ca "CeeSafeLibs Test CA"
gen_ca otherca "Unrelated Test CA"
gen_leaf localhost localhost "DNS:localhost,IP:127.0.0.1" ca
gen_leaf a a.test "DNS:a.test" ca
gen_leaf b b.test "DNS:b.test" ca
gen_leaf wild "*.wild.test" "DNS:*.wild.test" ca
gen_leaf client "test client" "DNS:client.test" ca clientAuth
rm -f ./*.srl ca.key otherca.key
