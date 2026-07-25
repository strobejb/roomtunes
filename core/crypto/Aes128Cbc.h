#pragma once

#include <QByteArray>

namespace RoomTunes {

// Self-contained AES-128-CBC decryption (FIPS-197), no external crypto
// library dependency -- needed only to decrypt Sonos' ThirdPartyMediaServersX
// blob (see ThirdPartyMediaServers.h), so a full crypto library is more than
// this project wants to link just for that.
class Aes128Cbc
{
public:
    // key and iv must each be exactly 16 bytes; ciphertext must be a
    // non-empty multiple of 16 bytes. Returns the raw decrypted bytes with
    // no padding scheme applied or removed -- if the caller's ciphertext
    // was produced with padding (e.g. OpenSSL EVP's default PKCS#7), that's
    // the caller's job to strip. Empty on invalid input sizes.
    static QByteArray decrypt(const QByteArray &key, const QByteArray &iv, const QByteArray &ciphertext);
};

}
