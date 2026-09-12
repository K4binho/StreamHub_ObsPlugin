#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>

#include "obs.h"
#include "obs-module.h"

// Resolve dados do módulo e fallback gravável do usuário antes de QProcess
// mudar diretório de trabalho. Caminhos absolutos permanecem absolutos.
inline QString StreamHubAbsolutePath(const QString &path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}

inline QString StreamHubModuleDataPath()
{
    const char *rawPath = obs_get_module_data_path(obs_current_module());
    return rawPath && *rawPath ? StreamHubAbsolutePath(QString::fromUtf8(rawPath)) : QString();
}

inline bool StreamHubPathWritable(const QString &path);

inline QString StreamHubUserDataPath()
{
    if (!QCoreApplication::instance())
        return QString();

    QStringList candidates;
    char *rawPath = obs_module_config_path("streamhub-data/.path");
    if (rawPath) {
        const QString configPath = StreamHubAbsolutePath(QString::fromUtf8(rawPath));
        if (!configPath.isEmpty())
            candidates << QFileInfo(configPath).absolutePath();
        bfree(rawPath);
    }

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!appData.isEmpty())
        candidates << QDir(appData).filePath("streamhub-data");

    const QString genericData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!genericData.isEmpty())
        candidates << QDir(genericData).filePath("streamhub/obs-multi-rtmp");

    for (const QString &candidate : candidates) {
        const QString path = StreamHubAbsolutePath(candidate);
        if (StreamHubPathWritable(path))
            return path;
    }
    return QString();
}

inline bool StreamHubPathWritable(const QString &path)
{
    if (path.isEmpty() || (!QDir().exists(path) && !QDir().mkpath(path)))
        return false;
    QTemporaryFile probe(QDir(path).filePath(".streamhub-write-test-XXXXXX"));
    return probe.open();
}

inline QString StreamHubWritableDataPath(bool allowUserFallback = true)
{
    const QString modulePath = StreamHubModuleDataPath();
    if (StreamHubPathWritable(modulePath))
        return modulePath;
    return allowUserFallback ? StreamHubUserDataPath() : QString();
}

inline QString StreamHubServerPath()
{
    const QString dataPath = StreamHubWritableDataPath();
    return dataPath.isEmpty() ? QString() : QDir(dataPath).filePath("streamhub-server");
}

inline QString StreamHubRuntimePath()
{
    const QString dataPath = StreamHubWritableDataPath();
    return dataPath.isEmpty() ? QString() : QDir(dataPath).filePath("runtime.json");
}

inline QString StreamHubModuleConfigPath(const char *fileName)
{
    char *rawPath = obs_module_config_path(fileName);
    const QString path = rawPath ? StreamHubAbsolutePath(QString::fromUtf8(rawPath)) : QString();
    if (rawPath)
        bfree(rawPath);
    return path;
}
