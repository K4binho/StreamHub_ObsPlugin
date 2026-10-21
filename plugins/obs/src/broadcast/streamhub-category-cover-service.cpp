#include "streamhub-category-cover-service.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

StreamHubCategoryCoverService::StreamHubCategoryCoverService(QNetworkAccessManager *network, int port,
                                                             QObject *parent)
    : QObject(parent), network_(network), port_(port)
{
}

void StreamHubCategoryCoverService::SetPort(int port)
{
    port_ = port;
}

void StreamHubCategoryCoverService::Fetch(const QString &categoryId, int attempt)
{
    const QString id = categoryId.trimmed();
    if (id.isEmpty() || !network_)
        return;

    const auto cached = cache_.constFind(id);
    if (cached != cache_.cend()) {
        emit coverLoaded(id, cached.value());
        return;
    }

    QUrl url(QStringLiteral("http://localhost:%1/api/twitch/category-cover").arg(port_));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), id);
    url.setQuery(query);
    QNetworkReply *reply = network_->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, attempt]() {
        QPixmap cover;
        cover.loadFromData(reply->readAll());
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool valid = reply->error() == QNetworkReply::NoError &&
                           (httpStatus == 0 || (httpStatus >= 200 && httpStatus < 300)) && !cover.isNull();
        reply->deleteLater();

        if (valid) {
            cache_.insert(id, cover);
            if (cache_.size() > 100)
                cache_.erase(cache_.begin());
            emit coverLoaded(id, cover);
            return;
        }

        if (attempt < 2) {
            QTimer::singleShot(400 * (attempt + 1), this, [this, id, attempt]() {
                Fetch(id, attempt + 1);
            });
            return;
        }
        emit coverFailed(id);
    });
}
