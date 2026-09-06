#include "streamhub-chat-dock.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QButtonGroup>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QPushButton>
#include <QPainter>
#include <QScrollBar>
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

StreamHubChatDock::StreamHubChatDock(QWidget *parent)
    : QWidget(parent), background_(":/streamhub-ui/branding/streamhub-background.png"),
      crown_(":/streamhub-ui/branding/k4-crown.png")
{
    auto *container = this;
    container->setObjectName("streamHubChat");
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(9);

    auto *header = new QWidget(container);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    auto *brandIcon = new QLabel(header);
    brandIcon->setObjectName("chatBrandIcon");
    brandIcon->setFixedSize(34, 34);
    brandIcon->setAlignment(Qt::AlignCenter);
    brandIcon->setPixmap(QPixmap(":/streamhub-ui/branding/k4-logo.png")
                             .scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    headerLayout->addWidget(brandIcon);
    auto *brand = new QLabel(tr("StreamHub Chat"), header);
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
        "<a style=\"color:#00c8ff;text-decoration:none\" href=\"https://livepix.gg/k4binho\">Donate</a>",
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
        QWidget#streamHubChat { background: #080c14; color: #f2f7ff; }
        QLabel#chatBrandIcon { background:transparent; }
        QLabel#chatBrand { font-size: 17px; font-weight: 700; color: #f2f7ff; }
        QLabel#connectionState { color: #9eb2cb; padding-right: 5px; }
        QLabel#connectionState[connected="true"] { color: #16d86a; }
        QPushButton#iconButton { background: #101a2a; border: 1px solid #29496f;
            border-radius: 7px; color: #f2f7ff; font-size: 15px; }
        QPushButton#iconButton:hover { background: #15233a; border-color: #00c8ff; }
        QLabel#donateLink { font-size: 10px; }
        QLabel#donateLink a { color: #00c8ff; text-decoration: none; }
        QPushButton#filterChip { background: #101a2a; border: 1px solid #29496f;
            border-radius: 14px; padding: 5px 11px; color: #9eb2cb; }
        QPushButton#filterChip:hover { border-color: #00c8ff; color: #f2f7ff; }
        QPushButton#filterChip:checked { background: #0077ff; border: 1px solid #00c8ff;
            color: #f2f7ff; font-weight: 600; }
        QLabel#chatStatus { color: #9eb2cb; font-style: italic; padding: 2px 5px; }
        QListWidget#chatMessages { background: #080c14; border: 1px solid #29496f;
            border-radius: 9px; padding: 8px; outline: none; }
        QListWidget#chatMessages::item { border: none; background: transparent; }
        QWidget#socialLinks { background: #101a2a; border: 1px solid #29496f;
            border-radius: 8px; }
        QLabel#socialsLabel { color: #9eb2cb; font-weight: 600; }
        QPushButton#socialButton { background: transparent; border: 1px solid transparent;
            border-radius: 6px; padding: 4px; }
        QPushButton#socialButton:hover { background: #15233a; border-color: #00c8ff; }
        QLabel#readOnlyState { background: #101a2a; border: 1px solid #29496f;
            border-radius: 8px; color: #9eb2cb; padding: 9px 12px; }
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

void StreamHubChatDock::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (!background_.isNull()) {
        const QPixmap scaled = background_.scaled(size(), Qt::KeepAspectRatioByExpanding,
                                                  Qt::SmoothTransformation);
        painter.setOpacity(0.48);
        painter.drawPixmap((width() - scaled.width()) / 2,
                           (height() - scaled.height()) / 2, scaled);
    }

    if (!crown_.isNull()) {
        const QPixmap crown = crown_.scaled(130, 130, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
        painter.setOpacity(0.13);
        painter.drawPixmap(width() - crown.width() - 14, 70, crown);
    }
}

void StreamHubChatDock::ConnectTo(int port)
{
    port_ = port;
    since_ = 0;
    connected_ = false;
    SetConnected(false);
    PollOnce();
}

