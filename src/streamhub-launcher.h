#pragma once

#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QtGlobal>

class QNetworkAccessManager;
class QNetworkReply;
class StreamHubNodeProvision;

struct StreamHubRuntime {
    qint64 nodePid = 0;
    qint64 obsPid = 0;
    int port = 0;
    QString token;
};

bool StreamHubReadRuntime(const QString &path, StreamHubRuntime *runtime);
bool StreamHubWriteRuntime(const QString &path, const StreamHubRuntime &runtime);
void StreamHubRemoveRuntime(const QString &path);

using StreamHubJobHandle = void *;

class StreamHubLauncher : public QObject {
    Q_OBJECT

public:
    explicit StreamHubLauncher(QObject *parent = nullptr);
    ~StreamHubLauncher() override;

    // serverDir = caminho absoluto pra pasta bundled/streamhub-server
    // (dentro dos dados do plugin, ver obs_get_module_data_path).
    // pluginDataDir = pasta de dados do plugin (raiz), usada pra guardar o
    // runtime portátil do Node.js caso precise ser baixado.
    void Start(const QString &pluginDataDir, const QString &serverDir, int port);
    void Restart();
    void Stop();
    void SyncTwitchTransmission();
    void SyncKickTransmission();
    void SyncYoutubeTransmission();

    int Port() const { return port_; }
    bool IsRunning() const;

signals:
    void statusChanged(const QString &status);
    void twitchTransmissionReady(const QJsonObject &transmission);
    void twitchTransmissionFailed(const QString &error);
    void kickTransmissionReady(const QJsonObject &transmission);
    void kickTransmissionFailed(const QString &error);
    void youtubeTransmissionReady(const QJsonObject &transmission);
    void youtubeTransmissionFailed(const QString &error);

private:
    void OnNodeReady(const QString &nodePath, const QString &npmCliPath);
    void RunNpmInstallThenStart(const QString &nodePath, const QString &npmCliPath);
    void StartServerProcess(const QString &nodePath);
    bool DependenciesAreComplete() const;
    bool SaveDependencyMarker() const;
    bool PreparePreviousInstance();
    bool RequestInternal(const QString &path, const QString &token, qint64 expectedPid,
                         qint64 expectedObsPid, int port) const;
    bool RequestInternalJson(const QString &path, const QString &token, qint64 expectedPid,
                             qint64 expectedObsPid, int port, const QJsonObject &body,
                             QJsonObject *response) const;
    bool WriteRuntime(qint64 nodePid) const;
    void RemoveOwnedRuntime() const;
    QString NewInstanceToken() const;
    void AttachJobObject(QProcess *process);
    void CloseJobObject();

    StreamHubNodeProvision *provisioner_ = nullptr;
    QProcess *npmInstallProcess_ = nullptr;
    QProcess *process_ = nullptr;
    bool twitchSyncInFlight_ = false;
    bool kickSyncInFlight_ = false;
    bool youtubeSyncInFlight_ = false;

    QString serverDir_;
    QString nodePath_;
    QString runtimePath_;
    QString instanceToken_;
    int port_ = 605;
    bool restartPending_ = false;
    bool stopping_ = false;
    StreamHubJobHandle jobHandle_ = nullptr;
};
