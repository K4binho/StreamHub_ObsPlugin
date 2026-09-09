#pragma once

#include <QDir>
#include <QString>

#include "obs.h"
#include "obs-module.h"

// Resolve paths while still in the OBS working directory, before QProcess
// switches to the server directory. Already-absolute paths stay absolute.
inline QString StreamHubAbsolutePath(const QString &path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}

inline QString StreamHubWritableDataPath()
{
    const char *rawPath = obs_get_module_data_path(obs_current_module());
    return rawPath && *rawPath ? StreamHubAbsolutePath(QString::fromUtf8(rawPath)) : QString();
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
