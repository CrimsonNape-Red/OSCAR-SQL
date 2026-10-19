/* OAuth2 PKCE Handler Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Generic OAuth2 Authorization Code flow with PKCE (Proof Key for Code
 * Exchange).  Opens the system browser for user consent, runs a temporary
 * local HTTP server to receive the callback, exchanges the authorization
 * code for tokens, and handles token refresh.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OAUTH2_HANDLER_H
#define OAUTH2_HANDLER_H

#include <QMap>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QDateTime>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;

/*!
 * \class OAuth2Handler
 * \brief Handles OAuth2 Authorization Code flow with PKCE.
 *
 * Opens the system browser for user consent, listens on a local
 * port for the redirect callback, exchanges the auth code for
 * tokens, and supports token refresh.  Tokens are persisted via
 * QSettings under a provider-specific key.
 */
class OAuth2Handler : public QObject
{
    Q_OBJECT

public:
    /// Configuration for a specific OAuth2 provider.
    struct Config {
        QUrl    authUrl;       ///< Authorization endpoint.
        QUrl    tokenUrl;      ///< Token exchange endpoint.
        QString clientId;      ///< Application client ID.
        QString clientSecret;  ///< Client secret (required by some providers, e.g. Google, even for native apps).
        QString scope;         ///< Requested scopes (space-separated).
        quint16 redirectPort = 17178;  ///< Fixed local port for redirect callback.
        QMap<QString, QString> extraAuthParams;  ///< Provider-specific authorization query parameters.
    };

    explicit OAuth2Handler(const Config& config, QObject* parent = nullptr);
    ~OAuth2Handler() override;

    /// Start the authorization flow: opens browser, waits for callback.
    void startAuth();

    /// Refresh the access token using the stored refresh token.
    void refreshToken();

    /// Whether we have a non-expired access token.
    bool hasValidToken() const;

    /// Whether we have a refresh token (may need refreshing but can try).
    bool hasRefreshToken() const;

    /// The current access token (may be expired).
    QString accessToken() const;

    /// Save tokens to QSettings under \a providerKey, encrypted with encryptToken().
    void saveTokens(const QString& providerKey);

    /// Load tokens from QSettings under \a providerKey. Plain-text tokens saved by
    /// earlier versions are accepted and re-saved encrypted.
    void loadTokens(const QString& providerKey);

    /// Clear saved tokens.
    void clearTokens(const QString& providerKey);

    /// Prefix that marks a stored token as encrypted (format version 1).
    static const QLatin1String kEncryptedPrefix;

    /// Encrypt \a token for storage: kEncryptedPrefix followed by base64 of
    /// IV + ciphertext + tag (AES-256-GCM). The key is built into OSCAR, so this
    /// keeps tokens from being read as plain text; it does not stop someone who
    /// has the OSCAR source. Returns an empty string for an empty token or on failure.
    static QString encryptToken(const QString& token);

    /// Reverse encryptToken(). A value without kEncryptedPrefix is a plain-text
    /// token from an earlier version and is returned unchanged. Returns an empty
    /// string, and sets \a ok to false, if an encrypted value can't be decrypted.
    static QString decryptToken(const QString& stored, bool* ok = nullptr);

    /// Whether \a stored carries kEncryptedPrefix.
    static bool isEncryptedToken(const QString& stored);

signals:
    /// Emitted when authorization completes (initial or refresh).
    void authenticated(const QString& accessToken);

    /// Emitted when authorization or token refresh fails.
    void authFailed(const QString& error);

private slots:
    void onNewConnection();
    void onTokenReplyFinished();

private:
    void stopServer();
    QString generateCodeVerifier();
    QString generateCodeChallenge(const QString& verifier);
    void exchangeCodeForToken(const QString& authCode);

    Config                  m_config;
    QNetworkAccessManager*  m_nam           = nullptr;
    QTcpServer*             m_server        = nullptr;
    QString                 m_codeVerifier;
    QString                 m_state;
    QString                 m_redirectUri;

    // Tokens.
    QString     m_accessToken;
    QString     m_refreshToken;
    QDateTime   m_tokenExpiry;
};

#endif // OAUTH2_HANDLER_H
