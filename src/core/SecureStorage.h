#pragma once

#include <QString>

class SecureStorage
{
public:
    static QString protect(const QString& plainText, QString* error = nullptr);
    static QString unprotect(const QString& cipherText, QString* error = nullptr);
};

