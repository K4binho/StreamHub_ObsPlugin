#pragma once

#include <QString>
#include <QJsonObject>

class QWidget;

class StreamHubChatSettings {
public:
    // Exibe o formulário modal, preserva campos que ele não edita e salva
    // config.json de forma atômica. Retorna true somente quando o usuário
    // confirmou e o arquivo foi gravado.
    static bool Edit(QWidget *parent, const QString &configPath, const QJsonObject &connectedAccounts,
                     QString *error = nullptr);
};
