#include "streamhub-chat-admin.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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
    moderationUser_ = new QLineEdit(actionCard); moderationUser_->setPlaceholderText(tr("usuário sem @"));
    moderationAction_ = new QComboBox(actionCard); moderationAction_->addItem(tr("Timeout"), "timeout"); moderationAction_->addItem(tr("Banir"), "ban"); moderationAction_->addItem(tr("Desbanir"), "unban");
    timeoutSeconds_ = new QSpinBox(actionCard); timeoutSeconds_->setRange(1, 1209600); timeoutSeconds_->setValue(600); timeoutSeconds_->setSuffix(tr(" segundos"));
    auto *moderateButton = new QPushButton(tr("Aplicar ação"), actionCard); moderateButton->setObjectName("primaryAction");
    connect(moderateButton, &QPushButton::clicked, this, &StreamHubChatAdminDialog::RunModeration);
    actionForm->addRow(tr("Usuário"), moderationUser_); actionForm->addRow(tr("Ação"), moderationAction_); actionForm->addRow(tr("Duração"), timeoutSeconds_); actionForm->addRow(moderateButton);
    moderationRoot->addWidget(actionCard);

    auto *settingsCard = new QWidget(moderation); settingsCard->setObjectName("adminCard");
    auto *settings = new QFormLayout(settingsCard);
    slowMode_ = new QCheckBox(tr("Modo lento"), settingsCard); slowModeSeconds_ = new QSpinBox(settingsCard); slowModeSeconds_->setRange(3, 120); slowModeSeconds_->setValue(30); slowModeSeconds_->setSuffix(tr(" segundos"));
    followerMode_ = new QCheckBox(tr("Somente seguidores"), settingsCard); subscriberMode_ = new QCheckBox(tr("Somente inscritos"), settingsCard); emoteMode_ = new QCheckBox(tr("Somente emotes"), settingsCard);
    settings->addRow(slowMode_); settings->addRow(tr("Intervalo"), slowModeSeconds_); settings->addRow(followerMode_); settings->addRow(subscriberMode_); settings->addRow(emoteMode_);
    auto *settingsButtons = new QHBoxLayout(); auto *loadSettings = new QPushButton(tr("Carregar"), settingsCard); auto *applySettings = new QPushButton(tr("Aplicar"), settingsCard); applySettings->setObjectName("primaryAction");
    connect(loadSettings, &QPushButton::clicked, this, &StreamHubChatAdminDialog::LoadChatSettings); connect(applySettings, &QPushButton::clicked, this, &StreamHubChatAdminDialog::ApplyChatSettings);
    settingsButtons->addStretch(); settingsButtons->addWidget(loadSettings); settingsButtons->addWidget(applySettings); settings->addRow(settingsButtons);
    moderationRoot->addWidget(settingsCard); moderationStatus_ = MakeStatus(moderation); moderationRoot->addWidget(moderationStatus_); moderationRoot->addStretch();
    tabs->addTab(moderation, tr("Moderação"));

    auto *rewards = new QWidget(tabs);
    auto *rewardsRoot = new QVBoxLayout(rewards);
    auto *createCard = new QWidget(rewards); createCard->setObjectName("adminCard"); auto *createForm = new QFormLayout(createCard);
    rewardTitle_ = new QLineEdit(createCard); rewardTitle_->setMaxLength(45); rewardCost_ = new QSpinBox(createCard); rewardCost_->setRange(1, 1000000000); rewardCost_->setValue(100); rewardPrompt_ = new QLineEdit(createCard); rewardPrompt_->setMaxLength(200);
    auto *createButton = new QPushButton(tr("Criar recompensa"), createCard); createButton->setObjectName("primaryAction"); connect(createButton, &QPushButton::clicked, this, &StreamHubChatAdminDialog::CreateReward);
    createForm->addRow(tr("Nome"), rewardTitle_); createForm->addRow(tr("Custo"), rewardCost_); createForm->addRow(tr("Instrução"), rewardPrompt_); createForm->addRow(createButton);
    rewardsRoot->addWidget(createCard);

    auto *redemptionCard = new QWidget(rewards); redemptionCard->setObjectName("adminCard"); auto *redemptionLayout = new QVBoxLayout(redemptionCard);
    rewardSelector_ = new QComboBox(redemptionCard); redemptionLayout->addWidget(rewardSelector_);
    redemptionsList_ = new QListWidget(redemptionCard); redemptionLayout->addWidget(redemptionsList_);
    auto *redemptionButtons = new QHBoxLayout(); auto *refresh = new QPushButton(tr("Atualizar"), redemptionCard); auto *fulfill = new QPushButton(tr("Concluir"), redemptionCard); auto *cancel = new QPushButton(tr("Cancelar resgate"), redemptionCard);
    connect(refresh, &QPushButton::clicked, this, &StreamHubChatAdminDialog::RefreshRedemptions); connect(fulfill, &QPushButton::clicked, this, [this]() { ResolveRedemption("FULFILLED"); }); connect(cancel, &QPushButton::clicked, this, [this]() { ResolveRedemption("CANCELED"); });
    redemptionButtons->addWidget(refresh); redemptionButtons->addStretch(); redemptionButtons->addWidget(fulfill); redemptionButtons->addWidget(cancel); redemptionLayout->addLayout(redemptionButtons);
    rewardsRoot->addWidget(redemptionCard, 1); rewardStatus_ = MakeStatus(rewards); rewardsRoot->addWidget(rewardStatus_);
    tabs->addTab(rewards, tr("Recompensas"));

    setStyleSheet(R"(
        QDialog#streamHubAdmin { background:#080c14; color:#f2f7ff; }
        QWidget#adminCard { background:#0d1726; border:1px solid #29496f; border-radius:9px; }
        QLabel#adminStatus { color:#9eb2cb; }
        QTabWidget::pane { border:1px solid #29496f; background:#080c14; }
        QTabBar::tab { padding:8px 14px; background:#101a2a; color:#b8c8dc; border:1px solid #29496f; }
        QTabBar::tab:selected { background:#0077ff; color:white; border-color:#00c8ff; }
        QLineEdit,QComboBox,QSpinBox,QListWidget { background:#080c14; color:white; border:1px solid #29496f; border-radius:7px; padding:6px; }
        QPushButton { background:#17283a; color:white; border:1px solid #29496f; border-radius:7px; padding:7px 11px; }
        QPushButton:hover { border-color:#00c8ff; }
        QPushButton#primaryAction { background:#0077ff; border-color:#00c8ff; font-weight:700; }
    )");
    RefreshRewards();
}

void StreamHubChatAdminDialog::Request(const QByteArray &method, const QString &path, const QJsonObject &body, ReplyHandler handler)
{
    QNetworkRequest request{QUrl(QString("http://127.0.0.1:%1%2").arg(port_).arg(path))}; request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = method == "GET" ? network_->get(request) : network_->sendCustomRequest(request, method, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, handler]() { const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const auto object = QJsonDocument::fromJson(reply->readAll()).object(); reply->deleteLater(); handler(object, status); });
}

void StreamHubChatAdminDialog::RunModeration() { Request("POST", "/api/twitch/moderation", {{"user", moderationUser_->text()}, {"action", moderationAction_->currentData().toString()}, {"duration", timeoutSeconds_->value()}}, [this](const QJsonObject &o, int status) { moderationStatus_->setText(status == 200 ? tr("Ação aplicada.") : o.value("error").toString()); }); }
void StreamHubChatAdminDialog::LoadChatSettings() { Request("GET", "/api/twitch/chat-settings", {}, [this](const QJsonObject &o, int status) { if (status != 200) { moderationStatus_->setText(o.value("error").toString()); return; } const auto d = o.value("data").toObject(); slowMode_->setChecked(d.value("slow_mode").toBool()); followerMode_->setChecked(d.value("follower_mode").toBool()); subscriberMode_->setChecked(d.value("subscriber_mode").toBool()); emoteMode_->setChecked(d.value("emote_mode").toBool()); slowModeSeconds_->setValue(qMax(3, d.value("slow_mode_wait_time").toInt(30))); moderationStatus_->setText(tr("Configurações carregadas.")); }); }
void StreamHubChatAdminDialog::ApplyChatSettings() { Request("PATCH", "/api/twitch/chat-settings", {{"slowMode", slowMode_->isChecked()}, {"slowModeWaitTime", slowModeSeconds_->value()}, {"followerMode", followerMode_->isChecked()}, {"subscriberMode", subscriberMode_->isChecked()}, {"emoteMode", emoteMode_->isChecked()}}, [this](const QJsonObject &o, int status) { moderationStatus_->setText(status == 200 ? tr("Configurações aplicadas.") : o.value("error").toString()); }); }
void StreamHubChatAdminDialog::RefreshRewards() { Request("GET", "/api/twitch/rewards", {}, [this](const QJsonObject &o, int status) { rewardSelector_->clear(); if (status != 200) { rewardStatus_->setText(o.value("error").toString()); return; } for (const auto &v : o.value("data").toArray()) { const auto r = v.toObject(); rewardSelector_->addItem(QString("%1 · %2 pontos").arg(r.value("title").toString()).arg(r.value("cost").toInt()), r.value("id").toString()); } rewardStatus_->setText(rewardSelector_->count() ? tr("Selecione uma recompensa e atualize os resgates.") : tr("Nenhuma recompensa gerenciável pelo StreamHub.")); }); }
void StreamHubChatAdminDialog::CreateReward() { Request("POST", "/api/twitch/rewards", {{"title", rewardTitle_->text()}, {"cost", rewardCost_->value()}, {"prompt", rewardPrompt_->text()}}, [this](const QJsonObject &o, int status) { rewardStatus_->setText(status == 200 ? tr("Recompensa criada.") : o.value("error").toString()); if (status == 200) RefreshRewards(); }); }
void StreamHubChatAdminDialog::RefreshRedemptions() { const QString id = rewardSelector_->currentData().toString(); if (id.isEmpty()) { rewardStatus_->setText(tr("Selecione uma recompensa.")); return; } Request("GET", "/api/twitch/rewards/" + id + "/redemptions", {}, [this](const QJsonObject &o, int status) { redemptionsList_->clear(); if (status != 200) { rewardStatus_->setText(o.value("error").toString()); return; } for (const auto &v : o.value("data").toArray()) { const auto r = v.toObject(); const QString input = r.value("user_input").toString(); auto *item = new QListWidgetItem(input.isEmpty() ? r.value("user_name").toString() : QString("%1 — %2").arg(r.value("user_name").toString(), input), redemptionsList_); item->setData(Qt::UserRole, r.value("id").toString()); } rewardStatus_->setText(redemptionsList_->count() ? tr("Selecione um resgate.") : tr("Não há resgates pendentes.")); }); }
void StreamHubChatAdminDialog::ResolveRedemption(const QString &status) { const QString rewardId = rewardSelector_->currentData().toString(); auto *item = redemptionsList_->currentItem(); if (rewardId.isEmpty() || !item) { rewardStatus_->setText(tr("Selecione um resgate pendente.")); return; } Request("PATCH", "/api/twitch/rewards/" + rewardId + "/redemptions/" + item->data(Qt::UserRole).toString(), {{"status", status}}, [this](const QJsonObject &o, int httpStatus) { if (httpStatus != 200) { rewardStatus_->setText(o.value("error").toString()); return; } RefreshRedemptions(); }); }
