#include "streamhub-control-dock.h"
#include "streamhub-broadcast-request.h"
#include "streamhub-category-cover-service.h"
#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
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
#include <QMainWindow>
#include <QMessageBox>
#include <QPair>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>
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

QString PreviewPlatformIcon(const QString &platform)
{
    if (platform.compare("kick", Qt::CaseInsensitive) == 0)
        return QStringLiteral(":/streamhub-ui/icons/kick.svg");
    if (platform.compare("youtube", Qt::CaseInsensitive) == 0)
        return QStringLiteral(":/streamhub-ui/icons/youtube.svg");
    return QStringLiteral(":/streamhub-ui/icons/twitch.svg");
}

QString PreviewPlatformAccent(const QString &platform)
{
    if (platform.compare("kick", Qt::CaseInsensitive) == 0)
        return QStringLiteral("#53FC18");
    if (platform.compare("youtube", Qt::CaseInsensitive) == 0)
        return QStringLiteral("#FF0033");
    return QStringLiteral("#9146FF");
}

QString PreviewPlatformName(const QString &platform)
{
    if (platform.compare("kick", Qt::CaseInsensitive) == 0)
        return QStringLiteral("Kick");
    if (platform.compare("youtube", Qt::CaseInsensitive) == 0)
        return QStringLiteral("YouTube");
    return QStringLiteral("Twitch");
}

QString SafeAuthorizationUrl(const QString &raw)
{
    const QUrl url(raw.trimmed());
    if (!url.isValid() || url.scheme().compare("https", Qt::CaseInsensitive) != 0 || url.host().isEmpty())
        return {};

    const QUrlQuery query(url);
    for (const auto &item : query.queryItems(QUrl::FullyDecoded)) {
        if (item.first.compare("code", Qt::CaseInsensitive) == 0)
            return {};
    }
    return url.toString(QUrl::FullyEncoded).toHtmlEscaped();
}
}


