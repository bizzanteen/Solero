#pragma once
#include <QString>

namespace solero {

// The saved Nexus website login, kept in the system keyring (Secret Service).
//
// Deliberately not stored the way the API key is (plaintext ~/.nexus_api_key): a
// key is scoped and revocable from the website, a password is neither. If there is
// no keyring, nothing is saved and the feature stays off - there is no file
// fallback.
class NexusCredentials {
public:
    static bool keyringAvailable();

    static QString savedLogin();                 // empty when nothing is saved
    static QString password(const QString& login);
    static bool save(const QString& login, const QString& password);
    static bool forget(const QString& login);

    // Pull the login out of `secret-tool search` output. Exposed for tests.
    static QString parseLoginAttribute(const QString& searchOutput);
};

} // namespace solero
