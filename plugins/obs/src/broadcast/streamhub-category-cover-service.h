#pragma once

#include <QObject>
#include <QHash>
#include <QPixmap>
#include <QString>

class QNetworkAccessManager;

class StreamHubCategoryCoverService : public QObject {
    Q_OBJECT
public:
    explicit StreamHubCategoryCoverService(QNetworkAccessManager *network, int port, QObject *parent = nullptr);
    void SetPort(int port);
    void Fetch(const QString &categoryId, int attempt = 0);

signals:
    void coverLoaded(const QString &categoryId, const QPixmap &cover);
    void coverFailed(const QString &categoryId);

private:
    QNetworkAccessManager *network_ = nullptr;
    int port_ = 605;
    QHash<QString, QPixmap> cache_;
};
