#include "streamhub-chat-admin.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QLabel *MakeStatus(QWidget *parent) { auto *label = new QLabel(parent); label->setWordWrap(true); label->setObjectName("adminStatus"); return label; }
}

StreamHubChatAdminDialog::StreamHubChatAdminDialog(int port, QWidget *parent) : QDialog(parent), port_(port)
{
    setWindowTitle(tr("Administração do StreamHub Chat"));
    setWindowIcon(QIcon(":/streamhub-ui/branding/k4-logo.png"));
    setMinimumSize(540, 520);
    setObjectName("streamHubAdmin");
    network_ = new QNetworkAccessManager(this);
    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    root->addWidget(tabs);

    auto *moderation = new QWidget(tabs);
    auto *moderationRoot = new QVBoxLayout(moderation);
    auto *actionCard = new QWidget(moderation); actionCard->setObjectName("adminCard");
    auto *actionForm = new QFormLayout(actionCard);
    moderationPlatform_ = new QComboBox(actionCard);
    moderationPlatform_->addItem(tr("Twitch"), "twitch");
    moderationPlatform_->addItem(tr("Kick"), "kick");
    moderationPlatform_->addItem(tr("YouTube"), "youtube");
    moderationUser_ = new QLineEdit(actionCard);
    moderationAction_ = new QComboBox(actionCard); moderationAction_->addItem(tr("Timeout"), "timeout"); moderationAction_->addItem(tr("Banir"), "ban"); moderationAction_->addItem(tr("Desbanir"), "unban");
    timeoutSeconds_ = new QSpinBox(actionCard); timeoutSeconds_->setRange(1, 1209600); timeoutSeconds_->setValue(600); timeoutSeconds_->setSuffix(tr(" segundos"));
    durationLabel_ = new QLabel(tr("Duração (segundos)"), actionCard);
    auto *moderateButton = new QPushButton(tr("Aplicar ação"), actionCard); moderateButton->setObjectName("primaryAction");
    connect(moderateButton, &QPushButton::clicked, this, &StreamHubChatAdminDialog::RunModeration);
    actionForm->addRow(tr("Plataforma"), moderationPlatform_); actionForm->addRow(tr("Identificador do usuário"), moderationUser_); actionForm->addRow(tr("Ação"), moderationAction_); actionForm->addRow(durationLabel_, timeoutSeconds_); actionForm->addRow(moderateButton);
    connect(moderationPlatform_, &QComboBox::currentIndexChanged, this, &StreamHubChatAdminDialog::UpdateModerationPlatform);
    connect(moderationAction_, &QComboBox::currentIndexChanged, this, &StreamHubChatAdminDialog::UpdateModerationPlatform);
    moderationRoot->addWidget(actionCard);
    moderationInfo_ = MakeStatus(moderation);
    moderationRoot->addWidget(moderationInfo_);

    chatSettingsCard_ = new QWidget(moderation); chatSettingsCard_->setObjectName("adminCard");
    auto *settingsCard = chatSettingsCard_;
    auto *settings = new QFormLayout(settingsCard);
    slowMode_ = new QCheckBox(tr("Modo lento"), settingsCard); slowModeSeconds_ = new QSpinBox(settingsCard); slowModeSeconds_->setRange(3, 120); slowModeSeconds_->setValue(30); slowModeSeconds_->setSuffix(tr(" segundos"));
    followerMode_ = new QCheckBox(tr("Somente seguidores"), settingsCard); subscriberMode_ = new QCheckBox(tr("Somente inscritos"), settingsCard); emoteMode_ = new QCheckBox(tr("Somente emotes"), settingsCard);
    settings->addRow(slowMode_); settings->addRow(tr("Intervalo"), slowModeSeconds_); settings->addRow(followerMode_); settings->addRow(subscriberMode_); settings->addRow(emoteMode_);
    auto *settingsButtons = new QHBoxLayout(); auto *loadSettings = new QPushButton(tr("Carregar"), settingsCard); auto *applySettings = new QPushButton(tr("Aplicar"), settingsCard); applySettings->setObjectName("primaryAction");
    connect(loadSettings, &QPushButton::clicked, this, &StreamHubChatAdminDialog::LoadChatSettings); connect(applySettings, &QPushButton::clicked, this, &StreamHubChatAdminDialog::ApplyChatSettings);
    settingsButtons->addStretch(); settingsButtons->addWidget(loadSettings); settingsButtons->addWidget(applySettings); settings->addRow(settingsButtons);
    moderationRoot->addWidget(settingsCard); moderationStatus_ = MakeStatus(moderation); moderationRoot->addWidget(moderationStatus_); moderationRoot->addStretch();
    tabs->addTab(moderation, tr("Moderação"));
    UpdateModerationPlatform();

    setStyleSheet(R"(
        QDialog#streamHubAdmin { background:#080c14; color:#f2f7ff; }
        QWidget#adminCard { background:#0d1726; border:1px solid #29496f; border-radius:9px; }
        QLabel#adminStatus { color:#9eb2cb; }
        QTabWidget::pane { border:1px solid #29496f; background:#080c14; }
        QTabBar::tab { padding:8px 14px; background:#101a2a; color:#b8c8dc; border:1px solid #29496f; }
        QTabBar::tab:selected { background:#0077ff; color:white; border-color:#00c8ff; }
        QLineEdit,QComboBox,QSpinBox { background:#080c14; color:white; border:1px solid #29496f; border-radius:7px; padding:6px; }
        QPushButton { background:#17283a; color:white; border:1px solid #29496f; border-radius:7px; padding:7px 11px; }
        QPushButton:hover { border-color:#00c8ff; }
        QPushButton#primaryAction { background:#0077ff; border-color:#00c8ff; font-weight:700; }
    )");
}

void StreamHubChatAdminDialog::Request(const QByteArray &method, const QString &path, const QJsonObject &body, ReplyHandler handler)
{
    QNetworkRequest request{QUrl(QString("http://localhost:%1%2").arg(port_).arg(path))}; request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = method == "GET" ? network_->get(request) : network_->sendCustomRequest(request, method, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, handler]() { const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const auto object = QJsonDocument::fromJson(reply->readAll()).object(); reply->deleteLater(); handler(object, status); });
}

void StreamHubChatAdminDialog::UpdateModerationPlatform()
{
    const QString platform = moderationPlatform_->currentData().toString();
    const bool kick = platform == "kick";
    if (platform != durationPlatform_) {
        const int seconds = durationPlatform_ == "kick" ? timeoutSeconds_->value() * 60 : timeoutSeconds_->value();
        timeoutSeconds_->setRange(1, 1209600);
        timeoutSeconds_->setValue(kick ? qMax(1, (seconds + 59) / 60) : seconds);
        durationPlatform_ = platform;
    }
    if (platform == "twitch") moderationUser_->setPlaceholderText(tr("Nome de usuário da Twitch"));
    else if (platform == "kick") moderationUser_->setPlaceholderText(tr("ID numérico do usuário na Kick"));
    else moderationUser_->setPlaceholderText(tr("@handle ou ID do canal do YouTube"));
    if (platform == "twitch") moderationInfo_->setText(tr("Twitch: informe o usuário. Os modos do chat abaixo também podem ser ajustados aqui."));
    else if (platform == "kick") moderationInfo_->setText(tr("Kick: informe o ID numérico do usuário. O timeout é definido em minutos; a API oficial não oferece os modos do chat nesta integração."));
    else moderationInfo_->setText(tr("YouTube: informe o @handle ou ID do canal. As ações exigem uma live com chat ativo; modos do chat não são oferecidos pela API."));
    chatSettingsCard_->setVisible(platform == "twitch");
    timeoutSeconds_->setVisible(moderationAction_->currentData().toString() == "timeout");
    durationLabel_->setVisible(timeoutSeconds_->isVisible());
    durationLabel_->setText(kick ? tr("Duração (minutos)") : tr("Duração (segundos)"));
    timeoutSeconds_->setRange(1, kick ? 10080 : 1209600);
    timeoutSeconds_->setSuffix(kick ? tr(" minutos") : tr(" segundos"));
}
void StreamHubChatAdminDialog::RunModeration() { const QString platform = moderationPlatform_->currentData().toString(); const int duration = timeoutSeconds_->value() * (platform == "kick" ? 60 : 1); Request("POST", "/api/moderation/action", {{"platform", platform}, {"user", moderationUser_->text()}, {"action", moderationAction_->currentData().toString()}, {"duration", duration}}, [this](const QJsonObject &o, int status) { moderationStatus_->setText(status == 200 ? tr("Ação aplicada.") : o.value("error").toString()); }); }
void StreamHubChatAdminDialog::LoadChatSettings() { Request("GET", "/api/twitch/chat-settings", {}, [this](const QJsonObject &o, int status) { if (status != 200) { moderationStatus_->setText(o.value("error").toString()); return; } const auto d = o.value("data").toObject(); slowMode_->setChecked(d.value("slow_mode").toBool()); followerMode_->setChecked(d.value("follower_mode").toBool()); subscriberMode_->setChecked(d.value("subscriber_mode").toBool()); emoteMode_->setChecked(d.value("emote_mode").toBool()); slowModeSeconds_->setValue(qMax(3, d.value("slow_mode_wait_time").toInt(30))); moderationStatus_->setText(tr("Configurações carregadas.")); }); }
void StreamHubChatAdminDialog::ApplyChatSettings() { Request("PATCH", "/api/twitch/chat-settings", {{"slowMode", slowMode_->isChecked()}, {"slowModeWaitTime", slowModeSeconds_->value()}, {"followerMode", followerMode_->isChecked()}, {"subscriberMode", subscriberMode_->isChecked()}, {"emoteMode", emoteMode_->isChecked()}}, [this](const QJsonObject &o, int status) { moderationStatus_->setText(status == 200 ? tr("Configurações aplicadas.") : o.value("error").toString()); }); }
