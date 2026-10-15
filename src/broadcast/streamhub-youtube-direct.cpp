#include "streamhub-youtube-direct.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

#include <functional>

namespace {
constexpr auto kClientId = "203647478055-esftfgit9i1bjf6hm76qbneb0e3vn3vd.apps.googleusercontent.com";
constexpr auto kScope = "https://www.googleapis.com/auth/youtube.force-ssl";
constexpr auto kApi = "https://www.googleapis.com/youtube/v3";

QString Encode(const QString &value)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(value));
}
}

StreamHubYoutubeDirect::StreamHubYoutubeDirect(QObject *parent) : QObject(parent)
{
    network_ = new QNetworkAccessManager(this);
    callbackServer_ = new QTcpServer(this);
    connect(callbackServer_, &QTcpServer::newConnection, this, &StreamHubYoutubeDirect::HandleCallback);
}

QString StreamHubYoutubeDirect::RandomUrlToken(int bytes) const
{
    QByteArray raw(bytes, Qt::Uninitialized);
    for (char &value : raw)
        value = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
    return QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

void StreamHubYoutubeDirect::StartAuthorization()
{
    if (callbackServer_->isListening())
        callbackServer_->close();
    if (!callbackServer_->listen(QHostAddress::LocalHost, 0)) {
        emit failed(tr("Não foi possível abrir o retorno local do Google."));
        return;
    }

    state_ = RandomUrlToken(32);
    verifier_ = RandomUrlToken(48);
    const QByteArray challenge = QCryptographicHash::hash(verifier_.toUtf8(), QCryptographicHash::Sha256)
                                     .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QString redirectUri = QStringLiteral("http://127.0.0.1:%1/oauth2callback").arg(callbackServer_->serverPort());
    redirectUri_ = redirectUri;
    QUrl url(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
    QUrlQuery query;
    query.addQueryItem("client_id", QString::fromLatin1(kClientId));
    query.addQueryItem("redirect_uri", redirectUri);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("scope", QString::fromLatin1(kScope));
    query.addQueryItem("access_type", "offline");
    query.addQueryItem("prompt", "consent");
    query.addQueryItem("state", state_);
    query.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    query.addQueryItem("code_challenge_method", "S256");
    url.setQuery(query);
    emit authorizationOpened(url);
    if (!QDesktopServices::openUrl(url))
        emit failed(tr("Não foi possível abrir o navegador para autorizar o YouTube."));
}

void StreamHubYoutubeDirect::HandleCallback()
{
    while (callbackServer_->hasPendingConnections()) {
        auto *socket = callbackServer_->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            const QByteArray request = socket->readAll();
            const int firstLineEnd = request.indexOf("\r\n");
            if (firstLineEnd < 0) return;
            const QList<QByteArray> parts = request.left(firstLineEnd).split(' ');
            if (parts.size() < 2) return;
            const QUrl callbackUrl(QStringLiteral("http://127.0.0.1") + QString::fromLatin1(parts.at(1)));
            const QUrlQuery query(callbackUrl);
            const QString code = query.queryItemValue("code");
            const QString state = query.queryItemValue("state");
            const QString error = query.queryItemValue("error");
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<h2>Autorizacao recebida. Voce pode voltar ao OBS.</h2>");
            socket->disconnectFromHost();
            callbackServer_->close();
            if (!error.isEmpty()) { emit failed(tr("Autorização Google recusada: %1").arg(error)); return; }
            if (state != state_ || code.isEmpty()) { emit failed(tr("Retorno OAuth do Google inválido.")); return; }
            ExchangeCode(code, redirectUri_);
        });
    }
}

void StreamHubYoutubeDirect::ExchangeCode(const QString &code, const QString &redirectUri)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery body;
    body.addQueryItem("client_id", QString::fromLatin1(kClientId));
    body.addQueryItem("code", code);
    body.addQueryItem("code_verifier", verifier_);
    body.addQueryItem("redirect_uri", redirectUri);
    body.addQueryItem("grant_type", "authorization_code");
    auto *reply = network_->post(request, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = reply->error() == QNetworkReply::NoError && !object.value("access_token").toString().isEmpty();
        reply->deleteLater();
        if (!ok) { emit failed(tr("Google não concluiu a autorização do YouTube.")); return; }
        accessToken_ = object.value("access_token").toString();
        refreshToken_ = object.value("refresh_token").toString();
        accessExpiresAt_ = QDateTime::currentSecsSinceEpoch() + object.value("expires_in").toInt(3600) - 60;
        RequestAccount();
    });
}

void StreamHubYoutubeDirect::RequestJson(const QString &path, std::function<void(const QJsonObject &, int)> done)
{
    if (accessToken_.isEmpty() || accessExpiresAt_ <= QDateTime::currentSecsSinceEpoch()) {
        emit failed(tr("A autorização direta do YouTube expirou; conecte novamente."));
        return;
    }
    QNetworkRequest request(QUrl(QString::fromLatin1(kApi) + path));
    request.setRawHeader("Authorization", "Bearer " + accessToken_.toUtf8());
    auto *reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        done(object, status);
    });
}

void StreamHubYoutubeDirect::RequestAccount()
{
    RequestJson("/channels?part=snippet&mine=true", [this](const QJsonObject &object, int status) {
        const QJsonObject channel = object.value("items").toArray().at(0).toObject();
        if (status != 200 || channel.isEmpty()) { emit failed(tr("Não foi possível identificar o canal YouTube autorizado.")); return; }
        accountName_ = channel.value("snippet").toObject().value("title").toString();
        emit accountChanged(accountName_);
        FetchCurrentContent();
    });
}

void StreamHubYoutubeDirect::RefreshAccount()
{
    if (accessToken_.isEmpty()) { emit disconnected(); return; }
    RequestAccount();
}

void StreamHubYoutubeDirect::FetchCurrentContent()
{
    RequestJson("/liveBroadcasts?part=snippet,contentDetails,status&mine=true&maxResults=50", [this](const QJsonObject &object, int status) {
        if (status != 200) { emit failed(tr("Não foi possível consultar as lives do YouTube.")); return; }
        QJsonArray candidates;
        for (const auto &value : object.value("items").toArray()) {
            const auto broadcast = value.toObject();
            const QString life = broadcast.value("status").toObject().value("lifeCycleStatus").toString().toLower();
            if (life == "live" || life == "testing" || life == "ready" || life == "created") candidates.append(broadcast);
        }
        if (candidates.isEmpty()) { emit failed(tr("O YouTube não encontrou uma live atual ou agendada.")); return; }
        const auto broadcast = candidates.first().toObject();
        const auto snippet = broadcast.value("snippet").toObject();
        currentContent_ = {{"platform", "youtube"}, {"name", accountName_}, {"broadcastId", broadcast.value("id")},
                           {"title", snippet.value("title")}, {"description", snippet.value("description")},
                           {"categoryId", snippet.value("categoryId")}, {"category", ""}, {"tags", QJsonArray{}},
                           {"language", snippet.value("defaultLanguage")},
                           {"visibility", broadcast.value("status").toObject().value("privacyStatus")},
                           {"isLive", snippet.value("actualStartTime").isString()}};
        emit contentReady(currentContent_);
    });
}