StreamHubControlDock::StreamHubControlDock(QWidget *parent) : QWidget(parent)
{
    setObjectName("streamHubControl");
    // Deixa o OBS restaurar o tamanho salvo do dock sem um salto de geometria
    // enquanto o conteúdo das abas e das áreas roláveis termina de ser montado.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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

    auto *accountsScroll = new QScrollArea(tabs);
    accountsScroll->setObjectName("accountsScroll");
    accountsScroll->setWidgetResizable(true);
    accountsScroll->setFrameShape(QFrame::NoFrame);
    accountsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    accountsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto *accounts = new QWidget();
    auto *accountsLayout = new QVBoxLayout(accounts);
    accountsLayout->setContentsMargins(4, 6, 4, 6);
    accountsLayout->setSpacing(7);
    auto openAccountOptions = [this](const QString &platform, QLabel *accountName, QLabel *accountStatus,
                                     QPushButton *connectState, QPushButton *syncState,
                                     std::function<void()> connectAction, std::function<void()> syncAction,
                                     QPushButton *extraState, std::function<void()> extraAction,
                                     std::function<void()> disconnectAction) {
        QWidget *dialogParent = qobject_cast<QMainWindow *>(window());
        if (!dialogParent) {
            for (QWidget *candidate : QApplication::topLevelWidgets()) {
                if (candidate->isVisible() && qobject_cast<QMainWindow *>(candidate)) {
                    dialogParent = candidate;
                    if (candidate->isActiveWindow())
                        break;
                }
            }
        }
        if (!dialogParent)
            dialogParent = this;

        QDialog dialog(dialogParent);
        dialog.setWindowTitle(tr("Conta %1").arg(platform));
        dialog.setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        dialog.setWindowModality(Qt::ApplicationModal);
        dialog.setObjectName("accountOptionsDialog");
        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(10);
        auto *name = new QLabel(accountName->text(), &dialog);
        name->setObjectName("accountOptionsName");
        auto *status = new QLabel(accountStatus->text(), &dialog);
        status->setObjectName("accountOptionsStatus");
        status->setWordWrap(true);
        layout->addWidget(name);
        layout->addWidget(status);

        int selectedAction = 0;
        auto *connectOption = new QPushButton(connectState->text(), &dialog);
        connectOption->setObjectName("accountOptionsAction");
        connectOption->setEnabled(connectState->isEnabled());
        auto *syncOption = new QPushButton(tr("Sincronizar transmissão"), &dialog);
        syncOption->setObjectName("accountOptionsAction");
        syncOption->setEnabled(syncState->isEnabled());
        layout->addWidget(connectOption);
        layout->addWidget(syncOption);
        QPushButton *extraOption = nullptr;
        if (extraState && extraState->property("available").toBool()) {
            extraOption = new QPushButton(extraState->text(), &dialog);
            extraOption->setObjectName("accountOptionsAction");
            layout->addWidget(extraOption);
            connect(extraOption, &QPushButton::clicked, &dialog, [&dialog, &selectedAction]() {
                selectedAction = 3;
                dialog.accept();
            });
        }
        auto *disconnectOption = new QPushButton(tr("Sair da conta"), &dialog);
        disconnectOption->setObjectName("accountOptionsDisconnect");
        disconnectOption->setEnabled(accountStatus->property("authenticated").toBool());
        layout->addWidget(disconnectOption);
        auto *closeOption = new QPushButton(tr("Fechar"), &dialog);
        closeOption->setObjectName("accountOptionsClose");
        layout->addWidget(closeOption);
        connect(connectOption, &QPushButton::clicked, &dialog, [&dialog, &selectedAction]() {
            selectedAction = 1;
            dialog.accept();
        });
        connect(syncOption, &QPushButton::clicked, &dialog, [&dialog, &selectedAction]() {
            selectedAction = 2;
            dialog.accept();
        });
        connect(disconnectOption, &QPushButton::clicked, &dialog, [&dialog, &selectedAction]() {
            selectedAction = 4;
            dialog.accept();
        });
        connect(closeOption, &QPushButton::clicked, &dialog, &QDialog::reject);

        dialog.setStyleSheet(QStringLiteral(
            "QDialog#accountOptionsDialog { background:#080c14; color:#f2f7ff; }"
            "QLabel#accountOptionsName { color:#f2f7ff; font-size:16px; font-weight:800; }"
            "QLabel#accountOptionsStatus { color:#9eb2cb; }"
            "QPushButton#accountOptionsAction { min-height:34px; padding:5px 12px; background:#17283a; color:white; border:1px solid #29496f; border-radius:7px; }"
            "QPushButton#accountOptionsAction:hover { border-color:#00c8ff; background:#1b3248; }"
            "QPushButton#accountOptionsAction:disabled { color:#71839b; }"
            "QPushButton#accountOptionsDisconnect { min-height:34px; padding:5px 12px; background:#351923; color:#ffbac4; border:1px solid #8d3344; border-radius:7px; }"
            "QPushButton#accountOptionsDisconnect:hover { background:#51202b; border-color:#e24d65; }"
            "QPushButton#accountOptionsDisconnect:disabled { color:#71839b; background:#101a27; border-color:#29496f; }"
            "QPushButton#accountOptionsClose { min-height:30px; padding:4px 10px; background:transparent; color:#b8c8dc; border:1px solid #29496f; border-radius:7px; }"));
        dialog.adjustSize();
        const QPoint center = dialogParent->window()->frameGeometry().center();
        dialog.move(center - QPoint(dialog.width() / 2, dialog.height() / 2));
        dialog.exec();
        if (selectedAction == 1)
            connectAction();
        else if (selectedAction == 2)
            syncAction();
        else if (selectedAction == 3 && extraAction)
            extraAction();
        else if (selectedAction == 4 && disconnectAction &&
                 QMessageBox::question(this, tr("Sair da conta"),
                     tr("Desconectar %1 do StreamHub? As configurações de transmissão serão mantidas.").arg(platform),
                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            disconnectAction();
    };
    auto *accountCard = new QWidget(accounts);
    accountCard->setObjectName("accountCard");
    auto *accountCardLayout = new QVBoxLayout(accountCard);
    accountCardLayout->setContentsMargins(10, 7, 10, 7);
    accountCardLayout->setSpacing(5);
    auto *accountIdentity = new QHBoxLayout();
    accountIdentity->setSpacing(8);
    auto *twitchIcon = new QLabel(accountCard);
    twitchIcon->setPixmap(QIcon(":/streamhub-ui/icons/twitch.svg").pixmap(38, 38));
    twitchIcon->setFixedSize(40, 40);
    accountIdentity->addWidget(twitchIcon);
    auto *accountText = new QVBoxLayout();
    accountName_ = new QLabel(tr("Twitch"), accountCard);
    accountName_->setObjectName("accountName");
    accountStatus_ = new QLabel(tr("● Verificando conta..."), accountCard);
    accountStatus_->setObjectName("accountState");
    accountText->addWidget(accountName_);
    accountText->addWidget(accountStatus_);
    accountIdentity->addLayout(accountText, 1);
    connectButton_ = new QPushButton(tr("Conectar"), accountCard);
    twitchSyncButton_ = new QPushButton(tr("Sincronizar"), accountCard);
    connectButton_->setObjectName("accountAction");
    twitchSyncButton_->setObjectName("accountAction");
    connectButton_->hide();
    twitchSyncButton_->hide();
    twitchSyncButton_->setToolTip(tr("Busca RTMP e stream key oficiais e atualiza destino nativo em Múltiplas saídas."));
    connect(connectButton_, &QPushButton::clicked, this, &StreamHubControlDock::StartTwitchLogin);
    connect(twitchSyncButton_, &QPushButton::clicked, this, &StreamHubControlDock::SyncTwitchTransmission);
    auto *twitchGear = new QPushButton(QIcon(":/streamhub-ui/icons/settings.svg"), QString(), accountCard);
    twitchGear->setObjectName("accountGear");
    twitchGear->setFixedSize(34, 34);
    twitchGear->setToolTip(tr("Opções da conta Twitch"));
    accountIdentity->addWidget(twitchGear, 0, Qt::AlignVCenter);
    accountCardLayout->addLayout(accountIdentity);
    connect(twitchGear, &QPushButton::clicked, this, [openAccountOptions, this]() {
        openAccountOptions(tr("Twitch"), accountName_, accountStatus_, connectButton_, twitchSyncButton_,
                           [this]() { StartTwitchLogin(); }, [this]() { SyncTwitchTransmission(); }, nullptr, {},
                           [this]() { Request("POST", "/api/accounts/twitch/disconnect", {}, [this](const QJsonObject &, int) { RefreshAccount(); }); });
    });
    accountsLayout->addWidget(accountCard);

    auto *kickCard = new QWidget(accounts);
    kickCard->setObjectName("accountCard");
    auto *kickCardLayout = new QVBoxLayout(kickCard);
    kickCardLayout->setContentsMargins(10, 7, 10, 7);
    kickCardLayout->setSpacing(5);
    auto *kickIdentity = new QHBoxLayout();
    kickIdentity->setSpacing(8);
    auto *kickIcon = new QLabel(kickCard);
    kickIcon->setPixmap(QIcon(":/streamhub-ui/icons/kick.svg").pixmap(38, 38));
    kickIcon->setFixedSize(40, 40);
    kickIdentity->addWidget(kickIcon);
    auto *kickText = new QVBoxLayout();
    kickAccountName_ = new QLabel(tr("Kick"), kickCard);
    kickAccountName_->setObjectName("accountName");
    kickAccountStatus_ = new QLabel(tr("● Verificando conta..."), kickCard);
    kickAccountStatus_->setObjectName("accountState");
    kickText->addWidget(kickAccountName_);
    kickText->addWidget(kickAccountStatus_);
    kickIdentity->addLayout(kickText, 1);
    kickConnectButton_ = new QPushButton(tr("Conectar"), kickCard);
    kickSyncButton_ = new QPushButton(tr("Sincronizar"), kickCard);
    kickCopyLinkButton_ = new QPushButton(tr("Copiar link"), kickCard);
    kickConnectButton_->setObjectName("accountAction");
    kickSyncButton_->setObjectName("accountAction");
    kickConnectButton_->hide();
    kickSyncButton_->hide();
    kickSyncButton_->setToolTip(tr("Busca RTMP e stream key oficiais e atualiza destino nativo em Múltiplas saídas."));
    kickCopyLinkButton_->setObjectName("accountActionSecondary");
    kickCopyLinkButton_->setProperty("available", false);
    kickCopyLinkButton_->hide();
    kickCopyLinkButton_->setProperty("available", false);
    connect(kickConnectButton_, &QPushButton::clicked, this, &StreamHubControlDock::StartKickLogin);
    connect(kickSyncButton_, &QPushButton::clicked, this, &StreamHubControlDock::SyncKickTransmission);
    connect(kickCopyLinkButton_, &QPushButton::clicked, this, [this]() {
        if (kickAuthorizationUri_.isEmpty())
            return;
        QApplication::clipboard()->setText(kickAuthorizationUri_);
        kickLoginHelp_->setText(tr("Link de autorização Kick copiado. Abra no navegador se necessário."));
    });
    kickCopyLinkButton_->hide();
    auto *kickGear = new QPushButton(QIcon(":/streamhub-ui/icons/settings.svg"), QString(), kickCard);
    kickGear->setObjectName("accountGear");
    kickGear->setFixedSize(34, 34);
    kickGear->setToolTip(tr("Opções da conta Kick"));
    kickIdentity->addWidget(kickGear, 0, Qt::AlignVCenter);
    kickCardLayout->addLayout(kickIdentity);
    connect(kickGear, &QPushButton::clicked, this, [openAccountOptions, this]() {
        openAccountOptions(tr("Kick"), kickAccountName_, kickAccountStatus_, kickConnectButton_, kickSyncButton_,
                           [this]() { StartKickLogin(); }, [this]() { SyncKickTransmission(); }, kickCopyLinkButton_,
                           [this]() { kickCopyLinkButton_->click(); },
                           [this]() { Request("POST", "/api/accounts/kick/disconnect", {}, [this](const QJsonObject &, int) { RefreshKickAccount(); }); });
    });
    accountsLayout->addWidget(kickCard);

    auto *youtubeCard = new QWidget(accounts);
    youtubeCard->setObjectName("accountCard");
    auto *youtubeCardLayout = new QVBoxLayout(youtubeCard);
    youtubeCardLayout->setContentsMargins(10, 7, 10, 7);
    youtubeCardLayout->setSpacing(5);
    auto *youtubeIdentity = new QHBoxLayout();
    youtubeIdentity->setSpacing(8);
    auto *youtubeIcon = new QLabel(youtubeCard);
    youtubeIcon->setPixmap(QIcon(":/streamhub-ui/icons/youtube.svg").pixmap(38, 38));
    youtubeIcon->setFixedSize(40, 40);
    youtubeIdentity->addWidget(youtubeIcon);
    auto *youtubeText = new QVBoxLayout();
    youtubeAccountName_ = new QLabel(tr("YouTube"), youtubeCard);
    youtubeAccountName_->setObjectName("accountName");
    youtubeAccountStatus_ = new QLabel(tr("● Verificando conta..."), youtubeCard);
    youtubeAccountStatus_->setObjectName("accountState");
    youtubeText->addWidget(youtubeAccountName_);
    youtubeText->addWidget(youtubeAccountStatus_);
    youtubeIdentity->addLayout(youtubeText, 1);
    youtubeConnectButton_ = new QPushButton(tr("Conectar"), youtubeCard);
    youtubeSyncButton_ = new QPushButton(tr("Sincronizar"), youtubeCard);
    youtubeConnectButton_->setObjectName("accountAction");
    youtubeSyncButton_->setObjectName("accountAction");
    youtubeConnectButton_->hide();
    youtubeSyncButton_->hide();
    connect(youtubeConnectButton_, &QPushButton::clicked, this, &StreamHubControlDock::StartYoutubeLogin);
    connect(youtubeSyncButton_, &QPushButton::clicked, this, &StreamHubControlDock::SyncYoutubeTransmission);
    auto *youtubeGear = new QPushButton(QIcon(":/streamhub-ui/icons/settings.svg"), QString(), youtubeCard);
    youtubeGear->setObjectName("accountGear");
    youtubeGear->setFixedSize(34, 34);
    youtubeGear->setToolTip(tr("Opções da conta YouTube"));
    youtubeIdentity->addWidget(youtubeGear, 0, Qt::AlignVCenter);
    youtubeCardLayout->addLayout(youtubeIdentity);
    connect(youtubeGear, &QPushButton::clicked, this, [openAccountOptions, this]() {
        openAccountOptions(tr("YouTube"), youtubeAccountName_, youtubeAccountStatus_, youtubeConnectButton_, youtubeSyncButton_,
                           [this]() { StartYoutubeLogin(); }, [this]() { SyncYoutubeTransmission(); }, nullptr, {},
                           [this]() { Request("POST", "/api/accounts/youtube/disconnect", {}, [this](const QJsonObject &, int) { RefreshYoutubeAccount(); }); });
    });
    accountsLayout->addWidget(youtubeCard);

    auto *accountLogPanel = new QWidget(accounts);
    accountLogPanel->setObjectName("accountLogPanel");
    auto *accountLogLayout = new QVBoxLayout(accountLogPanel);
    accountLogLayout->setContentsMargins(12, 10, 12, 10);
    accountLogLayout->setSpacing(9);
    auto *accountLogTitle = new QLabel(tr("Status das contas"), accountLogPanel);
    accountLogTitle->setObjectName("accountLogTitle");
    accountLogLayout->addWidget(accountLogTitle);
    auto addAccountLogRow = [accountLogPanel, accountLogLayout](const QString &iconPath,
                                                                const QString &platform,
                                                                QLabel *&message,
                                                                const QString &initialText) {
        auto *row = new QWidget(accountLogPanel);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(9);
        auto *icon = new QLabel(row);
        icon->setFixedSize(24, 24);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(QIcon(iconPath).pixmap(22, 22));
        rowLayout->addWidget(icon, 0, Qt::AlignTop);
        auto *textColumn = new QVBoxLayout();
        textColumn->setContentsMargins(0, 0, 0, 0);
        textColumn->setSpacing(2);
        auto *name = new QLabel(platform, row);
        name->setObjectName("accountLogPlatform");
        textColumn->addWidget(name);
        message = StatusLabel(row);
        message->setObjectName("accountLogMessage");
        message->setText(initialText);
        message->setTextInteractionFlags(Qt::TextBrowserInteraction);
        message->setOpenExternalLinks(true);
        textColumn->addWidget(message);
        rowLayout->addLayout(textColumn, 1);
        accountLogLayout->addWidget(row);
    };

    addAccountLogRow(":/streamhub-ui/icons/twitch.svg", tr("Twitch"), loginHelp_,
                     tr("Aguardando conexão e sincronização."));
    addAccountLogRow(":/streamhub-ui/icons/kick.svg", tr("Kick"), kickLoginHelp_,
                     tr("Aguardando conexão e sincronização."));
    addAccountLogRow(":/streamhub-ui/icons/youtube.svg", tr("YouTube"), youtubeLoginHelp_,
                     tr("Aguardando conexão e sincronização."));
    accountsLayout->addWidget(accountLogPanel);
    accountsLayout->addStretch();
    accountsScroll->setWidget(accounts);
    tabs->addTab(accountsScroll, QIcon(":/streamhub-ui/icons/twitch.svg"), tr("Contas"));

    auto *broadcast = new QWidget(tabs);
    auto *broadcastLayout = new QVBoxLayout(broadcast);
    broadcastLayout->setContentsMargins(6, 6, 6, 6);
    broadcastLayout->setSpacing(5);

    auto *overview = new QWidget(broadcast);
    overview->setObjectName("broadcastOverview");
    auto *overviewLayout = new QVBoxLayout(overview);
    overviewLayout->setContentsMargins(8, 7, 8, 7);
    overviewLayout->setSpacing(5);

    auto *preview = new QWidget(overview);
    auto *previewLayout = new QHBoxLayout(preview);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(8);
    previewCover_ = new QLabel(preview);
    previewCover_->setFixedSize(70, 70);
    previewCover_->setAlignment(Qt::AlignCenter);
    previewCover_->setPixmap(QIcon(":/streamhub-ui/icons/twitch.svg").pixmap(38, 38));
    previewLayout->addWidget(previewCover_);
    auto *previewText = new QVBoxLayout();
    previewCaption_ = new QLabel(tr("PRÉVIA DA LIVE"), preview);
    previewCaption_->setObjectName("previewCaption");
    previewTitle_ = new QLabel(tr("Título da transmissão"), preview);
    previewTitle_->setObjectName("previewTitle"); previewTitle_->setWordWrap(true);
    previewCategory_ = new QLabel(tr("Nenhuma categoria selecionada"), preview);
    previewCategory_->setObjectName("previewMeta");
    previewNotification_ = new QLabel(tr("A prévia é atualizada enquanto você edita."), preview);
    previewNotification_->setObjectName("previewNotification"); previewNotification_->setWordWrap(true);
    previewText->addWidget(previewCaption_); previewText->addWidget(previewTitle_); previewText->addWidget(previewCategory_); previewText->addWidget(previewNotification_);
    previewLayout->addLayout(previewText, 1);
    overviewLayout->addWidget(preview);
    UpdatePreviewPlatformVisual();

    auto *syncTitle = new QLabel(tr("CONFERÊNCIA ENTRE PLATAFORMAS"), overview);
    syncTitle->setObjectName("previewCaption");
    overviewLayout->addWidget(syncTitle);
    auto *syncRow = new QWidget(overview);
    auto *syncLayout = new QHBoxLayout(syncRow);
    syncLayout->setContentsMargins(0, 0, 0, 0);
    syncLayout->setSpacing(7);
    transmissionSyncStatus_ = StatusLabel(syncRow);
    transmissionSyncStatus_->setObjectName("syncStatus");
    transmissionSyncStatus_->setWordWrap(true);
    transmissionSyncStatus_->setText(tr("Aguardando consulta das plataformas."));
    syncLayout->addWidget(transmissionSyncStatus_, 1);
    transmissionSyncIndicator_ = new QLabel(QStringLiteral("•"), syncRow);
    transmissionSyncIndicator_->setObjectName("syncIndicator");
    transmissionSyncIndicator_->setProperty("state", "pending");
    transmissionSyncIndicator_->setAlignment(Qt::AlignCenter);
    transmissionSyncIndicator_->setFixedWidth(17);
    syncLayout->addWidget(transmissionSyncIndicator_, 0, Qt::AlignTop);
    overviewLayout->addWidget(syncRow);
    broadcastLayout->addWidget(overview, 0);

    auto *formScroll = new QScrollArea(broadcast);
    formScroll->setObjectName("broadcastFormScroll");
    formScroll->setWidgetResizable(true);
    formScroll->setFrameShape(QFrame::NoFrame);
    formScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    formScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto *formContent = new QWidget(formScroll);
    auto *formContentLayout = new QVBoxLayout(formContent);
    formContentLayout->setContentsMargins(0, 0, 3, 0);
    formContentLayout->setSpacing(5);

    auto *formCard = new QWidget(formContent);
    formCard->setObjectName("formCard");
    auto *metadataLayout = new QVBoxLayout(formCard);
    metadataLayout->setContentsMargins(10, 8, 10, 8);
    metadataLayout->setSpacing(8);

    auto *sharedBox = new QGroupBox(tr("Dados principais"), formCard);
    auto *sharedForm = new QFormLayout(sharedBox);
    sharedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    sharedForm->setHorizontalSpacing(8);
    sharedForm->setVerticalSpacing(5);
    title_ = new QTextEdit(sharedBox);
    title_->setFixedHeight(46);
    title_->setPlaceholderText(tr("Título usado nas plataformas conectadas"));
    titleCount_ = new QLabel("0/140", sharedBox);
    titleCount_->setObjectName("counter");
    auto *titleBox = new QVBoxLayout();
    titleBox->addWidget(title_);
    titleBox->addWidget(titleCount_, 0, Qt::AlignRight);
    sharedForm->addRow(tr("Título da live"), titleBox);
    category_ = new QLineEdit(sharedBox);
    category_->setPlaceholderText(tr("Procure jogo ou categoria Twitch"));
    categoryResults_ = new QListWidget(sharedBox);
    categoryResults_->setObjectName("categoryResults");
    categoryResults_->setIconSize(QSize(40, 56));
    categoryResults_->setMaximumHeight(170);
    categoryResults_->hide();
    auto *categoryBox = new QVBoxLayout();
    categoryBox->addWidget(category_);
    categoryBox->addWidget(categoryResults_);
    sharedForm->addRow(tr("Jogo · Twitch + Kick"), categoryBox);
    metadataLayout->addWidget(sharedBox);

    auto *commonBox = new QGroupBox(tr("Metadados"), formCard);
    auto *commonForm = new QFormLayout(commonBox);
    commonForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    commonForm->setHorizontalSpacing(8);
    commonForm->setVerticalSpacing(5);
    tags_ = new QLineEdit(commonBox);
    tags_->setPlaceholderText(tr("Separadas por vírgula · Twitch até 5 tags; YouTube até 50"));
    language_ = new QComboBox(commonBox);
    language_->addItem(tr("Manter atual"), "");
    language_->addItem(tr("Português"), "pt");
    language_->addItem(tr("Inglês"), "en");
    language_->addItem(tr("Espanhol"), "es");
    classification_ = new QComboBox(commonBox);
    classification_->addItem(tr("Nenhuma classificação adicional"), "");
    classification_->addItem(tr("Linguagem imprópria"), "ProfanityVulgarity");
    classification_->addItem(tr("Violência gráfica"), "ViolentGraphic");
    classification_->addItem(tr("Temas políticos e sociais"), "DebatedSocialIssuesAndPolitics");
    classification_->addItem(tr("Jogos de azar"), "Gambling");
    commonForm->addRow(tr("Tags · Twitch + YouTube"), tags_);
    commonForm->addRow(tr("Idioma · Twitch + YouTube"), language_);
    commonForm->addRow(tr("Classificação · Twitch"), classification_);
    metadataLayout->addWidget(commonBox);

    auto *youtubeBox = new QGroupBox(tr("YouTube"), formCard);
    auto *youtubeForm = new QFormLayout(youtubeBox);
    youtubeForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    youtubeForm->setHorizontalSpacing(8);
    youtubeForm->setVerticalSpacing(5);
    youtubeBroadcastSelector_ = new QComboBox(youtubeBox);
    youtubeBroadcastSelector_->setToolTip(tr("Escolha exatamente qual evento do YouTube será lido e atualizado."));
    notification_ = new QTextEdit(youtubeBox);
    notification_->setFixedHeight(58);
    notification_->setPlaceholderText(tr("Descrição da live YouTube"));
    notificationCount_ = new QLabel("0/5000", youtubeBox);
    notificationCount_->setObjectName("counter");
    auto *descriptionBox = new QVBoxLayout();
    descriptionBox->addWidget(notification_);
    descriptionBox->addWidget(notificationCount_, 0, Qt::AlignRight);
    youtubeCategory_ = new QLineEdit(youtubeBox);
    youtubeCategory_->setPlaceholderText(tr("Categoria oficial YouTube, por exemplo Gaming"));
    visibility_ = new QComboBox(youtubeBox);
    visibility_->addItems({tr("Público"), tr("Não listado"), tr("Privado")});
    youtubeForm->addRow(tr("Evento do YouTube"), youtubeBroadcastSelector_);
    youtubeForm->addRow(tr("Descrição"), descriptionBox);
    youtubeForm->addRow(tr("Categoria do vídeo"), youtubeCategory_);
    youtubeForm->addRow(tr("Visibilidade"), visibility_);
    metadataLayout->addWidget(youtubeBox);
    formContentLayout->addWidget(formCard);
    formContentLayout->addStretch();
    formScroll->setWidget(formContent);
    broadcastLayout->addWidget(formScroll, 1);

    auto *buttons = new QHBoxLayout();
    auto *loadButton = new QPushButton(tr("Carregar atuais"), broadcast);
    auto *applyButton = new QPushButton(tr("Aplicar em todas"), broadcast); applyButton->setObjectName("primaryAction");
    loadButton->setFixedHeight(34);
    applyButton->setFixedHeight(34);
    connect(loadButton, &QPushButton::clicked, this, &StreamHubControlDock::LoadBroadcast);
    connect(applyButton, &QPushButton::clicked, this, &StreamHubControlDock::ApplyBroadcast);
    connect(youtubeBroadcastSelector_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const QString id = youtubeBroadcastSelector_->currentData().toString().trimmed();
        if (auto *profile = obs_frontend_get_profile_config()) {
            config_set_string(profile, "StreamHub", "YoutubeBroadcastId", id.toUtf8().constData());
            config_save(profile);
        }
        if (index >= 0 && !id.isEmpty())
            RefreshPlatformContentStatus(QStringLiteral("youtube"));
    });
    buttons->setSpacing(6);
    buttons->addWidget(loadButton, 1);
    buttons->addWidget(applyButton, 1);
    broadcastLayout->addLayout(buttons);
    tabs->addTab(broadcast, QIcon(":/streamhub-ui/icons/camera.svg"), tr("Transmissão"));

    network_ = new QNetworkAccessManager(this);
    categoryCoverService_ = new StreamHubCategoryCoverService(network_, port_, this);
    connect(categoryCoverService_, &StreamHubCategoryCoverService::coverLoaded, this,
            [this](const QString &id, const QPixmap &cover) {
        if (categoryId_ == id && categoryIdPlatform_ == QStringLiteral("twitch")) {
            previewHasCategoryCover_ = true;
            previewCover_->setPixmap(cover.scaled(previewCover_->size(), Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation));
        }
        for (int row = 0; row < categoryResults_->count(); ++row) {
            auto *item = categoryResults_->item(row);
            if (item->data(Qt::UserRole).toString() == id)
                item->setIcon(QIcon(cover));
        }
    });
    connect(categoryCoverService_, &StreamHubCategoryCoverService::coverFailed, this,
            [this](const QString &id) {
        if (categoryId_ == id && categoryIdPlatform_ == QStringLiteral("twitch")) {
            previewHasCategoryCover_ = false;
            UpdatePreviewPlatformVisual();
        }
    });
    loginTimer_ = new QTimer(this); loginTimer_->setSingleShot(true);
    categoryTimer_ = new QTimer(this); categoryTimer_->setSingleShot(true); categoryTimer_->setInterval(350);
    connect(category_, &QLineEdit::textEdited, this, [this]() {
        categoryId_.clear();
        kickCategoryId_.clear();
        categoryIdPlatform_.clear();
        previewHasCategoryCover_ = false;
        UpdatePreviewPlatformVisual();
        categoryTimer_->start();
        UpdatePreview();
    });
    connect(youtubeCategory_, &QLineEdit::textEdited, this, [this]() {
        youtubeCategoryId_.clear();
        UpdatePreview();
    });
    connect(categoryTimer_, &QTimer::timeout, this, &StreamHubControlDock::SearchCategories);
    connect(categoryResults_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        categoryId_ = item->data(Qt::UserRole).toString();
        categoryIdPlatform_ = QStringLiteral("twitch");
        category_->setText(item->text()); categoryResults_->hide();
        previewHasCategoryCover_ = false;
        UpdatePreviewPlatformVisual();
        categoryCoverService_->Fetch(categoryId_);
        UpdatePreview();
    });
    connect(title_, &QTextEdit::textChanged, this, [this]() { LimitText(title_, titleCount_, 140); UpdatePreview(); });
    connect(notification_, &QTextEdit::textChanged, this, [this]() { LimitText(notification_, notificationCount_, 5000); UpdatePreview(); });
    connect(tags_, &QLineEdit::textChanged, this, &StreamHubControlDock::UpdatePreview);
    connect(language_, &QComboBox::currentIndexChanged, this, &StreamHubControlDock::UpdatePreview);
    connect(visibility_, &QComboBox::currentIndexChanged, this, &StreamHubControlDock::UpdatePreview);
    connect(classification_, &QComboBox::currentIndexChanged, this, &StreamHubControlDock::UpdatePreview);
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
        QWidget#accountCard, QWidget#formCard, QWidget#broadcastOverview { background:#0d1726; border:1px solid #29496f; border-radius:10px; }
        QWidget#accountCard { background:#091321; border:1px solid #1b3550; border-radius:12px; }
        QWidget#accountLogPanel { background:#0d1726; border:1px solid #29496f; border-radius:12px; }
        QLabel#accountLogTitle { color:#f2f7ff; font-size:13px; font-weight:800; }
        QLabel#accountLogPlatform { color:#dce8f7; font-size:12px; font-weight:700; }
        QLabel#accountLogMessage { color:#9eb2cb; }
        QLabel#accountName { font-size:16px; font-weight:800; }
        QLabel#accountState[connected="true"] { color:#16d86a; }
        QLabel#accountState[connected="false"] { color:#9eb2cb; }
        QLabel#previewCaption { color:#9146ff; font-size:10px; font-weight:800; }
        QLabel#previewTitle { color:white; font-size:13px; font-weight:800; }
        QLabel#previewMeta, QLabel#previewNotification, QLabel#controlStatus, QLabel#counter, QLabel#syncStatus { color:#9eb2cb; }
        QLabel#syncIndicator { font-size:16px; font-weight:900; }
        QLabel#syncIndicator[state="success"] { color:#16d86a; }
        QLabel#syncIndicator[state="warning"] { color:#ffb000; }
        QLabel#syncIndicator[state="error"] { color:#ff4058; }
        QLabel#syncIndicator[state="pending"] { color:#9146ff; }
        QLabel#broadcastStatus { color:#9eb2cb; }
        QLabel#counter { font-size:10px; }
        QPushButton { min-height:30px; padding:5px 11px; background:#17283a; color:white; border:1px solid #29496f; border-radius:7px; }
        QPushButton:hover { border-color:#00c8ff; background:#1b3248; }
        QPushButton#primaryAction { background:#9146ff; border-color:#b987ff; font-weight:800; }
        QPushButton#accountAction { min-width:95px; min-height:0; max-height:30px; padding:3px 8px; }
        QPushButton#accountActionSecondary { min-width:0px; }
        QPushButton#accountGear { min-width:34px; min-height:34px; max-width:34px; max-height:34px; padding:5px; background:#101a2a; }
        QLineEdit,QComboBox,QTextEdit,QListWidget { padding:6px; background:#080c14; color:#f2f7ff; border:1px solid #29496f; border-radius:7px; }
        QLineEdit:focus,QComboBox:focus,QTextEdit:focus { border-color:#9146ff; }
        QListWidget#categoryResults::item { padding:6px; }
        QListWidget#categoryResults::item:selected { background:#27334d; }
        QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; border:none; }
        QScrollBar:vertical { background:#080c14; width:8px; margin:2px; }
        QScrollBar::handle:vertical { background:#29496f; border-radius:4px; min-height:28px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }
        QScrollBar:horizontal { height:0px; }
    )");
}

void StreamHubControlDock::ConnectTo(int port)
{
    port_ = port;
    if (categoryCoverService_)
        categoryCoverService_->SetPort(port);
    RefreshAccounts();
}

void StreamHubControlDock::UpdatePreviewPlatformVisual()
{
    if (!previewCover_ || !previewCaption_)
        return;

    const QString platform = primaryPlatform_.isEmpty() ? QStringLiteral("twitch") : primaryPlatform_;
    if (!previewHasCategoryCover_)
        previewCover_->setPixmap(QIcon(PreviewPlatformIcon(platform)).pixmap(38, 38));

    previewCaption_->setStyleSheet(
        QStringLiteral("color:%1; font-size:10px; font-weight:800;").arg(PreviewPlatformAccent(platform)));
    previewCaption_->setText(tr("PRÉVIA DA LIVE · %1").arg(PreviewPlatformName(platform)));
}

void StreamHubControlDock::SetPrimaryPlatform(const QString &platform)
{
    const QString normalized = platform.trimmed().toLower();
    if (normalized != "twitch" && normalized != "kick" && normalized != "youtube")
        return;
    if (primaryPlatform_ == normalized) {
        UpdatePreviewPlatformVisual();
        return;
    }

    primaryPlatform_ = normalized;
    if (normalized != "twitch")
        previewHasCategoryCover_ = false;
    UpdatePreviewPlatformVisual();
    const QJsonObject *transmission = nullptr;
    if (normalized == "twitch" && !twitchBroadcastContent_.isEmpty())
        transmission = &twitchBroadcastContent_;
    else if (normalized == "kick" && !kickBroadcastContent_.isEmpty())
        transmission = &kickBroadcastContent_;
    else if (normalized == "youtube" && !youtubeBroadcastContent_.isEmpty())
        transmission = &youtubeBroadcastContent_;

    if (transmission)
        ApplyTransmissionMetadata(*transmission, primaryPlatform_);
}

void StreamHubControlDock::RefreshAccounts()
{
    kickLoginHelp_->clear();
    kickCopyLinkButton_->setProperty("available", false);
    kickAuthorizationUri_.clear();
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
        accountStatus_->setProperty("authenticated", connected);
        accountStatus_->setProperty("connected", connected && !reconnect); accountStatus_->style()->unpolish(accountStatus_); accountStatus_->style()->polish(accountStatus_);
        if (!connected) {
            accountName_->setText(tr("Twitch")); accountStatus_->setText(tr("● Não conectada"));
            connectButton_->setText(tr("Conectar")); twitchSyncButton_->setEnabled(false);
            twitchTransmission_ = {}; twitchBroadcastContent_ = {};
            SetTransmissionSyncNotice("twitch", tr("Conta desconectada; conteúdo não consultado."));
        }
        else if (reconnect) {
            accountName_->setText(QString("Twitch · %1").arg(o.value("name").toString()));
            accountStatus_->setText(tr("● Precisa de novas permissões"));
            connectButton_->setText(tr("Reconectar")); twitchSyncButton_->setEnabled(false);
            twitchTransmission_ = {}; twitchBroadcastContent_ = {};
            SetTransmissionSyncNotice("twitch", tr("Conta precisa de novas permissões; conteúdo não consultado."));
        }
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
        const QString flowId = o.value("flowId").toString();
        const QString url = o.value("verificationUri").toString().trimmed();
        const QString safeUrl = SafeAuthorizationUrl(url);
        if (safeUrl.isEmpty()) {
            loginHelp_->setText(tr("Twitch retornou link de autorização inválido."));
            connectButton_->setEnabled(true);
            return;
        }
        loginHelp_->setText(tr("Use o código <b>%1</b> em <a href=\"%2\">%2</a>")
                                .arg(o.value("userCode").toString().toHtmlEscaped(), safeUrl));
        if (!QDesktopServices::openUrl(QUrl(url)))
            loginHelp_->setText(tr("Navegador não abriu. Abra link Twitch: <a href=\"%1\">%1</a>")
                                    .arg(safeUrl));
        PollTwitchLogin(flowId, o.value("interval").toInt(5));
        QTimer::singleShot(10 * 60 * 1000, this, [this, flowId]() {
            if (!connectButton_->isEnabled()) {
                connectButton_->setEnabled(true);
                accountStatus_->setText(tr("● Autorização Twitch expirou. Tente novamente."));
                loginHelp_->setText(tr("Autorização Twitch expirou. Clique em Conectar novamente."));
            }
        });
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
    twitchTransmission_ = {};
    RefreshPlatformContentStatus(QStringLiteral("twitch"));
    loginHelp_->setText(tr("Buscando dados oficiais de transmissão..."));
    emit twitchTransmissionRequested();
}

void StreamHubControlDock::ApplyTransmissionMetadata(const QJsonObject &transmission,
                                                       const QString &platform)
{
    const QString normalized = platform.trimmed().toLower();
    const QString title = transmission.value("title").toString().trimmed();
    const QString category = transmission.value("category").toString().trimmed();
    const QString language = transmission.value("language").toString().trimmed();

    if (!title.isEmpty() && title_->toPlainText().trimmed().isEmpty())
        title_->setPlainText(title);

    auto setTags = [](QLineEdit *edit, const QJsonArray &values) {
        if (values.isEmpty())
            return;
        QStringList tags;
        for (const auto &value : values) {
            const QString tag = value.toString().trimmed();
            if (!tag.isEmpty())
                tags << tag;
        }
        edit->setText(tags.join(", "));
    };

    const bool sharedMetadataSource = normalized == primaryPlatform_;
    if (sharedMetadataSource) {
        setTags(tags_, transmission.value("tags").toArray());
        const int languageIndex = language_->findData(language);
        if (!language.isEmpty() && languageIndex >= 0)
            language_->setCurrentIndex(languageIndex);
    }

    if (normalized == "twitch") {
        if (!category.isEmpty() && category_->text().trimmed().isEmpty())
            category_->setText(category);
        categoryId_ = transmission.value("categoryId").toString().trimmed();
        categoryIdPlatform_ = categoryId_.isEmpty() ? QString() : QStringLiteral("twitch");
        const QJsonArray labels = transmission.value("classificationLabels").toArray();
        const int classificationIndex = labels.isEmpty()
                                            ? 0
                                            : classification_->findData(labels.first().toString());
        classification_->setCurrentIndex(qMax(0, classificationIndex));
        categoryCoverService_->Fetch(categoryId_);
    } else if (normalized == "kick") {
        if (!category.isEmpty() && category_->text().trimmed().isEmpty())
            category_->setText(category);
        kickCategoryId_ = transmission.value("categoryId").toString().trimmed();
        previewHasCategoryCover_ = false;
        UpdatePreviewPlatformVisual();
    } else if (normalized == "youtube") {
        const QString description = transmission.value("description").toString();
        notification_->setPlainText(description);
        if (!category.isEmpty())
            youtubeCategory_->setText(category);
        youtubeCategoryId_ = transmission.value("categoryId").toString().trimmed();
        const QString visibility = transmission.value("visibility").toString().trimmed().toLower();
        if (!visibility.isEmpty()) {
            const int visibilityIndex = visibility == "public" ? 0 : visibility == "unlisted" ? 1 : 2;
            visibility_->setCurrentIndex(visibilityIndex);
        }
        previewHasCategoryCover_ = false;
        UpdatePreviewPlatformVisual();
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
    const QVector<QPair<QString, QString>> notices{
        {QStringLiteral("Twitch"), twitchSyncNotice_},
        {QStringLiteral("Kick"), kickSyncNotice_},
        {QStringLiteral("YouTube"), youtubeSyncNotice_},
    };
    QStringList failedPlatforms;
    for (const auto &notice : notices) {
        const QString message = notice.second.toLower();
        if (message.contains(QStringLiteral("falha")) || message.contains(QStringLiteral("erro")))
            failedPlatforms << notice.first;
    }
    if (failedPlatforms.isEmpty())
        return;

    if (transmissionSyncStatus_)
        transmissionSyncStatus_->setText(tr("Erro ao atualizar %1").arg(failedPlatforms.join(tr(", "))));
    if (transmissionSyncIndicator_) {
        transmissionSyncIndicator_->setText(QStringLiteral("×"));
        transmissionSyncIndicator_->setProperty("state", "error");
        transmissionSyncIndicator_->style()->unpolish(transmissionSyncIndicator_);
        transmissionSyncIndicator_->style()->polish(transmissionSyncIndicator_);
    }
}

void StreamHubControlDock::SetBroadcastOperationStatus(const QString &message)
{
    if (transmissionSyncStatus_)
        transmissionSyncStatus_->setText(message);
    if (transmissionSyncIndicator_) {
        const bool failed = message.startsWith(tr("Erro"), Qt::CaseInsensitive);
        transmissionSyncIndicator_->setText(failed ? QStringLiteral("×") : QStringLiteral("✓"));
        transmissionSyncIndicator_->setProperty("state", failed ? "error" : "success");
        transmissionSyncIndicator_->style()->unpolish(transmissionSyncIndicator_);
        transmissionSyncIndicator_->style()->polish(transmissionSyncIndicator_);
    }
}

void StreamHubControlDock::RefreshBroadcastContentStatus()
{
    RefreshPlatformContentStatus(QStringLiteral("twitch"));
    RefreshPlatformContentStatus(QStringLiteral("kick"));
    RefreshYoutubeBroadcasts();
}

void StreamHubControlDock::RefreshYoutubeBroadcasts()
{
    if (!youtubeBroadcastSelector_)
        return;
    QString previousId = youtubeBroadcastSelector_->currentData().toString().trimmed();
    if (previousId.isEmpty()) {
        if (auto *profile = obs_frontend_get_profile_config())
            previousId = QString::fromUtf8(config_get_string(profile, "StreamHub", "YoutubeBroadcastId"));
    }
    SetTransmissionSyncNotice(QStringLiteral("youtube"), tr("Buscando eventos disponíveis no YouTube..."));
    Request("GET", "/api/broadcast/youtube/events", {}, [this, previousId](const QJsonObject &response, int status) {
        if (status != 200) {
            const QString detail = response.value("error").toString().trimmed();
            SetTransmissionSyncNotice(QStringLiteral("youtube"), detail.isEmpty()
                ? tr("Não foi possível listar os eventos do YouTube.") : detail);
            return;
        }
        const QSignalBlocker blocker(youtubeBroadcastSelector_);
        youtubeBroadcastSelector_->clear();
        youtubeBroadcastSelector_->addItem(tr("Selecione o evento para sincronizar"), QString());
        const QJsonArray events = response.value("items").toArray();
        for (const QJsonValue &value : events) {
            const QJsonObject event = value.toObject();
            const QString id = event.value("id").toString().trimmed();
            const QString title = event.value("title").toString().trimmed();
            const QString state = event.value("state").toString().trimmed();
            if (id.isEmpty())
                continue;
            const QString visibility = event.value("visibility").toString();
            const QString visibilityLabel = visibility == "private" ? tr("Privada") :
                visibility == "public" ? tr("Pública") : visibility == "unlisted" ? tr("Não listada") : visibility;
            const bool compatibilityKnown=event.contains("rtmpCompatible");
            const bool compatible=event.value("rtmpCompatible").toBool();
            const QString protocolLabel=!compatibilityKnown?tr("Protocolo não verificado"):
                compatible?tr("RTMP/RTMPS disponível"):tr("Não compatível com RTMP — webcam/WebRTC ou sem chave");
            const QString label = tr("%1 · %2 · %3 · %4 · ID: %5")
                .arg(title.isEmpty() ? tr("Sem título") : title,
                     state.isEmpty() ? tr("estado desconhecido") : state,
                     visibilityLabel, protocolLabel, id);
            youtubeBroadcastSelector_->addItem(label, id);
            if(compatibilityKnown)youtubeBroadcastSelector_->setItemData(youtubeBroadcastSelector_->count()-1,compatible,Qt::UserRole+1);
        }
        if (youtubeBroadcastSelector_->count() == 1) {
            youtubeBroadcastSelector_->addItem(tr("Nenhum evento editável encontrado"), QString());
            youtubeBroadcastContent_ = {};
            SetTransmissionSyncNotice(QStringLiteral("youtube"), tr("Nenhum evento editável foi retornado pelo YouTube."));
            return;
        }
        int selected = youtubeBroadcastSelector_->findData(previousId);
        if (selected < 0)
            selected = 0;
        youtubeBroadcastSelector_->setCurrentIndex(selected);
        if(selected==0) {
            youtubeBroadcastContent_={};youtubeTransmission_={};
            SetTransmissionSyncNotice(QStringLiteral("youtube"), previousId.isEmpty()
                ? tr("Escolha um evento marcado RTMP/RTMPS disponível e clique em Sincronizar.")
                : tr("O evento salvo não está mais disponível ou foi encerrado. Escolha um evento RTMP/RTMPS; a seleção não foi trocada automaticamente."));
        }
        if (selected > 0) RefreshPlatformContentStatus(QStringLiteral("youtube"));
    });
}

void StreamHubControlDock::RefreshPlatformContentStatus(const QString &platform)
{
    if (platform == QStringLiteral("twitch"))
        twitchBroadcastContent_ = {};
    else if (platform == QStringLiteral("kick"))
        kickBroadcastContent_ = {};
    else
        youtubeBroadcastContent_ = {};
    SetTransmissionSyncNotice(platform, tr("Consultando conteúdo atual..."));
    QString path = "/api/broadcast/current?platform=" + platform;
    if (platform == QStringLiteral("youtube") && youtubeBroadcastSelector_) {
        const QString broadcastId = youtubeBroadcastSelector_->currentData().toString().trimmed();
        if (!broadcastId.isEmpty())
            path += "&broadcastId=" + QString::fromUtf8(QUrl::toPercentEncoding(broadcastId));
    }
    Request("GET", path, {},
            [this, platform](const QJsonObject &content, int status) {
        if (status != 200 || content.isEmpty()) {
            const QString detail = content.value("error").toString().trimmed();
            SetTransmissionSyncNotice(
                platform, tr("Falha ao confirmar conteúdo: %1")
                              .arg(detail.isEmpty() ? tr("resposta inválida da API") : detail));
            return;
        }
        if (platform == QStringLiteral("twitch"))
            twitchBroadcastContent_ = content;
        else if (platform == QStringLiteral("kick"))
            kickBroadcastContent_ = content;
        else
            youtubeBroadcastContent_ = content;
        ApplyTransmissionMetadata(content, platform);
        if (platform == QStringLiteral("youtube") && content.value("metadataMismatch").toBool()) {
            SetTransmissionSyncNotice(
                platform,
                tr("Os dados do evento no Studio e do vídeo associado ainda estão diferentes."));
        } else {
            SetTransmissionSyncNotice(platform, {});
        }
    });
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
    twitchBroadcastContent_ = transmission;
    ApplyTransmissionMetadata(twitchTransmission_, "twitch");
    SetTransmissionSyncNotice("twitch", {});
    twitchSyncButton_->setEnabled(true);
    loginHelp_->setText(tr("RTMP e chave de transmissão atualizados em Múltiplas saídas."));
    emit twitchTransmissionReceived(twitchTransmission_);
}

void StreamHubControlDock::SetTwitchTransmissionError(const QString &error)
{
    twitchTransmission_ = {};
    twitchSyncButton_->setEnabled(true);
    loginHelp_->setText(tr("Falha na sincronização: %1").arg(error));
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
        kickAccountStatus_->setProperty("authenticated", connected);
        kickAccountStatus_->setProperty("connected", connected && !reconnect);
        kickAccountStatus_->style()->unpolish(kickAccountStatus_);
        kickAccountStatus_->style()->polish(kickAccountStatus_);
        if (!configured) {
            kickAccountName_->setText(tr("Kick"));
            kickAccountStatus_->setText(tr("● OAuth compartilhado indisponível · configure .env"));
            kickConnectButton_->setEnabled(false);
            kickSyncButton_->setEnabled(false);
            kickTransmission_ = {}; kickBroadcastContent_ = {};
            SetTransmissionSyncNotice("kick", tr("Integração sem configuração; conteúdo indisponível."));
        } else if (!connected) {
            kickAccountName_->setText(tr("Kick"));
            kickAccountStatus_->setText(tr("● Não conectada"));
            kickConnectButton_->setText(tr("Conectar"));
            kickConnectButton_->setEnabled(true);
            kickSyncButton_->setEnabled(false);
            kickTransmission_ = {}; kickBroadcastContent_ = {};
            SetTransmissionSyncNotice("kick", tr("Conta desconectada; conteúdo não consultado."));
        } else if (reconnect) {
            kickAccountName_->setText(QString("Kick · %1").arg(o.value("name").toString()));
            kickAccountStatus_->setText(tr("● Precisa de novas permissões"));
            kickConnectButton_->setText(tr("Reconectar"));
            kickConnectButton_->setEnabled(true);
            kickSyncButton_->setEnabled(false);
            kickTransmission_ = {}; kickBroadcastContent_ = {};
            SetTransmissionSyncNotice("kick", tr("Conta precisa de novas permissões; conteúdo não consultado."));
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
    kickAuthorizationUri_.clear();
    kickCopyLinkButton_->setProperty("available", false);
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
        kickAuthorizationUri_ = o.value("authorizationUri").toString().trimmed();
        const QString safeUrl = SafeAuthorizationUrl(kickAuthorizationUri_);
        if (safeUrl.isEmpty()) {
            kickLoginHelp_->setText(tr("Kick retornou link de autorização inválido."));
            kickConnectButton_->setEnabled(true);
            return;
        }
        kickLoginHelp_->setText(tr("Abra ou copie link Kick: <a href=\"%1\">%1</a>").arg(safeUrl));
        kickCopyLinkButton_->setProperty("available", true);
        if (!QDesktopServices::openUrl(QUrl(kickAuthorizationUri_)))
            kickLoginHelp_->setText(tr("Navegador não abriu. Abra link Kick: <a href=\"%1\">%1</a>").arg(safeUrl));
        const QString flowId = o.value("flowId").toString();
        PollKickLogin(flowId);
        QTimer::singleShot(10 * 60 * 1000, this, [this, flowId]() {
            if (!kickConnectButton_->isEnabled()) {
                kickConnectButton_->setEnabled(true);
                kickAccountStatus_->setText(tr("● Autorização Kick expirou. Tente novamente."));
                kickLoginHelp_->setText(tr("Autorização Kick expirou. Clique em Conectar novamente."));
                kickCopyLinkButton_->setProperty("available", false);
                kickAuthorizationUri_.clear();
            }
        });
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
                kickAuthorizationUri_.clear();
                kickCopyLinkButton_->setProperty("available", false);
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
    kickTransmission_ = {};
    RefreshPlatformContentStatus(QStringLiteral("kick"));
    kickLoginHelp_->setText(tr("Buscando dados oficiais de transmissão..."));
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
    kickBroadcastContent_ = transmission;
    ApplyTransmissionMetadata(kickTransmission_, "kick");
    SetTransmissionSyncNotice("kick", {});
    kickSyncButton_->setEnabled(true);
    kickLoginHelp_->setText(tr("RTMP e chave de transmissão atualizados em Múltiplas saídas."));
    emit kickTransmissionReceived(kickTransmission_);
}

void StreamHubControlDock::SetKickTransmissionError(const QString &error)
{
    kickTransmission_ = {};
    kickSyncButton_->setEnabled(true);
    kickLoginHelp_->setText(tr("Falha na sincronização: %1").arg(error));
}

void StreamHubControlDock::StartYoutubeLogin()
{
    QMessageBox consent(QMessageBox::Information, tr("Conectar ao YouTube"),
        tr("Ao continuar, você concorda em usar os serviços da API do YouTube e aceita a Política de Privacidade do StreamHub, conforme os "
           "<a href=\"https://www.youtube.com/t/terms\">Termos de Serviço do YouTube</a> e as "
           "<a href=\"https://developers.google.com/youtube/terms/developer-policies\">políticas para desenvolvedores</a>. "
           "Consulte a <a href=\"https://streamhub-oauth-broker.onrender.com/privacidade\">Política de Privacidade do StreamHub</a> e a "
           "<a href=\"https://policies.google.com/privacy\">Política de Privacidade do Google</a>."),
        QMessageBox::NoButton, this);
    auto *agreeButton = consent.addButton(tr("Concordar e continuar"), QMessageBox::AcceptRole);
    consent.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    consent.setTextFormat(Qt::RichText);
    consent.setTextInteractionFlags(Qt::TextBrowserInteraction);
    consent.exec();
    if (consent.clickedButton() != agreeButton)
        return;

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
        const QString url = o.value("authorizationUri").toString().trimmed();
        const QString safeUrl = SafeAuthorizationUrl(url);
        if (safeUrl.isEmpty()) {
            youtubeLoginHelp_->setText(tr("YouTube retornou link de autorização inválido."));
            youtubeConnectButton_->setEnabled(true);
            return;
        }
        youtubeLoginHelp_->setText(tr("Abra link YouTube: <a href=\"%1\">%1</a>").arg(safeUrl));
        if (!QDesktopServices::openUrl(QUrl(url)))
            youtubeLoginHelp_->setText(tr("Navegador não abriu. Abra link YouTube: <a href=\"%1\">%1</a>").arg(safeUrl));
        const QString flowId = o.value("flowId").toString();
        PollYoutubeLogin(flowId);
        QTimer::singleShot(10 * 60 * 1000, this, [this, flowId]() {
            if (!youtubeConnectButton_->isEnabled()) {
                youtubeConnectButton_->setEnabled(true);
                youtubeAccountStatus_->setText(tr("● Autorização YouTube expirou. Tente novamente."));
                youtubeLoginHelp_->setText(tr("Autorização YouTube expirou. Clique em Conectar novamente."));
            }
        });
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
        youtubeAccountStatus_->setProperty("authenticated", connected);
        const QString source = o.value("credentialSource").toString();
        youtubeAccountStatus_->setProperty("connected", connected && !reconnect);
        youtubeAccountStatus_->style()->unpolish(youtubeAccountStatus_);
        youtubeAccountStatus_->style()->polish(youtubeAccountStatus_);
        if (!configured) {
            youtubeAccountName_->setText(tr("YouTube"));
            youtubeAccountStatus_->setText(tr("● OAuth compartilhado indisponível · configure .env"));
            youtubeConnectButton_->setEnabled(false);
            youtubeSyncButton_->setEnabled(false);
            youtubeTransmission_ = {}; youtubeBroadcastContent_ = {};
            SetTransmissionSyncNotice("youtube", tr("Integração sem configuração; conteúdo indisponível."));
        } else if (!connected) {
            youtubeAccountName_->setText(tr("YouTube"));
            youtubeAccountStatus_->setText(source == "env" ? tr("● Não conectada · credenciais de .env") : tr("● Não conectada"));
            youtubeConnectButton_->setText(tr("Conectar"));
            youtubeConnectButton_->setEnabled(true);
            youtubeSyncButton_->setEnabled(false);
            youtubeTransmission_ = {}; youtubeBroadcastContent_ = {};
            SetTransmissionSyncNotice("youtube", tr("Conta desconectada; conteúdo não consultado."));
        } else if (reconnect) {
            youtubeAccountName_->setText(QString("YouTube · %1").arg(o.value("name").toString()));
            youtubeAccountStatus_->setText(tr("● Precisa de novas permissões"));
            youtubeConnectButton_->setText(tr("Reconectar"));
            youtubeConnectButton_->setEnabled(true);
            youtubeSyncButton_->setEnabled(false);
            youtubeTransmission_ = {}; youtubeBroadcastContent_ = {};
            SetTransmissionSyncNotice("youtube", tr("Conta precisa de novas permissões; conteúdo não consultado."));
        } else {
            youtubeAccountName_->setText(QString("YouTube · %1").arg(o.value("name").toString()));
            youtubeAccountStatus_->setText(tr("● Conectada"));
            youtubeLoginHelp_->clear();
            youtubeConnectButton_->setText(tr("Trocar conta"));
            youtubeConnectButton_->setEnabled(true);
            youtubeSyncButton_->setEnabled(true);
            QTimer::singleShot(0, this, &StreamHubControlDock::RefreshYoutubeBroadcasts);
        }
    });
}

void StreamHubControlDock::SyncYoutubeTransmission()
{
    const QString broadcastId = youtubeBroadcastSelector_ ? youtubeBroadcastSelector_->currentData().toString().trimmed() : QString();
    if (broadcastId.isEmpty()) {
        RefreshYoutubeBroadcasts();
        youtubeLoginHelp_->setText(tr("Selecione o evento na aba Transmissão e clique em Sincronizar novamente."));
        return;
    }
    const QVariant compatible=youtubeBroadcastSelector_->currentData(Qt::UserRole+1);
    if(compatible.isValid()&&!compatible.toBool()) {
        youtubeLoginHelp_->setText(tr("Este evento usa webcam/WebRTC ou não possui chave RTMP. Escolha um evento marcado RTMP/RTMPS disponível na aba Transmissão. Seus servidores e chaves atuais foram preservados."));
        return;
    }
    youtubeSyncButton_->setEnabled(false);
    youtubeTransmission_ = {};
    youtubeLoginHelp_->setText(tr("Buscando dados oficiais do conteúdo da transmissão..."));
    emit youtubeTransmissionRequested(broadcastId);
}

void StreamHubControlDock::SetYoutubeTransmission(const QJsonObject &transmission)
{
    if (!youtubeBroadcastSelector_ || transmission.value("broadcastId").toString() != youtubeBroadcastSelector_->currentData().toString()) {
        SetYoutubeTransmissionError(tr("O evento selecionado mudou. Sincronize novamente o evento desejado."));
        return;
    }
    const QString server = transmission.value("server").toString().trimmed();
    const QString key = transmission.value("streamKey").toString().trimmed();
    if ((!server.startsWith("rtmp://", Qt::CaseInsensitive) &&
         !server.startsWith("rtmps://", Qt::CaseInsensitive)) || key.isEmpty()) {
        SetYoutubeTransmissionError(tr("YouTube não forneceu servidor RTMP e stream key válidos."));
        return;
    }
    youtubeTransmission_ = transmission;
    SetTransmissionSyncNotice("youtube", {});
    youtubeSyncButton_->setEnabled(true);
    youtubeLoginHelp_->setText(tr("Evento sincronizado. Destinos com servidor e chave manuais são preservados."));
    emit youtubeTransmissionReceived(youtubeTransmission_);
}

void StreamHubControlDock::SetYoutubeTransmissionError(const QString &error)
{
    youtubeTransmission_ = {};
    youtubeSyncButton_->setEnabled(true);
    youtubeLoginHelp_->setText(tr("Falha na sincronização: %1").arg(error));
}

void StreamHubControlDock::LoadBroadcast()
{
    if (primaryPlatform_.isEmpty()) {
        SetBroadcastOperationStatus(tr("Serviço principal OBS não identificado."));
        return;
    }
    Request("GET", "/api/broadcast/current?platform=" + primaryPlatform_, {}, [this](const QJsonObject &o, int status) {
        if (status != 200) { SetBroadcastOperationStatus(o.value("error").toString()); return; }
        ApplyTransmissionMetadata(o, primaryPlatform_);
    SetBroadcastOperationStatus(tr("Dados atualizados"));
    });
}

void StreamHubControlDock::ApplyBroadcast()
{
    if (primaryPlatform_.isEmpty()) {
        SetBroadcastOperationStatus(tr("Serviço principal OBS não identificado."));
        return;
    }
    const QString privacyStatus = visibility_->currentIndex() == 0
                                      ? QStringLiteral("public")
                                      : visibility_->currentIndex() == 1
                                          ? QStringLiteral("unlisted")
                                          : QStringLiteral("private");
    const StreamHubBroadcastMetadata metadata{
        title_->toPlainText().trimmed(),
        notification_->toPlainText().trimmed(),
        category_->text().trimmed(),
        categoryId_,
        kickCategoryId_,
        youtubeCategory_->text().trimmed(),
        youtubeCategoryId_,
        tags_->text(),
        language_->currentData().toString(),
        youtubeBroadcastSelector_ ? youtubeBroadcastSelector_->currentData().toString().trimmed() : QString(),
        classification_->currentData().toString(),
        privacyStatus,
        primaryPlatform_,
    };
    const QJsonObject body = StreamHubBroadcastRequest::BuildApplyPayload(metadata);
    UpdateTransmissionSyncNotice();
    SetBroadcastOperationStatus(tr("Atualizando dados..."));
    Request("POST", "/api/broadcast/apply", body, [this](const QJsonObject &o, int status) {
        if (status != 200) { SetBroadcastOperationStatus(tr("Erro ao atualizar dados")); return; }
        QStringList failedPlatforms;
        for (const auto &v : o.value("results").toArray()) {
            const auto result = v.toObject();
            if (result.value("skipped").toBool())
                continue;
            if (!result.value("ok").toBool())
                failedPlatforms << result.value("platform").toString();
        }
        if (failedPlatforms.isEmpty())
            SetBroadcastOperationStatus(tr("Dados atualizados"));
        else
            SetBroadcastOperationStatus(tr("Erro ao atualizar %1").arg(failedPlatforms.join(tr(", "))));
        RefreshBroadcastContentStatus();
    });
}

void StreamHubControlDock::SearchCategories()
{
    const QString query = category_->text().trimmed(); if (query.size() < 2) { categoryResults_->hide(); return; }
    Request("GET", "/api/twitch/categories?q=" + QString::fromUtf8(QUrl::toPercentEncoding(query)), {}, [this](const QJsonObject &o, int status) {
        categoryResults_->clear(); if (status != 200) { categoryResults_->hide(); SetBroadcastOperationStatus(o.value("error").toString()); return; }
        for (const auto &value : o.value("data").toArray()) {
            const auto category = value.toObject();
            const QString id = category.value("id").toString();
            auto *item = new QListWidgetItem(QIcon(":/streamhub-ui/icons/twitch.svg"), category.value("name").toString(), categoryResults_);
            item->setData(Qt::UserRole, id);
            categoryCoverService_->Fetch(id);
        }
        categoryResults_->setVisible(categoryResults_->count() > 0);
    });
}

void StreamHubControlDock::UpdatePreview()
{
    const QString title = title_->toPlainText().trimmed(); previewTitle_->setText(title.isEmpty() ? tr("Título da transmissão") : title);
    const QString category = category_->text().trimmed();
    const QString language = language_->currentText();
    previewCategory_->setText(category.isEmpty() ? tr("Nenhum jogo selecionado") : QString("%1 · %2").arg(category, language));
    const QString notification = notification_->toPlainText().trimmed(); previewNotification_->setText(notification.isEmpty() ? tr("A prévia é atualizada enquanto você edita.") : notification);
}
