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
}

bool StreamHubChatSettings::Edit(QWidget *parent, const QString &configPath, QString *error)
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
    const int port = config.value("server").toObject().value("port").toInt(3000);

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Configurar chats do StreamHub"));
    dialog.setObjectName("streamHubSettings");
    dialog.setMinimumWidth(560);
    auto *layout = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    tabs->setObjectName("streamHubSettingsTabs");
    auto *chatPage = new QWidget(tabs);
    auto *chatLayout = new QVBoxLayout(chatPage);
    auto *description = new QLabel(
        QObject::tr("Configure todos os chats disponíveis. As chaves de transmissão ficam no painel Múltiplas saídas. Facebook ainda não possui conector de chat."),
        chatPage);
    description->setWordWrap(true);
    chatLayout->addWidget(description);

    auto *form = new QFormLayout();
    auto *twitchEnabled = new QCheckBox(QObject::tr("Ativar Twitch"), chatPage);
    twitchEnabled->setChecked(twitch.value("enabled").toBool(false));
    auto *twitchChannel = new QLineEdit(twitch.value("channel").toString(), chatPage);
    twitchChannel->setPlaceholderText(QObject::tr("Nome do canal"));
    form->addRow(twitchEnabled);
    form->addRow(QObject::tr("Canal da Twitch:"), twitchChannel);

    auto *kickEnabled = new QCheckBox(QObject::tr("Ativar Kick"), chatPage);
    kickEnabled->setChecked(kick.value("enabled").toBool(false));
    auto *kickChannel = new QLineEdit(kick.value("channel").toString(), chatPage);
    kickChannel->setPlaceholderText(QObject::tr("Nome do canal"));
    form->addRow(kickEnabled);
    form->addRow(QObject::tr("Canal da Kick:"), kickChannel);

    auto *youtubeEnabled = new QCheckBox(QObject::tr("Ativar YouTube"), chatPage);
    youtubeEnabled->setChecked(youtube.value("enabled").toBool(false));
    auto *youtubeApiKey = new QLineEdit(youtube.value("apiKey").toString(), chatPage);
    youtubeApiKey->setPlaceholderText(QObject::tr("Chave da API do YouTube Data v3"));
    youtubeApiKey->setEchoMode(QLineEdit::Password);
    auto *youtubeVideoId = new QLineEdit(youtube.value("videoId").toString(), chatPage);
    youtubeVideoId->setPlaceholderText(QObject::tr("ID do vídeo ou live"));
    form->addRow(youtubeEnabled);
    form->addRow(QObject::tr("API key do YouTube:"), youtubeApiKey);
    form->addRow(QObject::tr("ID da live:"), youtubeVideoId);

    auto *tiktokEnabled = new QCheckBox(QObject::tr("Ativar TikTok (experimental)"), chatPage);
    tiktokEnabled->setChecked(tiktok.value("enabled").toBool(false));
    auto *tiktokUsername = new QLineEdit(tiktok.value("username").toString(), chatPage);
    tiktokUsername->setPlaceholderText(QObject::tr("Usuário sem @"));
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
    bindEnabled(twitchEnabled, {twitchChannel});
    bindEnabled(kickEnabled, {kickChannel});
    bindEnabled(youtubeEnabled, {youtubeApiKey, youtubeVideoId});
    bindEnabled(tiktokEnabled, {tiktokUsername});

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QObject::tr("Salvar e aplicar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("Cancelar"));
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString twitchName = NormalizedChannel(twitchChannel->text());
        const QString kickName = NormalizedChannel(kickChannel->text());
        const QString youtubeKey = youtubeApiKey->text().trimmed();
        const QString youtubeLive = youtubeVideoId->text().trimmed();
        const QString tiktokName = NormalizedChannel(tiktokUsername->text());
        if (twitchEnabled->isChecked() && twitchName.isEmpty()) {
            QMessageBox::warning(&dialog, QObject::tr("Canal obrigatório"),
                                 QObject::tr("Informe o canal da Twitch ou desative esse chat."));
            return;
        }
        if (kickEnabled->isChecked() && kickName.isEmpty()) {
            QMessageBox::warning(&dialog, QObject::tr("Canal obrigatório"),
                                 QObject::tr("Informe o canal da Kick ou desative esse chat."));
            return;
        }
        if (youtubeEnabled->isChecked() && (youtubeKey.isEmpty() || youtubeLive.isEmpty())) {
            QMessageBox::warning(&dialog, QObject::tr("Dados obrigatórios"),
                                 QObject::tr("Informe a API key e o ID da live do YouTube ou desative esse chat."));
            return;
        }
        if (tiktokEnabled->isChecked() && tiktokName.isEmpty()) {
            QMessageBox::warning(&dialog, QObject::tr("Usuário obrigatório"),
                                 QObject::tr("Informe o usuário do TikTok ou desative esse chat."));
            return;
        }

        QJsonObject updatedTwitch = twitch;
        updatedTwitch.insert("enabled", twitchEnabled->isChecked());
        updatedTwitch.insert("channel", twitchName);
        config.insert("twitch", updatedTwitch);

        QJsonObject updatedKick = kick;
        updatedKick.insert("enabled", kickEnabled->isChecked());
        updatedKick.insert("channel", kickName);
        config.insert("kick", updatedKick);

        QJsonObject updatedYoutube = youtube;
        updatedYoutube.insert("enabled", youtubeEnabled->isChecked());
        updatedYoutube.insert("apiKey", youtubeKey);
        updatedYoutube.insert("videoId", youtubeLive);
        config.insert("youtube", updatedYoutube);

        QJsonObject updatedTiktok = tiktok;
        updatedTiktok.insert("enabled", tiktokEnabled->isChecked());
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
