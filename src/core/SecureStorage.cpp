#include "core/SecureStorage.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

QString SecureStorage::protect(const QString& plainText, QString* error)
{
    if (error) {
        error->clear();
    }

#ifdef Q_OS_WIN
    const QByteArray utf8 = plainText.toUtf8();

    DATA_BLOB inBlob;
    inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(utf8.constData()));
    inBlob.cbData = static_cast<DWORD>(utf8.size());

    DATA_BLOB outBlob;
    outBlob.pbData = nullptr;
    outBlob.cbData = 0;

    if (!CryptProtectData(&inBlob, L"ELLA", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        if (error) {
            *error = QStringLiteral("CryptProtectData failed");
        }
        return QString();
    }

    QByteArray encrypted(reinterpret_cast<const char*>(outBlob.pbData), static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return QString::fromLatin1(encrypted.toBase64());
#else
    Q_UNUSED(error);
    return QString::fromLatin1(plainText.toUtf8().toBase64());
#endif
}

QString SecureStorage::unprotect(const QString& cipherText, QString* error)
{
    if (error) {
        error->clear();
    }

    const QByteArray encrypted = QByteArray::fromBase64(cipherText.toLatin1());

#ifdef Q_OS_WIN
    if (encrypted.isEmpty() && !cipherText.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Invalid base64 payload");
        }
        return QString();
    }

    DATA_BLOB inBlob;
    inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()));
    inBlob.cbData = static_cast<DWORD>(encrypted.size());

    DATA_BLOB outBlob;
    outBlob.pbData = nullptr;
    outBlob.cbData = 0;

    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        if (error) {
            *error = QStringLiteral("CryptUnprotectData failed");
        }
        return QString();
    }

    QByteArray decrypted(reinterpret_cast<const char*>(outBlob.pbData), static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return QString::fromUtf8(decrypted);
#else
    Q_UNUSED(error);
    return QString::fromUtf8(encrypted);
#endif
}

