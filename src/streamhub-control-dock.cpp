#include "streamhub-control-dock.h"

#include <QComboBox>
#include <QDesktopServices>
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
#include <QScrollArea>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QLabel *StatusLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    label->setObjectName("controlStatus");
    return label;
}

void LimitText(QTextEdit *edit, QLabel *counter, int maximum)
{
    QString text = edit->toPlainText();
    if (text.size() > maximum) {
        text.truncate(maximum);
        edit->setPlainText(text);
        edit->moveCursor(QTextCursor::End);
    }
    counter->setText(QString("%1/%2").arg(text.size()).arg(maximum));
}

QString FriendlyPlatformError(const QString &platform, const QString &error)
{
    const QString normalized = error.toLower();
    if (normalized.contains("http 400") || normalized.contains("invalid request") ||
        normalized.contains("live não está ativa")) {
        return QObject::tr("%1: live offline ou dados indisponíveis.").arg(platform);
    }
    return QObject::tr("%1: não foi possível atualizar agora.").arg(platform);
}

QString PlatformApplyResult(const QString &platform, bool ok, const QString &message)
{
    if (ok)
        return QObject::tr("%1 atualizada.").arg(platform);
    return FriendlyPlatformError(platform, message);
}
}

StreamHubControlDock::StreamHubControlDock(QWidget *parent) : QWidget(parent)
{
    setObjectName("streamHubControl");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(7);

    auto *header = new QHBoxLayout();
    auto *logo = new QLabel(this);
    logo->setPixmap(QPixmap(":/streamhub-ui/branding/k4-logo.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    header->addWidget(logo);
    auto *heading = new QLabel(tr("Informações de transmissão K4"), this);
    heading->setObjectName("controlHeading");
    header->addWidget(heading);
    header->addStretch();
    root->addLayout(header);

    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("controlTabs");
    root->addWidget(tabs, 1);

    auto *accounts = new QWidget(tabs);
    auto *accountsLayout = new QVBoxLayout(accounts);
    accountsLayout->setContentsMargins(4, 6, 4, 4);
    accountsLayout->setSpacing(4);
    auto *accountIntro = new QLabel(tr("Conecte contas. Dados atualizam automaticamente."), accounts);
    accountIntro->setWordWrap(true);
    accountsLayout->addWidget(accountIntro);
    auto *accountCard = new QWidget(accounts);
    accountCard->setObjectName("accountCard");
    auto *accountCardLayout = new QHBoxLayout(accountCard);
    accountCardLayout->setContentsMargins(8, 6, 8, 6);
    accountCardLayout->setSpacing(8);
    auto *twitchIcon = new QLabel(accountCard);
    twitchIcon->setPixmap(QIcon(":/streamhub-ui/icons/twitch.svg").pixmap(44, 44));
    twitchIcon->setFixedSize(48, 48);
    accountCardLayout->addWidget(twitchIcon);
    auto *accountText = new QVBoxLayout();
    accountName_ = new QLabel(tr("Twitch"), accountCard);
    accountName_->setObjectName("accountName");
    accountStatus_ = new QLabel(tr("● Verificando conta..."), accountCard);
    accountStatus_->setObjectName("accountState");
    accountText->addWidget(accountName_);
    accountText->addWidget(accountStatus_);
    accountCardLayout->addLayout(accountText, 1);
    auto *twitchActions = new QHBoxLayout();
    connectButton_ = new QPushButton(tr("Conectar"), accountCard);
    twitchSyncButton_ = new QPushButton(tr("Sincronizar"), accountCard);
    connectButton_->setObjectName("accountAction");
    twitchSyncButton_->setObjectName("accountAction");
    connect(connectButton_, &QPushButton::clicked, this, &StreamHubControlDock::StartTwitchLogin);
    connect(twitchSyncButton_, &QPushButton::clicked, this, &StreamHubControlDock::SyncTwitchTransmission);
    twitchActions->addWidget(connectButton_);
    twitchActions->addWidget(twitchSyncButton_);
    accountCardLayout->addLayout(twitchActions);
    accountsLayout->addWidget(accountCard);

    auto *kickCard = new QWidget(accounts);
    kickCard->setObjectName("accountCard");
    auto *kickCardLayout = new QHBoxLayout(kickCard);
    kickCardLayout->setContentsMargins(8, 6, 8, 6);
    kickCardLayout->setSpacing(8);
    auto *kickIcon = new QLabel(kickCard);
    kickIcon->setPixmap(QIcon(":/streamhub-ui/icons/kick.svg").pixmap(44, 44));
    kickIcon->setFixedSize(48, 48);
    kickCardLayout->addWidget(kickIcon);
    auto *kickText = new QVBoxLayout();
    kickAccountName_ = new QLabel(tr("Kick"), kickCard);
    kickAccountName_->setObjectName("accountName");
    kickAccountStatus_ = new QLabel(tr("● Verificando conta..."), kickCard);
    kickAccountStatus_->setObjectName("accountState");
    kickText->addWidget(kickAccountName_);
    kickText->addWidget(kickAccountStatus_);
    kickCardLayout->addLayout(kickText, 1);
    auto *kickActions = new QHBoxLayout();
    kickConnectButton_ = new QPushButton(tr("Conectar"), kickCard);
    kickSyncButton_ = new QPushButton(tr("Sincronizar"), kickCard);
    kickConnectButton_->setObjectName("accountAction");
    kickSyncButton_->setObjectName("accountAction");
    connect(kickConnectButton_, &QPushButton::clicked, this, &StreamHubControlDock::StartKickLogin);
    connect(kickSyncButton_, &QPushButton::clicked, this, &StreamHubControlDock::SyncKickTransmission);
    kickActions->addWidget(kickConnectButton_);
    kickActions->addWidget(kickSyncButton_);
    kickCardLayout->addLayout(kickActions);
    accountsLayout->addWidget(kickCard);

    auto *youtubeCard = new QWidget(accounts);
    youtubeCard->setObjectName("accountCard");
    auto *youtubeCardLayout = new QHBoxLayout(youtubeCard);
    youtubeCardLayout->setContentsMargins(8, 6, 8, 6);
    youtubeCardLayout->setSpacing(8);
    auto *youtubeIcon = new QLabel(youtubeCard);
    youtubeIcon->setPixmap(QIcon(":/streamhub-ui/icons/youtube.svg").pixmap(44, 44));
    youtubeIcon->setFixedSize(48, 48);
    youtubeCardLayout->addWidget(youtubeIcon);
    auto *youtubeText = new QVBoxLayout();
    youtubeAccountName_ = new QLabel(tr("YouTube"), youtubeCard);
    youtubeAccountName_->setObjectName("accountName");
    youtubeAccountStatus_ = new QLabel(tr("● Verificando conta..."), youtubeCard);
    youtubeAccountStatus_->setObjectName("accountState");
    youtubeText->addWidget(youtubeAccountName_);
    youtubeText->addWidget(youtubeAccountStatus_);
    youtubeCardLayout->addLayout(youtubeText, 1);
    auto *youtubeActions = new QHBoxLayout();
    youtubeConnectButton_ = new QPushButton(tr("Conectar"), youtubeCard);
    youtubeSyncButton_ = new QPushButton(tr("Sincronizar"), youtubeCard);
    youtubeConnectButton_->setObjectName("accountAction");
    youtubeSyncButton_->setObjectName("accountAction");
    connect(youtubeConnectButton_, &QPushButton::clicked, this, &StreamHubControlDock::StartYoutubeLogin);
    connect(youtubeSyncButton_, &QPushButton::clicked, this, &StreamHubControlDock::SyncYoutubeTransmission);
    youtubeActions->setSpacing(6);
    youtubeActions->addWidget(youtubeConnectButton_);
    youtubeActions->addWidget(youtubeSyncButton_);
    youtubeCardLayout->addLayout(youtubeActions);
    accountsLayout->addWidget(youtubeCard);

    loginHelp_ = StatusLabel(accounts);
    loginHelp_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    loginHelp_->setOpenExternalLinks(true);
    accountsLayout->addWidget(loginHelp_);
    kickLoginHelp_ = StatusLabel(accounts);
    kickLoginHelp_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    kickLoginHelp_->setOpenExternalLinks(true);
    accountsLayout->addWidget(kickLoginHelp_);
    youtubeLoginHelp_ = StatusLabel(accounts);
    youtubeLoginHelp_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    youtubeLoginHelp_->setOpenExternalLinks(true);
    accountsLayout->addWidget(youtubeLoginHelp_);
    accountsLayout->addStretch();
    tabs->addTab(accounts, QIcon(":/streamhub-ui/icons/twitch.svg"), tr("Contas"));

    auto *broadcastScroll = new QScrollArea(tabs);
    broadcastScroll->setWidgetResizable(true);
    broadcastScroll->setFrameShape(QFrame::NoFrame);
    auto *broadcast = new QWidget(broadcastScroll);
    auto *broadcastLayout = new QVBoxLayout(broadcast);
    broadcastLayout->setContentsMargins(6, 6, 6, 6);
    broadcastLayout->setSpacing(6);

    auto *preview = new QWidget(broadcast);
    preview->setObjectName("livePreview");
    auto *previewLayout = new QHBoxLayout(preview);
    previewLayout->setContentsMargins(7, 6, 7, 6);
    previewLayout->setSpacing(8);
    previewCover_ = new QLabel(preview);
    previewCover_->setFixedSize(50, 64);
    previewCover_->setAlignment(Qt::AlignCenter);
    previewCover_->setPixmap(QIcon(":/streamhub-ui/icons/twitch.svg").pixmap(38, 38));
    previewLayout->addWidget(previewCover_);
    auto *previewText = new QVBoxLayout();
    auto *previewCaption = new QLabel(tr("PRÉVIA DA LIVE"), preview);
    previewCaption->setObjectName("previewCaption");
    previewTitle_ = new QLabel(tr("Título da transmissão"), preview);
    previewTitle_->setObjectName("previewTitle"); previewTitle_->setWordWrap(true);
    previewCategory_ = new QLabel(tr("Nenhuma categoria selecionada"), preview);
    previewCategory_->setObjectName("previewMeta");
    previewNotification_ = new QLabel(tr("A prévia é atualizada enquanto você edita."), preview);
    previewNotification_->setObjectName("previewNotification"); previewNotification_->setWordWrap(true);
    previewText->addWidget(previewCaption); previewText->addWidget(previewTitle_); previewText->addWidget(previewCategory_); previewText->addWidget(previewNotification_);
    previewLayout->addLayout(previewText, 1);
    broadcastLayout->addWidget(preview);

    auto *syncCard = new QWidget(broadcast);
    syncCard->setObjectName("formCard");
    auto *syncLayout = new QVBoxLayout(syncCard);
    syncLayout->setContentsMargins(10, 8, 10, 8);
    syncLayout->setSpacing(4);
    auto *syncTitle = new QLabel(tr("Sincronização das lives"), syncCard);
    syncTitle->setObjectName("previewCaption");
    syncLayout->addWidget(syncTitle);
    twitchTransmissionStatus_ = StatusLabel(syncCard);
    kickTransmissionStatus_ = StatusLabel(syncCard);
    youtubeTransmissionStatus_ = StatusLabel(syncCard);
    twitchTransmissionStatus_->setText(tr("Twitch: aguardando sincronização."));
    kickTransmissionStatus_->setText(tr("Kick: aguardando sincronização."));
    youtubeTransmissionStatus_->setText(tr("YouTube: aguardando sincronização."));
    syncLayout->addWidget(twitchTransmissionStatus_);
    syncLayout->addWidget(kickTransmissionStatus_);
    syncLayout->addWidget(youtubeTransmissionStatus_);
    broadcastLayout->addWidget(syncCard);

    auto *formCard = new QWidget(broadcast);
    formCard->setObjectName("formCard");
    auto *form = new QFormLayout(formCard);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    title_ = new QTextEdit(formCard); title_->setFixedHeight(64); title_->setPlaceholderText(tr("Dê um título claro para sua live"));
    titleCount_ = new QLabel("0/140", formCard); titleCount_->setObjectName("counter");
    auto *titleBox = new QVBoxLayout(); titleBox->addWidget(title_); titleBox->addWidget(titleCount_, 0, Qt::AlignRight);
    notification_ = new QTextEdit(formCard); notification_->setFixedHeight(58); notification_->setPlaceholderText(tr("Texto usado pelas plataformas que aceitam notificação"));
    notificationCount_ = new QLabel("0/140", formCard); notificationCount_->setObjectName("counter");
    auto *notificationBox = new QVBoxLayout(); notificationBox->addWidget(notification_); notificationBox->addWidget(notificationCount_, 0, Qt::AlignRight);
    category_ = new QLineEdit(formCard); category_->setPlaceholderText(tr("Digite para procurar um jogo ou categoria"));
    categoryResults_ = new QListWidget(formCard); categoryResults_->setObjectName("categoryResults"); categoryResults_->setIconSize(QSize(40, 56)); categoryResults_->setMaximumHeight(170); categoryResults_->hide();
    auto *categoryBox = new QVBoxLayout(); categoryBox->addWidget(category_); categoryBox->addWidget(categoryResults_);
    visibility_ = new QComboBox(formCard); visibility_->addItems({tr("Público"), tr("Não listado"), tr("Privado")});
    tags_ = new QLineEdit(formCard); tags_->setPlaceholderText(tr("gameplay, português, comunidade"));
    language_ = new QComboBox(formCard); language_->addItem(tr("Português"), "pt"); language_->addItem(tr("Inglês"), "en"); language_->addItem(tr("Espanhol"), "es"); language_->addItem(tr("Outro"), "other");
    classification_ = new QComboBox(formCard);
    classification_->addItem(tr("Nenhuma classificação adicional"), "");
    classification_->addItem(tr("Linguagem imprópria"), "ProfanityVulgarity");
    classification_->addItem(tr("Violência gráfica"), "ViolentGraphic");
    classification_->addItem(tr("Temas políticos e sociais"), "DebatedSocialIssuesAndPolitics");
    classification_->addItem(tr("Jogos de azar"), "Gambling");
    form->addRow(tr("Título"), titleBox);
    form->addRow(tr("Notificação ao vivo"), notificationBox);
    form->addRow(tr("Categoria/jogo"), categoryBox);
    form->addRow(tr("Público"), visibility_);
    form->addRow(tr("Marcações/#"), tags_);
    form->addRow(tr("Idioma"), language_);
    form->addRow(tr("Classificação"), classification_);
    broadcastLayout->addWidget(formCard);

    auto *buttons = new QHBoxLayout();
    auto *loadButton = new QPushButton(tr("Carregar atuais"), broadcast);
    auto *applyButton = new QPushButton(tr("Aplicar em todas"), broadcast); applyButton->setObjectName("primaryAction");
    connect(loadButton, &QPushButton::clicked, this, &StreamHubControlDock::LoadBroadcast);
    connect(applyButton, &QPushButton::clicked, this, &StreamHubControlDock::ApplyBroadcast);
    buttons->addStretch(); buttons->addWidget(loadButton); buttons->addWidget(applyButton);
    broadcastLayout->addLayout(buttons);
    broadcastStatus_ = StatusLabel(broadcast);
    broadcastLayout->addWidget(broadcastStatus_);
    broadcastLayout->addStretch();
    broadcastScroll->setWidget(broadcast);
    tabs->addTab(broadcastScroll, QIcon(":/streamhub-ui/icons/camera.svg"), tr("Transmissão"));

    network_ = new QNetworkAccessManager(this);
    loginTimer_ = new QTimer(this); loginTimer_->setSingleShot(true);
    categoryTimer_ = new QTimer(this); categoryTimer_->setSingleShot(true); categoryTimer_->setInterval(350);
    connect(category_, &QLineEdit::textEdited, this, [this]() { categoryId_.clear(); categoryTimer_->start(); UpdatePreview(); });
    connect(categoryTimer_, &QTimer::timeout, this, &StreamHubControlDock::SearchCategories);
    connect(categoryResults_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        categoryId_ = item->data(Qt::UserRole).toString(); category_->setText(item->text()); categoryResults_->hide();
        previewCover_->setPixmap(item->icon().pixmap(52, 72));
        LoadCategoryCover(categoryId_);
        UpdatePreview();
    });
    connect(title_, &QTextEdit::textChanged, this, [this]() { LimitText(title_, titleCount_, 140); UpdatePreview(); });
    connect(notification_, &QTextEdit::textChanged, this, [this]() { LimitText(notification_, notificationCount_, 140); UpdatePreview(); });
    connect(tags_, &QLineEdit::textChanged, this, &StreamHubControlDock::UpdatePreview);
    connect(language_, &QComboBox::currentIndexChanged, this, &StreamHubControlDock::UpdatePreview);
    kickLoginTimer_ = new QTimer(this);
    kickLoginTimer_->setSingleShot(true);
    youtubeLoginTimer_ = new QTimer(this);
    youtubeLoginTimer_->setSingleShot(true);
    RefreshKickAccount();
    RefreshYoutubeAccount();

    setStyleSheet(R"(
        QWidget#streamHubControl { background:#080c14; color:#f2f7ff; }
        QLabel#controlHeading { font-size:18px; font-weight:800; color:#f2f7ff; }
        QTabWidget#controlTabs::pane { border:1px solid #29496f; border-radius:8px; background:#080c14; }
        QTabBar::tab { background:#101a2a; border:1px solid #29496f; padding:8px 14px; color:#b8c8dc; }
        QTabBar::tab:selected { color:white; background:#0077ff; border-color:#00c8ff; }
        QWidget#accountCard, QWidget#formCard, QWidget#livePreview { background:#0d1726; border:1px solid #29496f; border-radius:10px; }
        QLabel#accountName { font-size:16px; font-weight:800; }
        QLabel#accountState[connected="true"] { color:#16d86a; }
        QLabel#accountState[connected="false"] { color:#9eb2cb; }
        QLabel#previewCaption { color:#9146ff; font-size:10px; font-weight:800; }
        QLabel#previewTitle { color:white; font-size:15px; font-weight:800; }
        QLabel#previewMeta, QLabel#previewNotification, QLabel#controlStatus, QLabel#counter { color:#9eb2cb; }
        QLabel#counter { font-size:10px; }
        QPushButton { min-height:30px; padding:5px 11px; background:#17283a; color:white; border:1px solid #29496f; border-radius:7px; }
        QPushButton:hover { border-color:#00c8ff; background:#1b3248; }
        QPushButton#primaryAction { background:#9146ff; border-color:#b987ff; font-weight:800; }
        QPushButton#accountAction { min-width:95px; }
        QLineEdit,QComboBox,QTextEdit,QListWidget { padding:6px; background:#080c14; color:#f2f7ff; border:1px solid #29496f; border-radius:7px; }
        QLineEdit:focus,QComboBox:focus,QTextEdit:focus { border-color:#9146ff; }
        QListWidget#categoryResults::item { padding:6px; }
        QListWidget#categoryResults::item:selected { background:#27334d; }
        QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; border:none; }
    )");
}

void StreamHubControlDock::ConnectTo(int port)
{
    port_ = port;
    RefreshAccounts();
}

void StreamHubControlDock::SetPrimaryPlatform(const QString &platform)
{
    const QString normalized = platform.trimmed().toLower();
    if (normalized != "twitch" && normalized != "kick" && normalized != "youtube")
        return;
    if (primaryPlatform_ == normalized)
        return;

    primaryPlatform_ = normalized;
    const QJsonObject *transmission = nullptr;
    if (normalized == "twitch" && !twitchTransmission_.isEmpty())
        transmission = &twitchTransmission_;
    else if (normalized == "kick" && !kickTransmission_.isEmpty())
        transmission = &kickTransmission_;
    else if (normalized == "youtube" && !youtubeTransmission_.isEmpty())
        transmission = &youtubeTransmission_;

    if (transmission)
        ApplyTransmissionMetadata(*transmission, primaryPlatform_);
}

void StreamHubControlDock::RefreshAccounts()
{
    kickLoginHelp_->clear();
    youtubeLoginHelp_->clear();
    RefreshAccount();
    RefreshKickAccount();
    RefreshYoutubeAccount();
}

void StreamHubControlDock::Request(const QByteArray &method, const QString &path, const QJsonObject &body, ReplyHandler handler)
{
    QNetworkRequest request{QUrl(QString("http://localhost:%1%2").arg(port_).arg(path))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = method == "GET" ? network_->get(request) : network_->sendCustomRequest(request, method, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, handler]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object(); reply->deleteLater(); handler(object, status);
    });
}

void StreamHubControlDock::RefreshAccount()
{
    Request("GET", "/api/accounts/twitch/status", {}, [this](const QJsonObject &o, int status) {
        if (status != 200) { accountStatus_->setText(tr("● Aguardando o serviço local...")); accountStatus_->setProperty("connected", false); QTimer::singleShot(1500, this, &StreamHubControlDock::RefreshAccount); return; }
        const bool connected = o.value("connected").toBool(); const bool reconnect = o.value("needsReconnect").toBool();
        accountStatus_->setProperty("connected", connected && !reconnect); accountStatus_->style()->unpolish(accountStatus_); accountStatus_->style()->polish(accountStatus_);
        if (!connected) { accountName_->setText(tr("Twitch")); accountStatus_->setText(tr("● Não conectada")); connectButton_->setText(tr("Conectar")); twitchSyncButton_->setEnabled(false); }
        else if (reconnect) { accountName_->setText(QString("Twitch · %1").arg(o.value("name").toString())); accountStatus_->setText(tr("● Precisa de novas permissões")); connectButton_->setText(tr("Reconectar")); twitchSyncButton_->setEnabled(false); }
        else {
            accountName_->setText(QString("Twitch · %1").arg(o.value("name").toString()));
            accountStatus_->setText(tr("● Conectada"));
            connectButton_->setText(tr("Trocar conta"));
            twitchSyncButton_->setEnabled(true);
            emit twitchConnected();
            QTimer::singleShot(0, this, &StreamHubControlDock::SyncTwitchTransmission);
        }
        connectButton_->setEnabled(true);
    });
}

void StreamHubControlDock::StartTwitchLogin()
{
    connectButton_->setEnabled(false); accountStatus_->setText(tr("● Preparando autorização..."));
    Request("POST", "/api/accounts/twitch/connect", {}, [this](const QJsonObject &o, int status) {
        if (status != 200) { accountStatus_->setText(o.value("error").toString()); connectButton_->setEnabled(true); return; }
        const QString url = o.value("verificationUri").toString();
        loginHelp_->setText(tr("Use o código <b>%1</b> em <a href=\"%2\">%2</a>").arg(o.value("userCode").toString(), url));
        QDesktopServices::openUrl(QUrl(url)); PollTwitchLogin(o.value("flowId").toString(), o.value("interval").toInt(5));
    });
}

void StreamHubControlDock::PollTwitchLogin(const QString &flowId, int intervalSeconds)
{
    loginTimer_->start(qMax(3, intervalSeconds) * 1000);
    connect(loginTimer_, &QTimer::timeout, this, [this, flowId, intervalSeconds]() {
        Request("GET", "/api/accounts/twitch/connect/" + flowId, {}, [this, flowId, intervalSeconds](const QJsonObject &o, int status) {
            if (status != 200) { accountStatus_->setText(o.value("error").toString()); connectButton_->setEnabled(true); return; }
            if (o.value("state").toString() == "connected") { loginHelp_->clear(); RefreshAccount(); return; }
            PollTwitchLogin(flowId, o.value("interval").toInt(intervalSeconds));
        });
    }, Qt::SingleShotConnection);
}

void StreamHubControlDock::SyncTwitchTransmission()
{
    twitchSyncButton_->setEnabled(false);
    SetTransmissionSyncNotice("twitch", tr("Twitch: sincronizando informações da transmissão..."));
    loginHelp_->setText(tr("Buscando dados oficiais da Twitch..."));
    emit twitchTransmissionRequested();
}

void StreamHubControlDock::ApplyTransmissionMetadata(const QJsonObject &transmission,
                                                       const QString &platform)
{
    const QString title = transmission.value("title").toString().trimmed();
    const QString category = transmission.value("category").toString().trimmed();
    const QString language = transmission.value("language").toString().trimmed();
    if (!title.isEmpty())
        title_->setPlainText(title);
    if (!category.isEmpty())
        category_->setText(category);
    if (platform.compare("twitch", Qt::CaseInsensitive) == 0) {
        categoryId_ = transmission.value("categoryId").toString().trimmed();
        const QJsonArray labels = transmission.value("classificationLabels").toArray();
        const int classificationIndex = labels.isEmpty()
                                            ? 0
                                            : classification_->findData(labels.first().toString());
        classification_->setCurrentIndex(qMax(0, classificationIndex));
        LoadCategoryCover(categoryId_);
    } else {
        categoryId_.clear();
    }

    const QJsonArray tags = transmission.value("tags").toArray();
    if (!tags.isEmpty()) {
        QStringList values;
        for (const auto &tag : tags)
            values << tag.toString().trimmed();
        tags_->setText(values.join(", "));
    }

    const int languageIndex = language_->findData(language);
    if (languageIndex >= 0)
        language_->setCurrentIndex(languageIndex);

    const QString visibility = transmission.value("visibility").toString().trimmed().toLower();
    if (!visibility.isEmpty()) {
        const int visibilityIndex = visibility == "public" ? 0 : visibility == "unlisted" ? 1 : 2;
        visibility_->setCurrentIndex(visibilityIndex);
    }
    UpdatePreview();
}

void StreamHubControlDock::SetTransmissionSyncNotice(const QString &platform,
                                                           const QString &message)
{
    if (platform.compare("twitch", Qt::CaseInsensitive) == 0)
        twitchSyncNotice_ = message;
    else if (platform.compare("kick", Qt::CaseInsensitive) == 0)
        kickSyncNotice_ = message;
    else if (platform.compare("youtube", Qt::CaseInsensitive) == 0)
        youtubeSyncNotice_ = message;
    UpdateTransmissionSyncNotice();
}

void StreamHubControlDock::UpdateTransmissionSyncNotice()
{
    const QString twitch = twitchSyncNotice_.isEmpty()
                               ? tr("Twitch: aguardando sincronização.")
                               : twitchSyncNotice_;
    const QString kick = kickSyncNotice_.isEmpty()
                             ? tr("Kick: aguardando sincronização.")
                             : kickSyncNotice_;
    const QString youtube = youtubeSyncNotice_.isEmpty()
                                ? tr("YouTube: aguardando sincronização.")
                                : youtubeSyncNotice_;

    if (twitchTransmissionStatus_)
        twitchTransmissionStatus_->setText(twitch);
    if (kickTransmissionStatus_)
        kickTransmissionStatus_->setText(kick);
    if (youtubeTransmissionStatus_)
        youtubeTransmissionStatus_->setText(youtube);
    Q_UNUSED(twitch);
    Q_UNUSED(kick);
    Q_UNUSED(youtube);
}

void StreamHubControlDock::SetTwitchTransmission(const QJsonObject &transmission)
{
    const QString server = transmission.value("server").toString().trimmed();
    const QString key = transmission.value("streamKey").toString().trimmed();
    if (server.isEmpty() || key.isEmpty()) {
        SetTwitchTransmissionError(tr("Twitch não forneceu servidor RTMP e stream key válidos."));
        return;
    }
    twitchTransmission_ = transmission;
    if (primaryPlatform_ == "twitch")
        ApplyTransmissionMetadata(twitchTransmission_, "twitch");
    SetTransmissionSyncNotice("twitch", tr("Twitch: sincronizada."));
    twitchSyncButton_->setEnabled(true);
    loginHelp_->clear();
    emit twitchTransmissionReceived(twitchTransmission_);
}

void StreamHubControlDock::SetTwitchTransmissionError(const QString &error)
{
    twitchSyncButton_->setEnabled(true);
    SetTransmissionSyncNotice("twitch", tr("Twitch: falha na sincronização: %1").arg(error));
    loginHelp_->setText(tr("Sincronização Twitch: %1").arg(error));
}

void StreamHubControlDock::RefreshKickAccount()
{
    Request("GET", "/api/accounts/kick/status", {}, [this](const QJsonObject &o, int status) {
        if (status != 200) {
            kickAccountStatus_->setText(tr("● Aguardando o serviço local..."));
            kickAccountStatus_->setProperty("connected", false);
            QTimer::singleShot(1500, this, &StreamHubControlDock::RefreshKickAccount);
            return;
        }
        const bool configured = o.value("configured").toBool();
        const bool connected = o.value("connected").toBool();
        const bool reconnect = o.value("needsReconnect").toBool();
        kickAccountStatus_->setProperty("connected", connected && !reconnect);
        kickAccountStatus_->style()->unpolish(kickAccountStatus_);
        kickAccountStatus_->style()->polish(kickAccountStatus_);
        if (!configured) {
            kickAccountName_->setText(tr("Kick"));
            kickAccountStatus_->setText(tr("● OAuth compartilhado indisponível · configure .env"));
            kickConnectButton_->setEnabled(false);
            kickSyncButton_->setEnabled(false);
        } else if (!connected) {
            kickAccountName_->setText(tr("Kick"));
            kickAccountStatus_->setText(tr("● Não conectada"));
            kickConnectButton_->setText(tr("Conectar"));
            kickConnectButton_->setEnabled(true);
            kickSyncButton_->setEnabled(false);
        } else if (reconnect) {
            kickAccountName_->setText(QString("Kick · %1").arg(o.value("name").toString()));
            kickAccountStatus_->setText(tr("● Precisa de novas permissões"));
            kickConnectButton_->setText(tr("Reconectar"));
            kickConnectButton_->setEnabled(true);
            kickSyncButton_->setEnabled(false);
        } else {
            kickAccountName_->setText(QString("Kick · %1").arg(o.value("name").toString()));
            kickAccountStatus_->setText(tr("● Conectada"));
            kickLoginHelp_->clear();
            kickConnectButton_->setText(tr("Trocar conta"));
            kickConnectButton_->setEnabled(true);
            kickSyncButton_->setEnabled(true);
            QTimer::singleShot(0, this, &StreamHubControlDock::SyncKickTransmission);
        }
    });
}

void StreamHubControlDock::StartKickLogin()
{
    kickConnectButton_->setEnabled(false);
    kickAccountStatus_->setProperty("connected", false);
    kickAccountStatus_->style()->unpolish(kickAccountStatus_);
    kickAccountStatus_->style()->polish(kickAccountStatus_);
    kickAccountStatus_->setText(tr("● Preparando autorização..."));
    Request("POST", "/api/accounts/kick/connect", {}, [this](const QJsonObject &o, int status) {
        if (status != 200) {
            kickAccountStatus_->setProperty("connected", false);
            kickAccountStatus_->style()->unpolish(kickAccountStatus_);
            kickAccountStatus_->style()->polish(kickAccountStatus_);
            kickAccountStatus_->setText(o.value("error").toString());
            kickConnectButton_->setEnabled(true);
            return;
        }
        const QString url = o.value("authorizationUri").toString();
        kickLoginHelp_->setText(tr("Autorização Kick aberta no navegador."));
        QDesktopServices::openUrl(QUrl(url));
        PollKickLogin(o.value("flowId").toString());
    });
}

void StreamHubControlDock::PollKickLogin(const QString &flowId)
{
    kickLoginTimer_->start(3000);
    connect(kickLoginTimer_, &QTimer::timeout, this, [this, flowId]() {
        Request("GET", "/api/accounts/kick/connect/" + flowId, {}, [this, flowId](const QJsonObject &o, int status) {
            if (status != 200) {
                kickAccountStatus_->setProperty("connected", false);
                kickAccountStatus_->style()->unpolish(kickAccountStatus_);
                kickAccountStatus_->style()->polish(kickAccountStatus_);
                kickAccountStatus_->setText(o.value("error").toString());
                kickConnectButton_->setEnabled(true);
                return;
            }
            if (o.value("state").toString() == "connected") {
                kickLoginHelp_->clear();
                RefreshKickAccount();
                return;
            }
            PollKickLogin(flowId);
        });
    }, Qt::SingleShotConnection);
}

void StreamHubControlDock::SyncKickTransmission()
{
    kickSyncButton_->setEnabled(false);
    SetTransmissionSyncNotice("kick", tr("Kick: sincronizando informações da transmissão..."));
    kickLoginHelp_->setText(tr("Buscando dados oficiais da Kick..."));
    emit kickTransmissionRequested();
}

void StreamHubControlDock::SetKickTransmission(const QJsonObject &transmission)
{
    const QString server = transmission.value("server").toString().trimmed();
    const QString key = transmission.value("streamKey").toString().trimmed();
    if (server.isEmpty() || key.isEmpty()) {
        SetKickTransmissionError(tr("Kick não forneceu servidor RTMP e stream key válidos."));
        return;
    }
    kickTransmission_ = transmission;
    const bool isLive = kickTransmission_.value("isLive").toBool();
    if (primaryPlatform_ == "kick")
        ApplyTransmissionMetadata(kickTransmission_, "kick");
    SetTransmissionSyncNotice("kick", isLive ? tr("Kick: sincronizada.") : tr("Kick: offline."));
    kickSyncButton_->setEnabled(true);
    kickLoginHelp_->clear();
    emit kickTransmissionReceived(kickTransmission_);
}

void StreamHubControlDock::SetKickTransmissionError(const QString &error)
{
    kickSyncButton_->setEnabled(true);
    SetTransmissionSyncNotice("kick", tr("Kick: falha na sincronização: %1").arg(error));
    kickLoginHelp_->setText(tr("Sincronização Kick: %1").arg(error));
}

void StreamHubControlDock::StartYoutubeLogin()
{
    youtubeConnectButton_->setEnabled(false);
    youtubeAccountStatus_->setProperty("connected", false);
    youtubeAccountStatus_->style()->unpolish(youtubeAccountStatus_);
    youtubeAccountStatus_->style()->polish(youtubeAccountStatus_);
    youtubeAccountStatus_->setText(tr("● Preparando autorização..."));
    Request("POST", "/api/accounts/youtube/connect", {}, [this](const QJsonObject &o, int status) {
        if (status != 200) {
            youtubeAccountStatus_->setProperty("connected", false);
            youtubeAccountStatus_->style()->unpolish(youtubeAccountStatus_);
            youtubeAccountStatus_->style()->polish(youtubeAccountStatus_);
            youtubeAccountStatus_->setText(o.value("error").toString());
            youtubeConnectButton_->setEnabled(true);
            return;
        }
        const QString url = o.value("authorizationUri").toString();
        youtubeLoginHelp_->setText(tr("Autorização YouTube aberta no navegador."));
        QDesktopServices::openUrl(QUrl(url));
        PollYoutubeLogin(o.value("flowId").toString());
    });
}

void StreamHubControlDock::PollYoutubeLogin(const QString &flowId)
{
    youtubeLoginTimer_->start(3000);
    connect(youtubeLoginTimer_, &QTimer::timeout, this, [this, flowId]() {
        Request("GET", "/api/accounts/youtube/connect/" + flowId, {}, [this, flowId](const QJsonObject &o, int status) {
            if (status != 200) {
                youtubeAccountStatus_->setProperty("connected", false);
                youtubeAccountStatus_->style()->unpolish(youtubeAccountStatus_);
                youtubeAccountStatus_->style()->polish(youtubeAccountStatus_);
                youtubeAccountStatus_->setText(o.value("error").toString());
                youtubeConnectButton_->setEnabled(true);
                return;
            }
            if (o.value("state").toString() == "connected") {
                youtubeLoginHelp_->clear();
                RefreshYoutubeAccount();
                return;
            }
            PollYoutubeLogin(flowId);
        });
    }, Qt::SingleShotConnection);
}

void StreamHubControlDock::RefreshYoutubeAccount()
{
    Request("GET", "/api/accounts/youtube/status", {}, [this](const QJsonObject &o, int status) {
        if (status != 200) {
            youtubeAccountStatus_->setText(tr("● Aguardando o serviço local..."));
            youtubeAccountStatus_->setProperty("connected", false);
            QTimer::singleShot(1500, this, &StreamHubControlDock::RefreshYoutubeAccount);
            return;
        }
        const bool configured = o.value("configured").toBool();
        const bool connected = o.value("connected").toBool();
        const bool reconnect = o.value("needsReconnect").toBool();
        const QString source = o.value("credentialSource").toString();
        youtubeAccountStatus_->setProperty("connected", connected && !reconnect);
        youtubeAccountStatus_->style()->unpolish(youtubeAccountStatus_);
        youtubeAccountStatus_->style()->polish(youtubeAccountStatus_);
        if (!configured) {
            youtubeAccountName_->setText(tr("YouTube"));
            youtubeAccountStatus_->setText(tr("● OAuth compartilhado indisponível · configure .env"));
            youtubeConnectButton_->setEnabled(false);
            youtubeSyncButton_->setEnabled(false);
        } else if (!connected) {
            youtubeAccountName_->setText(tr("YouTube"));
            youtubeAccountStatus_->setText(source == "env" ? tr("● Não conectada · credenciais de .env") : tr("● Não conectada"));
            youtubeConnectButton_->setText(tr("Conectar"));
            youtubeConnectButton_->setEnabled(true);
            youtubeSyncButton_->setEnabled(false);
        } else if (reconnect) {
            youtubeAccountName_->setText(QString("YouTube · %1").arg(o.value("name").toString()));
            youtubeAccountStatus_->setText(tr("● Precisa de novas permissões"));
            youtubeConnectButton_->setText(tr("Reconectar"));
            youtubeConnectButton_->setEnabled(true);
            youtubeSyncButton_->setEnabled(false);
        } else {
            youtubeAccountName_->setText(QString("YouTube · %1").arg(o.value("name").toString()));
            youtubeAccountStatus_->setText(tr("● Conectada"));
            youtubeLoginHelp_->clear();
            youtubeConnectButton_->setText(tr("Trocar conta"));
            youtubeConnectButton_->setEnabled(true);
            youtubeSyncButton_->setEnabled(true);
            QTimer::singleShot(0, this, &StreamHubControlDock::SyncYoutubeTransmission);
        }
    });
}

void StreamHubControlDock::SyncYoutubeTransmission()
{
    youtubeSyncButton_->setEnabled(false);
    SetTransmissionSyncNotice("youtube", tr("YouTube: sincronizando informações da transmissão..."));
    youtubeLoginHelp_->setText(tr("Buscando dados oficiais do YouTube..."));
    emit youtubeTransmissionRequested();
}

void StreamHubControlDock::SetYoutubeTransmission(const QJsonObject &transmission)
{
    const QString server = transmission.value("server").toString().trimmed();
    const QString key = transmission.value("streamKey").toString().trimmed();
    if (server.isEmpty() || key.isEmpty()) {
        SetYoutubeTransmissionError(tr("YouTube não forneceu servidor RTMP e stream key válidos."));
        return;
    }
    youtubeTransmission_ = transmission;
    const QString title = youtubeTransmission_.value("title").toString().trimmed();
    const QString category = youtubeTransmission_.value("category").toString().trimmed();
    const bool isLive = youtubeTransmission_.value("isLive").toBool();
    if (primaryPlatform_ == "youtube")
        ApplyTransmissionMetadata(youtubeTransmission_, "youtube");
    QStringList details;
    if (isLive && !title.isEmpty())
        details << tr("título: %1").arg(title);
    if (isLive && !category.isEmpty())
        details << tr("categoria: %1").arg(category);
    SetTransmissionSyncNotice(
        "youtube",
        isLive && !details.isEmpty()
            ? tr("YouTube: sincronizada · %1").arg(details.join(tr(" · ")))
            : tr("YouTube: RTMP e stream key sincronizados · live não está ativa."));
    youtubeSyncButton_->setEnabled(true);
    youtubeLoginHelp_->setText(tr("Transmissão YouTube sincronizada com Múltiplas saídas."));
    emit youtubeTransmissionReceived(youtubeTransmission_);
}

void StreamHubControlDock::SetYoutubeTransmissionError(const QString &error)
{
    youtubeSyncButton_->setEnabled(true);
    SetTransmissionSyncNotice("youtube", tr("YouTube: falha na sincronização: %1").arg(error));
    youtubeLoginHelp_->setText(tr("Sincronização YouTube: %1").arg(error));
}

void StreamHubControlDock::LoadCategoryCover(const QString &categoryId)
{
    const QString id = categoryId.trimmed();
    if (id.isEmpty()) {
        previewCover_->setPixmap(QIcon(":/streamhub-ui/icons/twitch.svg").pixmap(38, 38));
        return;
    }

    const QString path = QString("http://localhost:%1/api/twitch/category-cover?id=%2")
                             .arg(port_)
                             .arg(QString::fromUtf8(QUrl::toPercentEncoding(id)));
    QNetworkReply *reply = network_->get(QNetworkRequest(QUrl(path)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        const QByteArray payload = reply->readAll();
        QPixmap art;
        art.loadFromData(payload);
        const bool valid = reply->error() == QNetworkReply::NoError && !art.isNull();
        reply->deleteLater();
        if (valid && categoryId_ == id)
            previewCover_->setPixmap(art.scaled(previewCover_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
}

void StreamHubControlDock::LoadBroadcast()
{
    if (primaryPlatform_.isEmpty()) {
        broadcastStatus_->setText(tr("Serviço principal OBS não identificado."));
        return;
    }
    Request("GET", "/api/broadcast/current?platform=" + primaryPlatform_, {}, [this](const QJsonObject &o, int status) {
        if (status != 200) { broadcastStatus_->setText(o.value("error").toString()); return; }
        ApplyTransmissionMetadata(o, primaryPlatform_);
        broadcastStatus_->setText(tr("Informações atuais de %1 carregadas.").arg(primaryPlatform_));
    });
}

void StreamHubControlDock::ApplyBroadcast()
{
    QJsonArray tags;
    for (const QString &tag : tags_->text().split(',', Qt::SkipEmptyParts))
        tags.append(tag.trimmed());
    const QJsonObject body{
        {"title", title_->toPlainText().trimmed()},
        {"notification", notification_->toPlainText().trimmed()},
        {"category", category_->text().trimmed()},
        {"categoryId", categoryId_},
        {"visibility", visibility_->currentIndex()},
        {"tags", tags},
        {"language", language_->currentData().toString()},
        {"classification", classification_->currentData().toString()},
        {"sourcePlatform", primaryPlatform_},
    };
    if (primaryPlatform_.isEmpty()) {
        broadcastStatus_->setText(tr("Serviço principal OBS não identificado."));
        return;
    }
    broadcastStatus_->setText(tr("Aplicando em Twitch, Kick e YouTube..."));
    Request("POST", "/api/broadcast/apply", body, [this](const QJsonObject &o, int status) {
        if (status != 200) { broadcastStatus_->setText(o.value("error").toString()); return; }
        QStringList lines;
        for (const auto &v : o.value("results").toArray()) {
            const auto result = v.toObject();
            lines << QString("%1: %2").arg(result.value("platform").toString(), result.value("message").toString());
        }
        broadcastStatus_->setText(lines.join('\n'));
    });
}

void StreamHubControlDock::SearchCategories()
{
    const QString query = category_->text().trimmed(); if (query.size() < 2) { categoryResults_->hide(); return; }
    Request("GET", "/api/twitch/categories?q=" + QString::fromUtf8(QUrl::toPercentEncoding(query)), {}, [this](const QJsonObject &o, int status) {
        categoryResults_->clear(); if (status != 200) { categoryResults_->hide(); broadcastStatus_->setText(o.value("error").toString()); return; }
        for (const auto &value : o.value("data").toArray()) {
            const auto category = value.toObject();
            const QString id = category.value("id").toString();
            auto *item = new QListWidgetItem(QIcon(":/streamhub-ui/icons/twitch.svg"), category.value("name").toString(), categoryResults_);
            item->setData(Qt::UserRole, id);
            const QString path = QString("http://localhost:%1/api/twitch/category-cover?id=%2")
                                     .arg(port_)
                                     .arg(QString::fromUtf8(QUrl::toPercentEncoding(id)));
            QNetworkReply *imageReply = network_->get(QNetworkRequest(QUrl(path)));
            connect(imageReply, &QNetworkReply::finished, this, [this, imageReply, id]() {
                QPixmap art;
                art.loadFromData(imageReply->readAll());
                const bool valid = imageReply->error() == QNetworkReply::NoError && !art.isNull();
                imageReply->deleteLater();
                if (!valid) return;
                for (int row = 0; row < categoryResults_->count(); ++row) {
                    auto *result = categoryResults_->item(row);
                    if (result->data(Qt::UserRole).toString() == id)
                        result->setIcon(QIcon(art));
                }
            });
        }
        categoryResults_->setVisible(categoryResults_->count() > 0);
    });
}

void StreamHubControlDock::UpdatePreview()
{
    const QString title = title_->toPlainText().trimmed(); previewTitle_->setText(title.isEmpty() ? tr("Título da transmissão") : title);
    const QString category = category_->text().trimmed(); previewCategory_->setText(category.isEmpty() ? tr("Nenhuma categoria selecionada") : QString("%1 · %2").arg(category, language_->currentText()));
    const QString notification = notification_->toPlainText().trimmed(); previewNotification_->setText(notification.isEmpty() ? tr("A prévia é atualizada enquanto você edita.") : notification);
}
