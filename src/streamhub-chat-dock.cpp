#include "streamhub-chat-dock.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QButtonGroup>
#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "obs.h"

#include "plugin-support.h"
#include "streamhub-chat-settings.h"

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

QIcon IconForPlatform(const QString &platform)
{
    return QIcon(QString(":/streamhub-ui/icons/%1.svg").arg(platform));
}
}

StreamHubChatDock::StreamHubChatDock(QWidget *parent) : QWidget(parent)
{
    auto *container = this;
    container->setObjectName("streamHubChat");
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(9);

    auto *header = new QWidget(container);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    auto *brand = new QLabel(tr("▰  StreamHub Chat"), header);
    brand->setObjectName("chatBrand");
    headerLayout->addWidget(brand);
    headerLayout->addStretch();
    connectionLabel_ = new QLabel(tr("● Iniciando"), header);
    connectionLabel_->setObjectName("connectionState");
    headerLayout->addWidget(connectionLabel_);

    auto *headerActions = new QWidget(header);
    auto *headerActionsLayout = new QVBoxLayout(headerActions);
    headerActionsLayout->setContentsMargins(0, 0, 0, 0);
    headerActionsLayout->setSpacing(1);
    headerActionsLayout->setAlignment(Qt::AlignHCenter);
    auto *configureButton = new QPushButton(headerActions);
    configureButton->setObjectName("iconButton");
    configureButton->setIcon(QIcon(":/streamhub-ui/icons/settings.svg"));
    configureButton->setIconSize(QSize(18, 18));
    configureButton->setToolTip(tr("Configurar chats"));
    configureButton->setFixedSize(34, 30);
    connect(configureButton, &QPushButton::clicked, this, &StreamHubChatDock::OnConfigureClicked);
    headerActionsLayout->addWidget(configureButton, 0, Qt::AlignHCenter);
    auto *donateLink = new QLabel(
        "<a style=\"color:#a779ff;text-decoration:none\" href=\"https://livepix.gg/k4binho\">Donate</a>",
        headerActions);
    donateLink->setObjectName("donateLink");
    donateLink->setTextFormat(Qt::RichText);
    donateLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    donateLink->setOpenExternalLinks(true);
    donateLink->setToolTip(tr("Apoiar K4binho pelo LivePix"));
    headerActionsLayout->addWidget(donateLink, 0, Qt::AlignHCenter);
    headerLayout->addWidget(headerActions, 0, Qt::AlignTop);
    layout->addWidget(header);

    auto *filters = new QWidget(container);
    auto *filtersLayout = new QHBoxLayout(filters);
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);
    filterGroup_ = new QButtonGroup(this);
    filterGroup_->setExclusive(true);
    const QList<QPair<QString, QString>> filterSpecs = {
        {"all", tr("Todos")}, {"twitch", ""}, {"kick", ""},
        {"youtube", ""}, {"tiktok", ""}};
    for (const auto &spec : filterSpecs) {
        auto *button = new QPushButton(spec.second, filters);
        button->setObjectName("filterChip");
        button->setCheckable(true);
        button->setChecked(spec.first == "all");
        if (spec.first != "all") {
            button->setIcon(IconForPlatform(spec.first));
            button->setIconSize(QSize(18, 18));
            button->setFixedWidth(38);
            button->setToolTip(spec.first.at(0).toUpper() + spec.first.mid(1));
            button->setAccessibleName(button->toolTip());
        }
        filterGroup_->addButton(button);
        connect(button, &QPushButton::clicked, this, [this, platform = spec.first]() {
            activeFilter_ = platform;
            ApplyFilter();
        });
        filtersLayout->addWidget(button);
    }
    filtersLayout->addStretch();
    layout->addWidget(filters);

    statusLabel_ = new QLabel(tr("Preparando StreamHub..."), container);
    statusLabel_->setWordWrap(true);
    statusLabel_->setObjectName("chatStatus");
    layout->addWidget(statusLabel_);

    list_ = new QListWidget(container);
    list_->setObjectName("chatMessages");
    list_->setWordWrap(true);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setSpacing(3);
    layout->addWidget(list_);

    auto *socials = new QWidget(container);
    socials->setObjectName("socialLinks");
    auto *socialsLayout = new QHBoxLayout(socials);
    socialsLayout->setContentsMargins(9, 4, 7, 4);
    socialsLayout->setSpacing(5);
    auto *socialsLabel = new QLabel(tr("Siga K4binho"), socials);
    socialsLabel->setObjectName("socialsLabel");
    socialsLayout->addWidget(socialsLabel);
    socialsLayout->addStretch();
    const QList<QPair<QString, QString>> socialSpecs = {
        {"twitch", "https://www.twitch.tv/k4binho"},
        {"kick", "https://kick.com/k4binho"},
        {"youtube", "https://www.youtube.com/@k4binho"}};
    for (const auto &social : socialSpecs) {
        auto *button = new QPushButton(socials);
        button->setObjectName("socialButton");
        button->setIcon(IconForPlatform(social.first));
        button->setIconSize(QSize(17, 17));
        button->setFixedSize(30, 28);
        button->setToolTip(social.first.at(0).toUpper() + social.first.mid(1) + " — K4binho");
        button->setAccessibleName(button->toolTip());
        connect(button, &QPushButton::clicked, this, [url = social.second]() {
            QDesktopServices::openUrl(QUrl(url));
        });
        socialsLayout->addWidget(button);
    }
    layout->addWidget(socials);

    auto *readOnly = new QLabel(tr("◉  Chat em modo leitura"), container);
    readOnly->setObjectName("readOnlyState");
    layout->addWidget(readOnly);

    container->setStyleSheet(R"(
        QWidget#streamHubChat { background: #101622; color: #edf1fb; }
        QLabel#chatBrand { font-size: 17px; font-weight: 700; color: #f6f3ff; }
        QLabel#connectionState { color: #9daac0; padding-right: 5px; }
        QLabel#connectionState[connected="true"] { color: #2ee68a; }
        QPushButton#iconButton { background: #182235; border: 1px solid #33415c;
            border-radius: 7px; color: #dce5f7; font-size: 15px; }
        QPushButton#iconButton:hover { background: #243149; border-color: #8257ff; }
        QLabel#donateLink { font-size: 10px; }
        QLabel#donateLink a { color: #a779ff; text-decoration: none; }
        QPushButton#filterChip { background: #182235; border: 1px solid #283651;
            border-radius: 14px; padding: 5px 11px; color: #b8c3d8; }
        QPushButton#filterChip:hover { border-color: #6e4be8; color: white; }
        QPushButton#filterChip:checked { background: #2b1b58; border: 1px solid #8a52ff;
            color: white; font-weight: 600; }
        QLabel#chatStatus { color: #93a1b8; font-style: italic; padding: 2px 5px; }
        QListWidget#chatMessages { background: #0d131e; border: 1px solid #27334a;
            border-radius: 9px; padding: 8px; outline: none; }
        QListWidget#chatMessages::item { border: none; background: transparent; }
        QWidget#socialLinks { background: #121b2a; border: 1px solid #293750;
            border-radius: 8px; }
        QLabel#socialsLabel { color: #aab6ca; font-weight: 600; }
        QPushButton#socialButton { background: transparent; border: 1px solid transparent;
            border-radius: 6px; padding: 4px; }
        QPushButton#socialButton:hover { background: #202d43; border-color: #5f4ab0; }
        QLabel#readOnlyState { background: #151e2e; border: 1px solid #293750;
            border-radius: 8px; color: #8492aa; padding: 9px 12px; }
    )");

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
    SetConnected(false);
    PollOnce();
}

