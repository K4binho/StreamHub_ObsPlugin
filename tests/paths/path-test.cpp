#include "../../src/streamhub-paths.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--child"))
        return QFileInfo::exists(app.arguments().last()) ? 0 : 1;

    QTemporaryDir fixture(QDir::tempPath() + "/StreamHub paths + spaces-XXXXXX");
    if (!fixture.isValid())
        return 1;
    const QString bin = fixture.path() + "/obs-studio/bin/64bit";
    const QString expected = fixture.path() + "/obs-studio/data/obs-plugins/obs-multi-rtmp";
    if (!QDir().mkpath(bin) || !QDir().mkpath(expected + "/streamhub-server/server"))
        return 2;
    QFile script(expected + "/streamhub-server/server/index.js");
    if (!script.open(QIODevice::WriteOnly))
        return 3;
    script.close();

    const QString originalCwd = QDir::currentPath();
    if (!QDir::setCurrent(bin))
        return 4;
    const QString relativeData = "../../data/obs-plugins/obs-multi-rtmp";
    const QString resolved = StreamHubAbsolutePath(relativeData);
    if (resolved != expected || StreamHubAbsolutePath(expected) != expected)
        return 5;
    const QString server = StreamHubAbsolutePath(relativeData + "/streamhub-server");
    const QString absoluteScript = QDir(server).absoluteFilePath("server/index.js");

    // Reproduce the old argument being interpreted against the child's cwd.
    QProcess child;
    child.setWorkingDirectory(server);
    child.start(app.applicationFilePath(), {"--child", relativeData + "/streamhub-server/server/index.js"});
    if (!child.waitForFinished(10000) || child.exitStatus() != QProcess::NormalExit || child.exitCode() != 1)
        return 6;
    child.start(app.applicationFilePath(), {"--child", absoluteScript});
    if (!child.waitForFinished(10000) || child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
        return 7;

    QDir::setCurrent(originalCwd);
    std::puts("PASS: relative/absolute paths, spaces and child working directory; old bug reproduced.");
    return 0;
}
