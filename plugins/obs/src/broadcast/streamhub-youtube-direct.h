#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;

class StreamHubYoutubeDirect final : public QObject {
    Q_OBJECT
public:
    explicit StreamHubYoutubeDirect(QObject *parent = nullptr);

    void StartAuthorization();
    void RefreshAccount();
    void FetchCurrentContent();

signals:
    void authorizationOpened(const QUrl &url);
    void accountChanged(const QString &name);
    void contentReady(const QJsonObject &content);
    void disconnected();
    void failed(const QString &message);

private:
    void HandleCallback();
    void ExchangeCode(const QString &code, const QString &redirectUri);
    void RequestAccount();
    void RequestJson(const QString &path, std::function<void(const QJsonObject &, int)> done);
    void RequestContentAfterBroadcasts(const QJsonObject &response);
    QString RandomUrlToken(int bytes) const;

    QNetworkAccessManager *network_ = nullptr;
    QTcpServer *callbackServer_ = nullptr;
    QString state_;
    QString verifier_;
    QString redirectUri_;
    QString accessToken_;
    QString refreshToken_;
    qint64 accessExpiresAt_ = 0;
    QString accountName_;
    QJsonObject currentContent_;
};
