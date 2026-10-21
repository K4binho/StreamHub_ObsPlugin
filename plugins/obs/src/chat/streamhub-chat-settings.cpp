#include "streamhub-chat-settings.h"

#include <QCheckBox>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString NormalizedChannel(QString channel)
{
    channel = channel.trimmed();
    while (channel.startsWith('#') || channel.startsWith('@'))
        channel.remove(0, 1);
    return channel;
}

QString ChatStatusText(const QString &platform, const QJsonObject &identity)
{
    const QString account = identity.value("connected").toBool(false)
                                ? QObject::tr("conta conectada")
                                : QObject::tr("conta desconectada");
    const QJsonObject chat = identity.value("chat").toObject();
    const QString state = chat.value("state").toString();
    const QString chatText = chat.value("connected").toBool(false) || state == "connected"
                                 ? QObject::tr("chat ativo")
                                 : state == "connecting"
                                       ? QObject::tr("chat conectando")
                                       : state == "reconnecting"
                                             ? QObject::tr("chat reconectando")
                                             : QObject::tr("chat inativo");
    return QStringLiteral("%1 · %2 · %3").arg(platform, account, chatText);
}
}

bool StreamHubChatSettings::Edit(QWidget *parent, const QString &configPath,
                                const QJsonObject &connectedAccounts, QString *error)
{
    if (!QFileInfo::exists(configPath)) {
        const QFileInfo configInfo(configPath);
        if (!QDir().mkpath(configInfo.absolutePath())) {
            if (error)
                *error = QObject::tr("Não consegui criar a pasta de configuração: %1")
                             .arg(configInfo.absolutePath());
            return false;
        }

        const QString examplePath = configInfo.absolutePath() + "/config.example.json";
        if (!QFile::copy(examplePath, configPath)) {
            if (error)
                *error = QObject::tr("Não consegui criar o config.json a partir de %1")
                             .arg(examplePath);
            return false;
        }
        QFile::setPermissions(configPath, QFile::permissions(configPath) | QFileDevice::WriteOwner);
    }

    QFile input(configPath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Não consegui abrir %1").arg(configPath);
        return false;
    }

    QJsonParseError parseError;
    const QByteArray configBytes = input.readAll();
    input.close(); // No Windows, manter este handle aberto impede QSaveFile de substituir o arquivo.
    const QJsonDocument document = QJsonDocument::fromJson(configBytes, &parseError);
    if (!document.isObject()) {
        if (error)
            *error = QObject::tr("A configuração está inválida: %1").arg(parseError.errorString());
        return false;
    }
    QJsonObject config = document.object();
    const QJsonObject twitch = config.value("twitch").toObject();
    const QJsonObject kick = config.value("kick").toObject();
    const QJsonObject youtube = config.value("youtube").toObject();
    const QJsonObject tiktok = config.value("tiktok").toObject();
    const QJsonObject overlay = config.value("overlay").toObject();
    const QJsonObject hideCommands = overlay.value("hideCommands").toObject();
    const QJsonObject chat = config.value("chat").toObject();
    const QJsonObject twitchIdentity = connectedAccounts.value("twitch").toObject();
    const QJsonObject kickIdentity = connectedAccounts.value("kick").toObject();
    const QJsonObject youtubeIdentity = connectedAccounts.value("youtube").toObject();
    const bool twitchOAuthConnected = twitchIdentity.value("connected").toBool(false) &&
                                      !twitchIdentity.value("name").toString().trimmed().isEmpty();
    const bool kickOAuthConnected = kickIdentity.value("connected").toBool(false) &&
                                    !kickIdentity.value("name").toString().trimmed().isEmpty();
    const bool youtubeOAuthConnected = youtubeIdentity.value("connected").toBool(false) &&
                                       !youtubeIdentity.value("name").toString().trimmed().isEmpty();
    const int port = config.value("server").toObject().value("port").toInt(605);

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Configurar chats do StreamHub"));
    dialog.setObjectName("streamHubSettings");
    dialog.setMinimumWidth(560);
    auto *layout = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    tabs->setObjectName("streamHubSettingsTabs");
    auto *chatPage = new QWidget(tabs);
    auto *chatLayout = new QVBoxLayout(chatPage);
    auto *form = new QFormLayout();
    form->addRow(new QLabel(ChatStatusText(QObject::tr("Twitch"), twitchIdentity), chatPage));
    form->addRow(new QLabel(ChatStatusText(QObject::tr("Kick"), kickIdentity), chatPage));
    form->addRow(new QLabel(ChatStatusText(QObject::tr("YouTube"), youtubeIdentity), chatPage));

    auto *tiktokEnabled = new QCheckBox(QObject::tr("Chat TikTok pausado — integração não oficial"), chatPage);
    tiktokEnabled->setChecked(false);
    tiktokEnabled->setEnabled(false);
    auto *tiktokUsername = new QLineEdit(tiktok.value("username").toString(), chatPage);
    tiktokUsername->setPlaceholderText(QObject::tr("Usuário sem @"));
    tiktokUsername->setEnabled(false);
    form->addRow(tiktokEnabled);
    form->addRow(QObject::tr("Usuário do TikTok:"), tiktokUsername);
    auto *showTimestamps = new QCheckBox(QObject::tr("Mostrar horário nas mensagens"), chatPage);
    showTimestamps->setChecked(chat.value("showTimestamps").toBool(true));
    chatLayout->addLayout(form);
    chatLayout->addWidget(showTimestamps);
    chatLayout->addStretch();
    tabs->addTab(chatPage, QObject::tr("Chats"));

    auto *overlayPage = new QWidget(tabs);
    auto *overlayLayout = new QVBoxLayout(overlayPage);
    auto *overlayDescription = new QLabel(
        QObject::tr("Use esta URL em uma Fonte de navegador do OBS. O fundo é transparente."), overlayPage);
    overlayDescription->setWordWrap(true);
    overlayLayout->addWidget(overlayDescription);
    auto *overlayForm = new QFormLayout();
    auto *duration = new QSpinBox(overlayPage);
    duration->setRange(3, 300);
    duration->setSuffix(QObject::tr(" segundos"));
    duration->setValue(overlay.value("messageDurationSeconds").toInt(20));
    auto *channelName = new QLineEdit(overlay.value("channelName").toString(), overlayPage);
    channelName->setPlaceholderText(QObject::tr("k4binho"));
    auto *highlightMentions = new QCheckBox(QObject::tr("Destacar @k4binho e o nome do canal"), overlayPage);
    highlightMentions->setChecked(overlay.value("highlightMentions").toBool(true));
    overlayForm->addRow(QObject::tr("Tempo na tela:"), duration);
    overlayForm->addRow(QObject::tr("Nome do canal:"), channelName);
    overlayForm->addRow(highlightMentions);
    overlayLayout->addLayout(overlayForm);

    auto *filterTitle = new QLabel(QObject::tr("Ocultar comandos iniciados por !"), overlayPage);
    filterTitle->setObjectName("overlaySectionTitle");
    overlayLayout->addWidget(filterTitle);
    auto *hideTwitch = new QCheckBox(QObject::tr("Twitch"), overlayPage);
    auto *hideKick = new QCheckBox(QObject::tr("Kick"), overlayPage);
    auto *hideYoutube = new QCheckBox(QObject::tr("YouTube"), overlayPage);
    auto *hideTiktok = new QCheckBox(QObject::tr("TikTok"), overlayPage);
    hideTwitch->setChecked(hideCommands.value("twitch").toBool(false));
    hideKick->setChecked(hideCommands.value("kick").toBool(false));
    hideYoutube->setChecked(hideCommands.value("youtube").toBool(false));
    hideTiktok->setChecked(hideCommands.value("tiktok").toBool(false));
    auto *filters = new QHBoxLayout();
    filters->addWidget(hideTwitch);
    filters->addWidget(hideKick);
    filters->addWidget(hideYoutube);
    filters->addWidget(hideTiktok);
    filters->addStretch();
    overlayLayout->addLayout(filters);

    const QString overlayUrl = QString("http://localhost:%1/overlay.html").arg(port);
    auto *urlRow = new QHBoxLayout();
    auto *url = new QLineEdit(overlayUrl, overlayPage);
    url->setReadOnly(true);
    auto *copyUrl = new QPushButton(QObject::tr("Copiar URL"), overlayPage);
    auto *preview = new QPushButton(QObject::tr("Abrir prévia"), overlayPage);
    QObject::connect(copyUrl, &QPushButton::clicked, copyUrl, [url]() {
        QApplication::clipboard()->setText(url->text());
    });
    QObject::connect(preview, &QPushButton::clicked, preview, [url]() {
        QDesktopServices::openUrl(QUrl(url->text() + "?preview=1"));
    });
    urlRow->addWidget(url, 1);
    urlRow->addWidget(copyUrl);
    urlRow->addWidget(preview);
    overlayLayout->addLayout(urlRow);
    overlayLayout->addStretch();
    tabs->addTab(overlayPage, QObject::tr("Overlay"));
    layout->addWidget(tabs);

    const auto bindEnabled = [](QCheckBox *check, const QList<QWidget *> &fields) {
        const auto update = [check, fields]() {
            for (auto *field : fields)
                field->setEnabled(check->isChecked());
        };
        QObject::connect(check, &QCheckBox::toggled, check, [update](bool) { update(); });
        update();
    };
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QObject::tr("Salvar e aplicar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("Cancelar"));
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString tiktokName = NormalizedChannel(tiktokUsername->text());

        QJsonObject updatedTwitch = twitch;
        updatedTwitch.insert("enabled", twitchOAuthConnected || twitch.value("enabled").toBool(false));
        if (twitchOAuthConnected)
            updatedTwitch.insert("channel", NormalizedChannel(twitchIdentity.value("name").toString()));
        config.insert("twitch", updatedTwitch);

        QJsonObject updatedKick = kick;
        updatedKick.insert("enabled", kickOAuthConnected || kick.value("enabled").toBool(false));
        if (kickOAuthConnected)
            updatedKick.insert("channel", NormalizedChannel(kickIdentity.value("name").toString()));
        config.insert("kick", updatedKick);

        QJsonObject updatedYoutube = youtube;
        updatedYoutube.insert("enabled", youtubeOAuthConnected || youtube.value("enabled").toBool(false));
        config.insert("youtube", updatedYoutube);

        QJsonObject updatedTiktok = tiktok;
        updatedTiktok.insert("enabled", false);
        updatedTiktok.insert("username", tiktokName);
        config.insert("tiktok", updatedTiktok);

        QJsonObject updatedChat = chat;
        updatedChat.insert("showTimestamps", showTimestamps->isChecked());
        config.insert("chat", updatedChat);

        QJsonObject updatedOverlay = overlay;
        updatedOverlay.insert("messageDurationSeconds", duration->value());
        updatedOverlay.insert("channelName", NormalizedChannel(channelName->text()));
        updatedOverlay.insert("highlightMentions", highlightMentions->isChecked());
        QJsonObject updatedHideCommands = hideCommands;
        updatedHideCommands.insert("twitch", hideTwitch->isChecked());
        updatedHideCommands.insert("kick", hideKick->isChecked());
        updatedHideCommands.insert("youtube", hideYoutube->isChecked());
        updatedHideCommands.insert("tiktok", hideTiktok->isChecked());
        updatedOverlay.insert("hideCommands", updatedHideCommands);
        config.insert("overlay", updatedOverlay);

        const QByteArray updatedConfig = QJsonDocument(config).toJson(QJsonDocument::Indented);
        QSaveFile output(configPath);
        if (!output.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(&dialog, QObject::tr("Erro ao salvar"),
                                  QObject::tr("Não consegui abrir a configuração para salvar.\n\n%1")
                                      .arg(output.errorString()));
            return;
        }
        if (output.write(updatedConfig) != updatedConfig.size()) {
            const QString detail = output.errorString();
            output.cancelWriting();
            QMessageBox::critical(&dialog, QObject::tr("Erro ao salvar"),
                                  QObject::tr("Não consegui gravar a configuração.\n\n%1").arg(detail));
            return;
        }
        if (!output.commit()) {
            QMessageBox::critical(&dialog, QObject::tr("Erro ao salvar"),
                                  QObject::tr("Não consegui substituir a configuração anterior.\n\n%1")
                                      .arg(output.errorString()));
            return;
        }
        dialog.accept();
    });
    layout->addWidget(buttons);

    return dialog.exec() == QDialog::Accepted;
}
