#include "streamhub-chat-dock.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkRequest>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "plugin-support.h"

namespace {
// Uma cor por plataforma, só pra dar uma pista visual rápida sem precisar
// de ícones (dock nativo não tem CSS como a overlay do navegador tem).
QString ColorForPlatform(const QString &platform)
{
    if (platform == "twitch") return "#9146FF";
    if (platform == "youtube") return "#FF0000";
    if (platform == "kick") return "#53FC18";
    if (platform == "tiktok") return "#FFFFFF";
    return "#AAAAAA";
}
}

StreamHubChatDock::StreamHubChatDock(QWidget *parent) : QDockWidget("StreamHub Chat", parent)
{
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    statusLabel_ = new QLabel(tr("Preparando StreamHub..."), container);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet("color: #AAAAAA; font-style: italic; padding: 2px;");
    layout->addWidget(statusLabel_);

    list_ = new QListWidget(container);
    list_->setWordWrap(true);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(list_);

    container->setLayout(layout);
    setWidget(container);

    net_ = new QNetworkAccessManager(this);
    connect(net_, &QNetworkAccessManager::finished, this, &StreamHubChatDock::OnPollFinished);

    // Só é usado quando uma consulta falha (servidor ainda subindo, porta
    // errada etc.) — enquanto está tudo OK, a próxima consulta é disparada
    // direto de OnPollFinished(), sem esperar esse timer.
    retryTimer_ = new QTimer(this);
    retryTimer_->setSingleShot(true);
    retryTimer_->setInterval(3000);
    connect(retryTimer_, &QTimer::timeout, this, &StreamHubChatDock::PollOnce);
}

void StreamHubChatDock::ConnectTo(int port)
{
    port_ = port;
    since_ = 0;
    connected_ = false;
    PollOnce();
}

void StreamHubChatDock::PollOnce()
{
    if (pollInFlight_) {
        return;
    }
    pollInFlight_ = true;

    QUrl url(QString("http://127.0.0.1:%1/api/chat/poll?since=%2").arg(port_).arg(since_));
    QNetworkRequest request(url);
    // Um pouco acima do tempo que o servidor segura a resposta (~25s) pra
    // não competir com o próprio long-poll do lado do servidor.
    request.setTransferTimeout(30000);

    net_->get(request);
}

void StreamHubChatDock::OnPollFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    pollInFlight_ = false;

    if (reply->error() != QNetworkReply::NoError) {
        connected_ = false;
        statusLabel_->show();
        statusLabel_->setText(tr("Sem conexão com o StreamHub — tentando de novo..."));
        retryTimer_->start();
        return;
    }

    if (!connected_) {
        connected_ = true;
        blog(LOG_INFO, "[streamhub] dock de chat conectado ao servidor local (porta %d)", port_);
        statusLabel_->hide();
    }

    auto doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.isObject()) {
        auto obj = doc.object();
        since_ = static_cast<qint64>(obj.value("next").toDouble(static_cast<double>(since_)));

        for (const auto &v : obj.value("messages").toArray()) {
            auto m = v.toObject();
            AppendChatLine(m.value("platform").toString(), m.value("user").toString(),
                           m.value("message").toString());
        }
    }

    // Long-poll: a próxima consulta é disparada na hora; o servidor é quem
    // segura a resposta até ter novidade (ou até um timeout de ~25s), então
    // isso não vira um loop martelando requisições.
    PollOnce();
}

void StreamHubChatDock::SetStatus(const QString &status)
{
    if (connected_) {
        return; // já conectado, não precisa mais mostrar progresso
    }
    statusLabel_->show();
    statusLabel_->setText(status);
}

void StreamHubChatDock::AppendChatLine(const QString &platform, const QString &user, const QString &text)
{
    auto *item = new QListWidgetItem();
    item->setText(QString("[%1] %2: %3").arg(platform.toUpper(), user, text));
    item->setForeground(QColor(ColorForPlatform(platform)));
    list_->addItem(item);

    while (list_->count() > kMaxItems) {
        delete list_->takeItem(0);
    }

    list_->scrollToBottom();
}
