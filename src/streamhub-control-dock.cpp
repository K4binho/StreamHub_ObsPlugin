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
}

StreamHubControlDock::StreamHubControlDock(QWidget *parent) : QWidget(parent)
{
    setObjectName("streamHubControl");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

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
    accountsLayout->setContentsMargins(8, 14, 8, 8);
    auto *accountIntro = new QLabel(tr("Conecte uma vez. O StreamHub renova a autorização automaticamente."), accounts);
    accountIntro->setWordWrap(true);
    accountsLayout->addWidget(accountIntro);
    auto *accountCard = new QWidget(accounts);
    accountCard->setObjectName("accountCard");
    auto *accountCardLayout = new QHBoxLayout(accountCard);
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
    connectButton_ = new QPushButton(tr("Conectar"), accountCard);
    connectButton_->setObjectName("accountAction");
    connect(connectButton_, &QPushButton::clicked, this, &StreamHubControlDock::StartTwitchLogin);
    accountCardLayout->addWidget(connectButton_);
    accountsLayout->addWidget(accountCard);
    loginHelp_ = StatusLabel(accounts);
    loginHelp_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    loginHelp_->setOpenExternalLinks(true);
    accountsLayout->addWidget(loginHelp_);

    auto *youtubeCard = new QWidget(accounts);
    youtubeCard->setObjectName("accountCard");
    auto *youtubeCardRoot = new QVBoxLayout(youtubeCard);
    auto *youtubeTop = new QHBoxLayout();
    auto *youtubeIcon = new QLabel(youtubeCard);
    youtubeIcon->setPixmap(QIcon(":/streamhub-ui/icons/youtube.svg").pixmap(44, 44));
    youtubeIcon->setFixedSize(48, 48);
    youtubeTop->addWidget(youtubeIcon);
    auto *youtubeText = new QVBoxLayout();
    youtubeAccountName_ = new QLabel(tr("YouTube"), youtubeCard); youtubeAccountName_->setObjectName("accountName");
    youtubeAccountStatus_ = new QLabel(tr("● Verificando conta..."), youtubeCard); youtubeAccountStatus_->setObjectName("accountState");
    youtubeText->addWidget(youtubeAccountName_); youtubeText->addWidget(youtubeAccountStatus_); youtubeTop->addLayout(youtubeText, 1);
    youtubeConnectButton_ = new QPushButton(tr("Conectar"), youtubeCard); youtubeConnectButton_->setObjectName("accountAction");
    connect(youtubeConnectButton_, &QPushButton::clicked, this, &StreamHubControlDock::StartYoutubeLogin);
    youtubeTop->addWidget(youtubeConnectButton_); youtubeCardRoot->addLayout(youtubeTop);
    youtubeClientId_ = new QLineEdit(youtubeCard); youtubeClientId_->setPlaceholderText(tr("Client ID OAuth do Google"));
    youtubeClientSecret_ = new QLineEdit(youtubeCard); youtubeClientSecret_->setPlaceholderText(tr("Client Secret OAuth do Google")); youtubeClientSecret_->setEchoMode(QLineEdit::Password);
    youtubeCardRoot->addWidget(youtubeClientId_); youtubeCardRoot->addWidget(youtubeClientSecret_);
    auto *youtubeHelp = new QLabel(tr("Google Cloud: YouTube Data API v3 + cliente OAuth do tipo Aplicativo para computador."), youtubeCard);
    youtubeHelp->setObjectName("controlStatus"); youtubeHelp->setWordWrap(true); youtubeCardRoot->addWidget(youtubeHelp);
    accountsLayout->addWidget(youtubeCard);
    accountsLayout->addStretch();
    tabs->addTab(accounts, QIcon(":/streamhub-ui/branding/k4-logo.png"), tr("Contas"));

    auto *broadcastScroll = new QScrollArea(tabs);
    broadcastScroll->setWidgetResizable(true);
    broadcastScroll->setFrameShape(QFrame::NoFrame);
    auto *broadcast = new QWidget(broadcastScroll);
    auto *broadcastLayout = new QVBoxLayout(broadcast);
    broadcastLayout->setContentsMargins(8, 14, 8, 8);
    broadcastLayout->setSpacing(10);

    auto *preview = new QWidget(broadcast);
    preview->setObjectName("livePreview");
    auto *previewLayout = new QHBoxLayout(preview);
    previewCover_ = new QLabel(preview);
    previewCover_->setFixedSize(58, 78);
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
    broadcastStatus_ = StatusLabel(broadcast); broadcastLayout->addWidget(broadcastStatus_); broadcastLayout->addStretch();
    broadcastScroll->setWidget(broadcast);
    tabs->addTab(broadcastScroll, QIcon(":/streamhub-ui/icons/camera.svg"), tr("Transmissão"));

    network_ = new QNetworkAccessManager(this);
    loginTimer_ = new QTimer(this); loginTimer_->setSingleShot(true);
    youtubeLoginTimer_ = new QTimer(this); youtubeLoginTimer_->setSingleShot(true); youtubeLoginTimer_->setInterval(1500);
    connect(youtubeLoginTimer_, &QTimer::timeout, this, &StreamHubControlDock::PollYoutubeLogin);
    categoryTimer_ = new QTimer(this); categoryTimer_->setSingleShot(true); categoryTimer_->setInterval(350);
    connect(category_, &QLineEdit::textEdited, this, [this]() { categoryId_.clear(); categoryTimer_->start(); UpdatePreview(); });
    connect(categoryTimer_, &QTimer::timeout, this, &StreamHubControlDock::SearchCategories);
    connect(categoryResults_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        categoryId_ = item->data(Qt::UserRole).toString(); category_->setText(item->text()); categoryResults_->hide();
        previewCover_->setPixmap(item->icon().pixmap(52, 72)); UpdatePreview();
    });
    connect(title_, &QTextEdit::textChanged, this, [this]() { LimitText(title_, titleCount_, 140); UpdatePreview(); });
    connect(notification_, &QTextEdit::textChanged, this, [this]() { LimitText(notification_, notificationCount_, 140); UpdatePreview(); });
    connect(tags_, &QLineEdit::textChanged, this, &StreamHubControlDock::UpdatePreview);
    connect(language_, &QComboBox::currentIndexChanged, this, &StreamHubControlDock::UpdatePreview);

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

