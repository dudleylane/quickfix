#!/bin/sh

# Regenerates the example certificates: a CA, and a server and a client
# certificate it signs, each naming 127.0.0.1 as an IP subjectAltName -- an
# initiator checks an IP address against that, not against the CN (#100).
# They are for the examples and the unit tests only: the private keys are
# checked in, unencrypted.
#
# The tests rely on the layout: certs/ holds the CA alone, and newcerts/
# holds two certificates with distinct subjects.

cd "$(dirname "$0")" || exit 1
set -e

DAYS=36500
mkdir -p certs private newcerts

openssl req -x509 -newkey rsa:2048 -nodes -days "$DAYS" -sha256 \
    -subj "/O=QuickFIX examples/CN=QuickFIX example CA" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -keyout private/cakey.pem -out certs/cacert.pem

sign()
{
    name=$1 unit=$2 usage=$3
    openssl req -newkey rsa:2048 -nodes -sha256 -subj "/O=QuickFIX examples/OU=$unit/CN=127.0.0.1" \
        -keyout "127_0_0_1_$name.key" -out "127_0_0_1_$name.csr"
    printf 'basicConstraints=CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=%s\nsubjectAltName=IP:127.0.0.1\n' \
        "$usage" > "$name.ext"
    openssl x509 -req -in "127_0_0_1_$name.csr" -CA certs/cacert.pem -CAkey private/cakey.pem -set_serial "$4" \
        -days "$DAYS" -sha256 -extfile "$name.ext" -out "127_0_0_1_$name.crt"
    rm -f "127_0_0_1_$name.csr" "$name.ext"
}

sign server server serverAuth 1
sign client client clientAuth 2
cp 127_0_0_1_server.crt newcerts/01.pem
cp 127_0_0_1_client.crt newcerts/02.pem
