#pragma once

#include <QObject>
#include <QFile>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

class QMainWindow;
class QNetworkAccessManager;
class QNetworkReply;

class StreamHubUpdater : public QObject {
public:
    explicit StreamHubUpdater(QMainWindow *parent = nullptr);
    void Start();

private:
    struct PendingAsset {
        QString name;
        QUrl url;
        QString sha256;
        QString finalPath;
        QString temporaryPath;
    };

    void CheckLatest();
    void ReadManifest(const QJsonObject &release, const QJsonObject &manifest);
    void AskToDownload();
    void DownloadNext();
    void FinishDownload();
    void AskToInstall();
    void LaunchInstaller();
    bool ValidateManifest(const QJsonObject &release, const QJsonObject &manifest);
    bool IsAllowedUrl(const QUrl &url) const;
    bool IsNewerVersion(const QString &candidate) const;
    bool IsSha256(const QString &value) const;
    QString UpdateDirectory() const;
    QString ModuleDllPath() const;
    QString HelperPath() const;
    static QByteArray DownloadedHash(const QString &path);

    QMainWindow *window_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
    QNetworkReply *activeReply_ = nullptr;
    QFile activeFile_;
    QList<PendingAsset> pendingAssets_;
    int pendingIndex_ = 0;
    QString currentVersion_;
    QString candidateVersion_;
    QString releaseTag_;
    QString updateDirectory_;
    QString downloadedDllPath_;
    QString downloadedHelperPath_;
    bool started_ = false;
};