void StreamHubControlDock::ConnectTo(int port) { port_ = port; RefreshAccount(); RefreshYoutubeAccount(); }

void StreamHubControlDock::Request(const QByteArray &method, const QString &path, const QJsonObject &body, ReplyHandler handler)
{
    QNetworkRequest request{QUrl(QString("http://127.0.0.1:%1%2").arg(port_).arg(path))};
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
        if (!connected) { accountName_->setText(tr("Twitch")); accountStatus_->setText(tr("● Não conectada")); connectButton_->setText(tr("Conectar")); }
        else if (reconnect) { accountName_->setText(QString("Twitch · %1").arg(o.value("name").toString())); accountStatus_->setText(tr("● Precisa de novas permissões")); connectButton_->setText(tr("Reconectar")); }
        else { accountName_->setText(QString("Twitch · %1").arg(o.value("name").toString())); accountStatus_->setText(tr("● Conectada")); connectButton_->setText(tr("Trocar conta")); }
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

void StreamHubControlDock::RefreshYoutubeAccount()
{
    Request("GET", "/api/accounts/youtube/status", {}, [this](const QJsonObject &o, int status) {
        if (status != 200) { youtubeAccountStatus_->setText(tr("● Aguardando o serviço local...")); youtubeAccountStatus_->setProperty("connected", false); QTimer::singleShot(1500, this, &StreamHubControlDock::RefreshYoutubeAccount); return; }
        const bool connected = o.value("connected").toBool();
        const bool needsReconnect = o.value("needsReconnect").toBool();
        const bool usable = connected && !needsReconnect;
        const bool configured = o.value("configured").toBool();
        youtubeAccountStatus_->setProperty("connected", usable); youtubeAccountStatus_->style()->unpolish(youtubeAccountStatus_); youtubeAccountStatus_->style()->polish(youtubeAccountStatus_);
        if (youtubeClientId_->text().isEmpty()) youtubeClientId_->setText(o.value("clientId").toString());
        youtubeAccountName_->setText(connected ? QString("YouTube · %1").arg(o.value("name").toString()) : tr("YouTube"));
        if (!connected && configured) youtubeAccountStatus_->setText(tr("● Aguardando autorização"));
        else if (!connected) youtubeAccountStatus_->setText(tr("● Não conectado"));
        else if (needsReconnect) youtubeAccountStatus_->setText(tr("● Precisa reconectar"));
        else youtubeAccountStatus_->setText(tr("● Conectado"));
        youtubeConnectButton_->setText(connected ? (needsReconnect ? tr("Reconectar") : tr("Trocar conta")) : tr("Conectar"));
        youtubeClientId_->setVisible(!usable); youtubeClientSecret_->setVisible(!usable); youtubeConnectButton_->setEnabled(true);
        if (usable)
            youtubeClientSecret_->clear();
        youtubeLoginTimer_->stop();
        if (needsReconnect)
            youtubeAccountStatus_->setToolTip(tr("A autorização não contém refresh token. Conecte novamente para permitir renovação automática."));
        else
            youtubeAccountStatus_->setToolTip(QString());
    });
}

void StreamHubControlDock::StartYoutubeLogin()
{
    const QString clientId = youtubeClientId_->text().trimmed();
    if (clientId.isEmpty()) { youtubeAccountStatus_->setText(tr("Informe o Client ID OAuth do Google.")); return; }
    youtubeConnectButton_->setEnabled(false); youtubeAccountStatus_->setText(tr("● Abrindo autorização do Google..."));
    Request("POST", "/api/accounts/youtube/connect", {{"clientId", clientId}, {"clientSecret", youtubeClientSecret_->text().trimmed()}}, [this](const QJsonObject &o, int status) {
        if (status != 200) { youtubeAccountStatus_->setText(o.value("error").toString()); youtubeConnectButton_->setEnabled(true); return; }
        const QUrl authorizationUrl(o.value("authorizationUrl").toString());
        if (!authorizationUrl.isValid() || authorizationUrl.scheme() != "https") {
            youtubeAccountStatus_->setText(tr("URL de autorização do Google inválida."));
            youtubeConnectButton_->setEnabled(true);
            return;
        }
        QDesktopServices::openUrl(authorizationUrl); youtubeLoginTimer_->start();
    });
}

void StreamHubControlDock::PollYoutubeLogin()
{
    Request("GET", "/api/accounts/youtube/status", {}, [this](const QJsonObject &o, int status) {
        if (status == 200 && o.value("connected").toBool()) { RefreshYoutubeAccount(); return; }
        youtubeAccountStatus_->setText(tr("● Aguardando autorização no navegador...")); youtubeLoginTimer_->start();
    });
}

void StreamHubControlDock::LoadBroadcast()
{
    Request("GET", "/api/broadcast/current", {}, [this](const QJsonObject &o, int status) {
        if (status != 200) { broadcastStatus_->setText(o.value("error").toString()); return; }
        title_->setPlainText(o.value("title").toString()); category_->setText(o.value("gameName").toString()); categoryId_ = o.value("gameId").toString();
        QStringList tags; for (const auto &tag : o.value("tags").toArray()) tags << tag.toString(); tags_->setText(tags.join(", "));
        const int languageIndex = language_->findData(o.value("language").toString()); if (languageIndex >= 0) language_->setCurrentIndex(languageIndex);
        const auto labels = o.value("classificationLabels").toArray(); const int classificationIndex = labels.isEmpty() ? 0 : classification_->findData(labels.first().toString()); classification_->setCurrentIndex(qMax(0, classificationIndex));
        const QString visibility = o.value("visibility").toString();
        if (visibility == "unlisted") visibility_->setCurrentIndex(1);
        else if (visibility == "private") visibility_->setCurrentIndex(2);
        else if (!visibility.isEmpty()) visibility_->setCurrentIndex(0);
        const QString platform = o.value("platform").toString("twitch");
        const QString artUrl = o.value("boxArtUrl").toString();
        if (artUrl.isEmpty()) {
            previewCover_->setPixmap(QIcon(QString(":/streamhub-ui/icons/%1.svg").arg(platform)).pixmap(38, 38));
        } else {
            QNetworkReply *imageReply = network_->get(QNetworkRequest(QUrl(artUrl)));
            connect(imageReply, &QNetworkReply::finished, this, [this, imageReply]() {
                QPixmap art;
                art.loadFromData(imageReply->readAll());
                imageReply->deleteLater();
                if (!art.isNull())
                    previewCover_->setPixmap(art.scaled(52, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            });
        }
        UpdatePreview(); broadcastStatus_->setText(tr("Informações atuais carregadas."));
    });
}

void StreamHubControlDock::ApplyBroadcast()
{
    QJsonArray tags; for (const QString &tag : tags_->text().split(',', Qt::SkipEmptyParts)) tags.append(tag.trimmed());
    const QJsonObject body{{"title", title_->toPlainText().trimmed()}, {"notification", notification_->toPlainText().trimmed()}, {"category", category_->text().trimmed()}, {"categoryId", categoryId_}, {"visibility", visibility_->currentIndex()}, {"tags", tags}, {"language", language_->currentData().toString()}, {"classification", classification_->currentData().toString()}};
    broadcastStatus_->setText(tr("Aplicando..."));
    Request("POST", "/api/broadcast/apply", body, [this](const QJsonObject &o, int status) {
        if (status != 200) { broadcastStatus_->setText(o.value("error").toString()); return; }
        QStringList lines; for (const auto &v : o.value("results").toArray()) { const auto r = v.toObject(); lines << QString("%1: %2").arg(r.value("platform").toString(), r.value("message").toString()); } broadcastStatus_->setText(lines.join('\n'));
    });
}

void StreamHubControlDock::SearchCategories()
{
    const QString query = category_->text().trimmed(); if (query.size() < 2) { categoryResults_->hide(); return; }
    Request("GET", "/api/twitch/categories?q=" + QString::fromUtf8(QUrl::toPercentEncoding(query)), {}, [this](const QJsonObject &o, int status) {
        categoryResults_->clear(); if (status != 200) { categoryResults_->hide(); broadcastStatus_->setText(o.value("error").toString()); return; }
        for (const auto &value : o.value("data").toArray()) {
            const auto category = value.toObject(); auto *item = new QListWidgetItem(QIcon(":/streamhub-ui/icons/twitch.svg"), category.value("name").toString(), categoryResults_); item->setData(Qt::UserRole, category.value("id").toString());
            const QString artUrl = category.value("boxArtUrl").toString();
            if (!artUrl.isEmpty()) { QNetworkReply *imageReply = network_->get(QNetworkRequest(QUrl(artUrl))); const QString id = category.value("id").toString(); connect(imageReply, &QNetworkReply::finished, this, [this, imageReply, id]() { QPixmap art; art.loadFromData(imageReply->readAll()); imageReply->deleteLater(); if (art.isNull()) return; for (int row = 0; row < categoryResults_->count(); ++row) if (categoryResults_->item(row)->data(Qt::UserRole).toString() == id) categoryResults_->item(row)->setIcon(QIcon(art)); if (categoryId_ == id) previewCover_->setPixmap(art.scaled(52, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation)); }); }
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