void StreamHubChatDock::SetConfigPath(const QString &configPath)
{
    configPath_ = configPath;
    highlightTerms_ = {"k4binho"};
    QFile input(configPath_);
    if (!input.open(QIODevice::ReadOnly))
        return;
    const QJsonObject config = QJsonDocument::fromJson(input.readAll()).object();
    const auto addTerm = [this](QString value) {
        value = value.trimmed().toLower();
        while (value.startsWith('@') || value.startsWith('#')) value.remove(0, 1);
        if (!value.isEmpty() && !highlightTerms_.contains(value)) highlightTerms_.append(value);
    };
    addTerm(config.value("overlay").toObject().value("channelName").toString());
    addTerm(config.value("twitch").toObject().value("channel").toString());
    addTerm(config.value("kick").toObject().value("channel").toString());
    addTerm(config.value("tiktok").toObject().value("username").toString());
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
        SetConfigPath(configPath_);
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
            const QString platform = m.value("platform").toString();
            if (m.value("kind").toString() == "status") {
                AppendConnectionNotice(platform, m.value("message").toString(),
                                       m.value("connected").toBool(false));
                continue;
            }
            QStringList badges;
            for (const auto &badge : m.value("badges").toArray())
                badges.append(badge.toString());
            AppendChatLine(platform, m.value("user").toString(), m.value("message").toString(), badges,
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
                                       const QString &text, const QStringList &badges, qint64 timestamp)
{
    const bool followLatest = list_->verticalScrollBar()->value() >=
                              list_->verticalScrollBar()->maximum() - 2;
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
    timeLabel->setStyleSheet("color: #9eb2cb;");
    rowLayout->addWidget(timeLabel, 0, Qt::AlignTop);

    auto *badge = new QLabel(row);
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedSize(28, 24);
    badge->setPixmap(IconForPlatform(platform).pixmap(20, 20));
    badge->setToolTip(platform.isEmpty() ? tr("Desconhecido") : platform.at(0).toUpper() + platform.mid(1));
    rowLayout->addWidget(badge, 0, Qt::AlignTop);

    auto *message = new QLabel(row);
    message->setTextFormat(Qt::RichText);
    message->setWordWrap(true);
    QString badgeMarkup;
    for (QString badgeName : badges) {
        badgeName = badgeName.toLower();
        QString shortName;
        if (badgeName == "moderator" || badgeName == "mod") shortName = "MOD";
        else if (badgeName == "subscriber" || badgeName == "sub") shortName = "SUB";
        else if (badgeName == "vip") shortName = "VIP";
        else if (badgeName == "broadcaster") shortName = "LIVE";
        if (!shortName.isEmpty())
            badgeMarkup += QString("&nbsp;<small style='color:%1'>[%2]</small>")
                               .arg(ColorForPlatform(platform), shortName);
    }
    message->setText(QString("<b style='color:%1'>%2</b>%3&nbsp;&nbsp;<span style='color:#f2f7ff'>%4</span>")
                         .arg(ColorForPlatform(platform), user.toHtmlEscaped(), badgeMarkup, text.toHtmlEscaped()));
    rowLayout->addWidget(message, 1);

    item->setSizeHint(row->sizeHint());
    list_->addItem(item);
    list_->setItemWidget(item, row);
    item->setHidden(activeFilter_ != "all" && activeFilter_ != platform);

    const QString lowered = text.toLower();
    const bool mentioned = std::any_of(highlightTerms_.cbegin(), highlightTerms_.cend(),
                                       [&lowered](const QString &term) {
                                           return !term.isEmpty() && lowered.contains(term);
                                       });
    if (mentioned)
        row->setStyleSheet("background:rgba(0,200,255,35); border-left:3px solid #00c8ff; border-radius:5px;");

    while (list_->count() > kMaxItems) {
        delete list_->takeItem(0);
    }

    if (followLatest)
        list_->scrollToBottom();
}

void StreamHubChatDock::AppendConnectionNotice(const QString &platform, const QString &message,
                                                bool connected)
{
    if (connected)
        return;
    auto *item = new QListWidgetItem();
    item->setData(Qt::UserRole, platform);
    auto *label = new QLabel(QString("%1  %2")
                                 .arg(platform.isEmpty() ? QString() : QString::fromUtf8(u8"↻"),
                                      message), list_);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setStyleSheet(QString("color:#f6bd3a; background:rgba(246,189,58,18); "
                                 "border-left:3px solid %1; border-radius:5px; padding:7px;")
                             .arg(ColorForPlatform(platform)));
    item->setSizeHint(label->sizeHint());
    list_->addItem(item);
    list_->setItemWidget(item, label);
    item->setHidden(activeFilter_ != "all" && activeFilter_ != platform);
}
