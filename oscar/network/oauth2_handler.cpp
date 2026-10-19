/* OAuth2 PKCE Handler Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "oauth2_handler.h"
#include "SleepLib/crypto.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QDebug>

// ---------------------------------------------------------------------------
//  Construction / destruction
// ---------------------------------------------------------------------------

OAuth2Handler::OAuth2Handler(const Config& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_nam(new QNetworkAccessManager(this))
{
}

OAuth2Handler::~OAuth2Handler()
{
    stopServer();
}

// ---------------------------------------------------------------------------
//  PKCE helpers
// ---------------------------------------------------------------------------

QString OAuth2Handler::generateCodeVerifier()
{
    // RFC 7636: 43–128 characters from [A-Z, a-z, 0-9, "-", ".", "_", "~"].
    // We generate 64 random bytes and base64url-encode them.
    QByteArray random(64, '\0');
    QRandomGenerator* gen = QRandomGenerator::global();
    for (int i = 0; i < random.size(); ++i) {
        random[i] = static_cast<char>(gen->bounded(256));
    }
    // Base64url: replace + with -, / with _, remove =.
    QString encoded = QString::fromLatin1(random.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    // Trim to 128 chars max.
    return encoded.left(128);
}

QString OAuth2Handler::generateCodeChallenge(const QString& verifier)
{
    // S256: BASE64URL(SHA256(code_verifier))
    QByteArray hash = QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

// ---------------------------------------------------------------------------
//  Authorization flow
// ---------------------------------------------------------------------------

void OAuth2Handler::startAuth()
{
    // Generate PKCE values.
    m_codeVerifier = generateCodeVerifier();
    QString codeChallenge = generateCodeChallenge(m_codeVerifier);

    // Generate a random state parameter.
    QByteArray stateBytes(16, '\0');
    QRandomGenerator* gen = QRandomGenerator::global();
    for (int i = 0; i < stateBytes.size(); ++i) {
        stateBytes[i] = static_cast<char>(gen->bounded(256));
    }
    m_state = QString::fromLatin1(stateBytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    // Start local TCP server for the redirect callback.
    stopServer();
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this,     &OAuth2Handler::onNewConnection);

    if (!m_server->listen(QHostAddress::LocalHost, m_config.redirectPort)) {
        emit authFailed(tr("Could not start local callback server on port %1: %2")
                            .arg(m_config.redirectPort).arg(m_server->errorString()));
        return;
    }

    quint16 port = m_server->serverPort();
    QString redirectUri = QStringLiteral("http://127.0.0.1:%1/callback").arg(port);
    m_redirectUri = redirectUri;

    qDebug() << "OAuth2Handler: listening on" << redirectUri;

    // Build authorization URL.
    QUrl authUrl(m_config.authUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), m_config.clientId);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), m_state);
    if (!m_config.scope.isEmpty()) {
        query.addQueryItem(QStringLiteral("scope"), m_config.scope);
    }
    for (auto it = m_config.extraAuthParams.cbegin();
         it != m_config.extraAuthParams.cend(); ++it) {
        query.addQueryItem(it.key(), it.value());
    }
    authUrl.setQuery(query);

    // Open the system browser.
    qDebug() << "OAuth2Handler: auth URL:" << authUrl.toString();
    QDesktopServices::openUrl(authUrl);
}

void OAuth2Handler::onNewConnection()
{
    QTcpSocket* socket = m_server->nextPendingConnection();
    if (!socket) return;

    // Wait for the HTTP request to arrive.
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QByteArray data = socket->readAll();
        QString request = QString::fromUtf8(data);

        // Parse the GET request line for query parameters.
        // Expected: GET /callback?code=XXX&state=YYY HTTP/1.1
        QString firstLine = request.section('\n', 0, 0).trimmed();
        QString path = firstLine.section(' ', 1, 1);
        QUrl requestUrl(QStringLiteral("http://localhost") + path);
        QUrlQuery query(requestUrl);

        QString code  = query.queryItemValue(QStringLiteral("code"));
        QString state = query.queryItemValue(QStringLiteral("state"));
        QString error = query.queryItemValue(QStringLiteral("error"));

        // Send response HTML to the browser.
        QString html;
        if (!error.isEmpty() || code.isEmpty()) {
            html = QStringLiteral(
                "<html><body><h2>Authorization Failed</h2>"
                "<p>You can close this window and return to OSCAR.</p>"
                "</body></html>");
        } else {
            html = QStringLiteral(
                "<html><body><h2>Authorization Successful</h2>"
                "<p>You can close this window and return to OSCAR.</p>"
                "</body></html>");
        }

        QByteArray response = QStringLiteral(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: close\r\n"
            "\r\n%1").arg(html).toUtf8();

        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();

        // IMPORTANT: stopServer() deletes m_server, which is socket's parent and
        // therefore deletes socket too.  Doing that here — while we are executing
        // inside socket's readyRead signal — would destroy the sender mid-emission
        // and crash Qt's signal machinery.  Defer everything to after this slot
        // returns by posting a zero-delay timer.
        QTimer::singleShot(0, this, [this, code, state, error]() {
            stopServer();

            if (state != m_state) {
                emit authFailed(tr("OAuth state mismatch — possible CSRF attack. Authorization aborted."));
                return;
            }
            if (!error.isEmpty()) {
                emit authFailed(tr("Authorization denied: %1").arg(error));
                return;
            }
            if (code.isEmpty()) {
                emit authFailed(tr("No authorization code received."));
                return;
            }
            exchangeCodeForToken(code);
        });
    });
}

void OAuth2Handler::exchangeCodeForToken(const QString& authCode)
{
    QUrlQuery postData;
    postData.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    postData.addQueryItem(QStringLiteral("code"), authCode);
    postData.addQueryItem(QStringLiteral("client_id"), m_config.clientId);
    if (!m_config.clientSecret.isEmpty()) {
        postData.addQueryItem(QStringLiteral("client_secret"), m_config.clientSecret);
    }
    postData.addQueryItem(QStringLiteral("code_verifier"), m_codeVerifier);
    postData.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);

    QNetworkRequest request(m_config.tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply* reply = m_nam->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished,
            this,  &OAuth2Handler::onTokenReplyFinished);
}

void OAuth2Handler::onTokenReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    QByteArray body = reply->readAll();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "OAuth2Handler: token exchange failed:" << reply->errorString() << body;
        emit authFailed(tr("Token exchange failed: %1").arg(reply->errorString()));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject obj = doc.object();

    m_accessToken = obj.value(QStringLiteral("access_token")).toString();
    // Only update the refresh token if the response contains one.
    // Providers like Dropbox do not return a new refresh token on refresh
    // responses, so preserving the existing one avoids forcing re-auth.
    const QString newRefreshToken = obj.value(QStringLiteral("refresh_token")).toString();
    if (!newRefreshToken.isEmpty()) {
        m_refreshToken = newRefreshToken;
    }

    int expiresIn = obj.value(QStringLiteral("expires_in")).toInt(0);
    if (expiresIn > 0) {
        m_tokenExpiry = QDateTime::currentDateTimeUtc().addSecs(expiresIn - 60);  // 60s margin.
    } else {
        m_tokenExpiry = QDateTime();  // No expiry info.
    }

    if (m_accessToken.isEmpty()) {
        emit authFailed(tr("No access token in server response."));
        return;
    }

    qDebug() << "OAuth2Handler: authenticated, token expires in" << expiresIn << "seconds";
    emit authenticated(m_accessToken);
}

// ---------------------------------------------------------------------------
//  Token refresh
// ---------------------------------------------------------------------------

void OAuth2Handler::refreshToken()
{
    if (m_refreshToken.isEmpty()) {
        emit authFailed(tr("No refresh token available. Please sign in again."));
        return;
    }

    QUrlQuery postData;
    postData.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    postData.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    postData.addQueryItem(QStringLiteral("client_id"), m_config.clientId);
    if (!m_config.clientSecret.isEmpty()) {
        postData.addQueryItem(QStringLiteral("client_secret"), m_config.clientSecret);
    }

    QNetworkRequest request(m_config.tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply* reply = m_nam->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished,
            this,  &OAuth2Handler::onTokenReplyFinished);
}

// ---------------------------------------------------------------------------
//  Token state
// ---------------------------------------------------------------------------

bool OAuth2Handler::hasValidToken() const
{
    if (m_accessToken.isEmpty()) return false;
    if (m_tokenExpiry.isValid() && QDateTime::currentDateTimeUtc() >= m_tokenExpiry) return false;
    return true;
}

bool OAuth2Handler::hasRefreshToken() const
{
    return !m_refreshToken.isEmpty();
}

QString OAuth2Handler::accessToken() const
{
    return m_accessToken;
}

// ---------------------------------------------------------------------------
//  Token persistence
// ---------------------------------------------------------------------------

namespace {

/// AES-256 key for stored tokens (#329). Random bytes generated for this use only.
/// Changing it makes every saved sign-in unreadable, so users would sign in again.
const unsigned char kTokenSecret[32] = {
    0x3f, 0x9b, 0x23, 0xae, 0x0f, 0x0c, 0xc8, 0xbe, 0x0e, 0xca, 0x6f, 0x03, 0x76, 0x20, 0xd0, 0x67,
    0x38, 0x17, 0xbe, 0xfb, 0xd2, 0x62, 0xe0, 0x76, 0x0c, 0xa0, 0xd4, 0xd9, 0x8b, 0x61, 0xd5, 0x85
};

/// GCM IV and tag sizes, in bytes.
const int kTokenIvSize = 12;
const int kTokenTagSize = 16;

QByteArray tokenKey()
{
    return QByteArray(reinterpret_cast<const char*>(kTokenSecret), sizeof(kTokenSecret));
}

} // namespace

const QLatin1String OAuth2Handler::kEncryptedPrefix("#ENC1#");

bool OAuth2Handler::isEncryptedToken(const QString& stored)
{
    return stored.startsWith(kEncryptedPrefix);
}

QString OAuth2Handler::encryptToken(const QString& token)
{
    if (token.isEmpty()) {
        return QString();
    }

    // A fresh random IV for every value: GCM must never reuse an IV with the same key.
    QByteArray iv(kTokenIvSize, '\0');
    QRandomGenerator* gen = QRandomGenerator::system();
    for (int i = 0; i < iv.size(); ++i) {
        iv[i] = static_cast<char>(gen->bounded(256));
    }

    QByteArray ciphertext, tag;
    if (encrypt_aes256_gcm(tokenKey(), iv, token.toUtf8(), ciphertext, tag) != OK
            || tag.size() != kTokenTagSize) {
        qWarning() << "OAuth2: could not encrypt token; it will not be saved";
        return QString();
    }
    return kEncryptedPrefix + QString::fromLatin1((iv + ciphertext + tag).toBase64());
}

QString OAuth2Handler::decryptToken(const QString& stored, bool* ok)
{
    if (ok) {
        *ok = true;
    }
    if (!isEncryptedToken(stored)) {
        return stored;  // Plain text from an earlier version (or empty).
    }

    QByteArray blob = QByteArray::fromBase64(stored.mid(kEncryptedPrefix.size()).toLatin1());
    QByteArray plaintext;
    if (blob.size() > kTokenIvSize + kTokenTagSize) {
        QByteArray iv = blob.left(kTokenIvSize);
        QByteArray tag = blob.right(kTokenTagSize);
        QByteArray ciphertext = blob.mid(kTokenIvSize, blob.size() - kTokenIvSize - kTokenTagSize);
        if (decrypt_aes256_gcm(tokenKey(), iv, ciphertext, tag, plaintext) == OK) {
            return QString::fromUtf8(plaintext);
        }
    }
    qWarning() << "OAuth2: could not decrypt a saved token; the user must sign in again";
    if (ok) {
        *ok = false;
    }
    return QString();
}

void OAuth2Handler::saveTokens(const QString& providerKey)
{
    QSettings s;
    s.beginGroup(QStringLiteral("CloudAuth/%1").arg(providerKey));
    s.setValue(QStringLiteral("refreshToken"), encryptToken(m_refreshToken));
    s.setValue(QStringLiteral("accessToken"), encryptToken(m_accessToken));
    if (m_tokenExpiry.isValid()) {
        s.setValue(QStringLiteral("tokenExpiry"), m_tokenExpiry.toMSecsSinceEpoch());
    }
    s.endGroup();
}

void OAuth2Handler::loadTokens(const QString& providerKey)
{
    QSettings s;
    s.beginGroup(QStringLiteral("CloudAuth/%1").arg(providerKey));
    const QString storedRefresh = s.value(QStringLiteral("refreshToken")).toString();
    const QString storedAccess = s.value(QStringLiteral("accessToken")).toString();
    m_refreshToken = decryptToken(storedRefresh);
    m_accessToken = decryptToken(storedAccess);

    // Tokens saved in plain text by an earlier version are re-saved encrypted below.
    const bool plainTextFound = (!storedRefresh.isEmpty() && !isEncryptedToken(storedRefresh))
                             || (!storedAccess.isEmpty() && !isEncryptedToken(storedAccess));
    QVariant expiryVar = s.value(QStringLiteral("tokenExpiry"));
    if (expiryVar.isValid()) {
        bool ok = false;
        qint64 epochMs = expiryVar.toLongLong(&ok);
        if (ok && epochMs > 0) {
            m_tokenExpiry = QDateTime::fromMSecsSinceEpoch(epochMs);
        } else {
            // Migrate: legacy format stored as ISO 8601 string.
            m_tokenExpiry = QDateTime::fromString(expiryVar.toString(), Qt::ISODate);
        }
    }
    s.endGroup();

    if (plainTextFound) {
        qDebug() << "OAuth2: encrypting saved" << providerKey << "tokens";
        saveTokens(providerKey);
    }
}

void OAuth2Handler::clearTokens(const QString& providerKey)
{
    m_accessToken.clear();
    m_refreshToken.clear();
    m_tokenExpiry = QDateTime();

    QSettings s;
    s.beginGroup(QStringLiteral("CloudAuth/%1").arg(providerKey));
    s.remove(QString());  // Remove all keys in the group.
    s.endGroup();
}

// ---------------------------------------------------------------------------
//  Server management
// ---------------------------------------------------------------------------

void OAuth2Handler::stopServer()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}