void StreamHubChatDock::PrepareForRestart()
{
    connected_ = false;
    SetConnected(false);
    since_ = 0;
    statusLabel_->show();
    statusLabel_->setText(tr("Aplicando configuração dos chats..."));
}

void StreamHubChatDock::OnConfigureClicked()
{
    QString error;
    if (StreamHubChatSettings::Edit(this, configPath_, &error)) {
        PrepareForRestart();
        emit SettingsSaved();
    } else if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Erro de configuração"), error);
    }
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
        SetConnected(false);
        since_ = 0;
        statusLabel_->show();
        statusLabel_->setText(tr("Sem conexão com o StreamHub — tentando de novo..."));
        retryTimer_->start();
        return;
    }

    if (!connected_) {
        connected_ = true;
        SetConnected(true);
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
                           m.value("message").toString(),
                           static_cast<qint64>(m.value("timestamp").toDouble()));
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

void StreamHubChatDock::SetConnected(bool connected)
{
    connectionLabel_->setProperty("connected", connected);
    connectionLabel_->setText(connected ? tr("● Online") : tr("● Offline"));
    connectionLabel_->style()->unpolish(connectionLabel_);
    connectionLabel_->style()->polish(connectionLabel_);
}

void StreamHubChatDock::ApplyFilter()
{
    for (int row = 0; row < list_->count(); ++row) {
        auto *item = list_->item(row);
        item->setHidden(activeFilter_ != "all" && item->data(Qt::UserRole).toString() != activeFilter_);
    }
}

void StreamHubChatDock::AppendChatLine(const QString &platform, const QString &user,
                                       const QString &text, qint64 timestamp)
{
    auto *item = new QListWidgetItem();
    item->setData(Qt::UserRole, platform);

    auto *row = new QWidget(list_);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(3, 5, 3, 5);
    rowLayout->setSpacing(8);

    QDateTime when = timestamp > 0 ? QDateTime::fromMSecsSinceEpoch(timestamp).toLocalTime()
                                   : QDateTime::currentDateTime();
    auto *timeLabel = new QLabel(when.toString("HH:mm"), row);
    timeLabel->setFixedWidth(38);
    timeLabel->setStyleSheet("color: #8592a8;");
    rowLayout->addWidget(timeLabel, 0, Qt::AlignTop);

    auto *badge = new QLabel(row);
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedSize(28, 24);
    badge->setPixmap(IconForPlatform(platform).pixmap(20, 20));
    badge->setToolTip(platform.at(0).toUpper() + platform.mid(1));
    rowLayout->addWidget(badge, 0, Qt::AlignTop);

    auto *message = new QLabel(row);
    message->setTextFormat(Qt::RichText);
    message->setWordWrap(true);
    message->setText(QString("<b style='color:%1'>%2</b>&nbsp;&nbsp;<span style='color:#edf1fb'>%3</span>")
                         .arg(ColorForPlatform(platform), user.toHtmlEscaped(), text.toHtmlEscaped()));
    rowLayout->addWidget(message, 1);

    item->setSizeHint(row->sizeHint());
    list_->addItem(item);
    list_->setItemWidget(item, row);
    item->setHidden(activeFilter_ != "all" && activeFilter_ != platform);

    while (list_->count() > kMaxItems) {
        delete list_->takeItem(0);
    }

    list_->scrollToBottom();
}
