#include "crypto/Aes128Cbc.h"

#include <QTest>

using namespace RoomTunes;

class AesCryptoTests : public QObject
{
    Q_OBJECT

  private slots:
    void decryptsNistAes128CbcVector();
    void rejectsInvalidInputSizes();
};

void AesCryptoTests::decryptsNistAes128CbcVector()
{
    // NIST SP 800-38A F.2.1, AES-128 CBC-AES example.
    const QByteArray key = QByteArray::fromHex("2b7e151628aed2a6abf7158809cf4f3c");
    const QByteArray iv  = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f");
    const QByteArray ciphertext =
        QByteArray::fromHex("7649abac8119b246cee98e9b12e9197d"
                            "5086cb9b507219ee95db113a917678b2"
                            "73bed6b8e3c1743b7116e69e22229516"
                            "3ff1caa1681fac09120eca307586e1a7");
    const QByteArray expected =
        QByteArray::fromHex("6bc1bee22e409f96e93d7e117393172a"
                            "ae2d8a571e03ac9c9eb76fac45af8e51"
                            "30c81c46a35ce411e5fbc1191a0a52ef"
                            "f69f2445df4f9b17ad2b417be66c3710");

    QCOMPARE(Aes128Cbc::decrypt(key, iv, ciphertext), expected);
}

void AesCryptoTests::rejectsInvalidInputSizes()
{
    const QByteArray key        = QByteArray::fromHex("2b7e151628aed2a6abf7158809cf4f3c");
    const QByteArray iv         = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f");
    const QByteArray ciphertext = QByteArray::fromHex("7649abac8119b246cee98e9b12e9197d");

    QVERIFY(Aes128Cbc::decrypt(key.left(15), iv, ciphertext).isEmpty());
    QVERIFY(Aes128Cbc::decrypt(key, iv.left(15), ciphertext).isEmpty());
    QVERIFY(Aes128Cbc::decrypt(key, iv, QByteArray()).isEmpty());
    QVERIFY(Aes128Cbc::decrypt(key, iv, ciphertext.left(15)).isEmpty());
}

QTEST_APPLESS_MAIN(AesCryptoTests)

#include "aes_crypto_tests.moc"
