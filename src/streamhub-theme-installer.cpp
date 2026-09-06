#include "pch.h"
#include "streamhub-theme-installer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

#include <obs.h>
#include <util/platform.h>

#include "plugin-support.h"

namespace {
struct ThemeFile {
    const char *resource;
    const char *relativePath;
};

const ThemeFile kThemeFiles[] = {
    {":/streamhub-theme/K4binho_Ma_Fase.obt", "K4binho_Ma_Fase.obt"},
    {":/streamhub-theme/K4binho_Ma_Fase.qss", "K4binho_Ma_Fase.qss"},
    {":/streamhub-theme/INSTALACAO.md", "K4binho-Ma-Fase/INSTALACAO.md"},
    {":/streamhub-theme/backgrounds/ma-fase-background-1920x1080.png",
     "K4binho-Ma-Fase/backgrounds/ma-fase-background-1920x1080.png"},
    {":/streamhub-theme/assets/crown-watermark.png", "K4binho-Ma-Fase/crown-watermark.png"},
    {":/streamhub-theme/assets/checkbox_checked.svg", "K4binho-Ma-Fase/checkbox_checked.svg"},
    {":/streamhub-theme/assets/checkbox_checked_disabled.svg", "K4binho-Ma-Fase/checkbox_checked_disabled.svg"},
    {":/streamhub-theme/assets/checkbox_checked_focus.svg", "K4binho-Ma-Fase/checkbox_checked_focus.svg"},
    {":/streamhub-theme/assets/checkbox_unchecked.svg", "K4binho-Ma-Fase/checkbox_unchecked.svg"},
    {":/streamhub-theme/assets/checkbox_unchecked_disabled.svg", "K4binho-Ma-Fase/checkbox_unchecked_disabled.svg"},
    {":/streamhub-theme/assets/checkbox_unchecked_focus.svg", "K4binho-Ma-Fase/checkbox_unchecked_focus.svg"},
};

bool WriteResource(const ThemeFile &entry, const QString &themesDirectory)
{
    QFile input(QString::fromUtf8(entry.resource));
    if (!input.open(QIODevice::ReadOnly))
        return false;
    const QByteArray content = input.readAll();

    const QString destination = QDir(themesDirectory).filePath(QString::fromUtf8(entry.relativePath));
    if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
        return false;

    QFile current(destination);
    if (current.open(QIODevice::ReadOnly) && current.readAll() == content)
        return true;

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly) || output.write(content) != content.size())
        return false;
    return output.commit();
}

bool InstallTo(const QString &themesDirectory)
{
    if (themesDirectory.isEmpty() || !QDir().mkpath(themesDirectory))
        return false;
    for (const auto &entry : kThemeFiles) {
        if (!WriteResource(entry, themesDirectory))
            return false;
    }
    return true;
}
}

QString StreamHubInstallBundledTheme()
{
    QStringList candidates;
    candidates << QDir(QCoreApplication::applicationDirPath())
                      .absoluteFilePath("../../data/obs-studio/themes");

    char *userThemes = os_get_config_path_ptr("obs-studio/themes");
    if (userThemes) {
        candidates << QString::fromUtf8(userThemes);
        bfree(userThemes);
    }

    candidates.removeDuplicates();
    for (const QString &candidate : candidates) {
        const QString absolute = QDir(candidate).absolutePath();
        if (InstallTo(absolute)) {
            blog(LOG_INFO, TAG "Tema K4binho - Ma Fase instalado/atualizado em: %s",
                 absolute.toUtf8().constData());
            return absolute;
        }
    }

    blog(LOG_WARNING, TAG "Nao foi possivel instalar o tema K4binho - Ma Fase.");
    return {};
}
