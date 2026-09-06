#pragma once

#include <QDir>
#include <QString>

// Resolve paths while still in the OBS working directory, before QProcess
// switches to the server directory. Already-absolute paths stay absolute.
inline QString StreamHubAbsolutePath(const QString &path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}
