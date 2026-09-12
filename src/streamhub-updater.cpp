#include "streamhub-updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include "obs-module.h"
#include "plugin-support.h"

namespace {
constexpr auto kApiUrl = "https://api.github.com/repos/K4binho/StreamHub_ObsPlugin/releases/latest";
constexpr auto kManifestName = "streamhub-update.json";
constexpr auto kDllName = "obs-multi-rtmp.dll";
constexpr auto kHelperName = "streamhub-updater-helper.exe";
constexpr auto kPlatform = "windows-x64";

QString NormalizeVersion(QString value)
{
    value = value.trimmed();
    if (value.startsWith('v', Qt::CaseInsensitive))
        value.remove(0, 1);
    return value;
}

QVector<int> VersionParts(const QString &value)
{
    const QStringList parts = NormalizeVersion(value).split('.', Qt::KeepEmptyParts);
    QVector<int> result;
    for (const QString &part : parts) {
        bool ok = false;
        const int number = part.toInt(&ok);
        if (!ok || number < 0)
            return {};
        result.push_back(number);
    }
    return result;
}

QString AssetValue(const QJsonObject &manifest, const char *key)
{
    return manifest.value(QLatin1String(key)).toString().trimmed();
}
}

StreamHubUpdater::StreamHubUpdater(QMainWindow *parent)
    : QObject(parent), window_(parent), network_(new QNetworkAccessManager(this))
{
}

void StreamHubUpdater::Start()
{
#if defined(_WIN32)
    if (started_)
        return;
    started_ = true;
    QTimer::singleShot(8000, this, &StreamHubUpdater::CheckLatest);
    QTimer::singleShot(24 * 60 * 60 * 1000, this, &StreamHubUpdater::CheckLatest);
#else
    Q_UNUSED(window_);
#endif
}

void StreamHubUpdater::CheckLatest()
{
#if defined(_WIN32)
    QNetworkRequest request{QUrl(QString::fromLatin1(kApiUrl))};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "StreamHub-OBS-Plugin");
    auto *reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray payload = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString error = reply->errorString();
        reply->deleteLater();
        if (status != 200 || payload.isEmpty())
            return;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return;
        const QJsonObject release = document.object();
        const QString manifestUrl = [&release]() {
            for (const auto &value : release.value("assets").toArray()) {
                const QJsonObject asset = value.toObject();
                if (asset.value("name").toString() == QLatin1String(kManifestName))
                    return asset.value("browser_download_url").toString();
            }
            return QString();
        }();
        if (manifestUrl.isEmpty() || !IsAllowedUrl(QUrl(manifestUrl)))
            return;

        QNetworkRequest manifestRequest{QUrl(manifestUrl)};
        manifestRequest.setRawHeader("Accept", "application/json");
        manifestRequest.setRawHeader("User-Agent", "StreamHub-OBS-Plugin");
        auto *manifestReply = network_->get(manifestRequest);
        connect(manifestReply, &QNetworkReply::finished, this, [this, manifestReply, release]() {
            const QByteArray payload = manifestReply->readAll();
            const int status = manifestReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            manifestReply->deleteLater();
            if (status != 200 || payload.isEmpty())
                return;
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                return;
            ReadManifest(release, document.object());
        });
    });
#else
    return;
#endif
}

bool StreamHubUpdater::IsAllowedUrl(const QUrl &url) const
{
    if (!url.isValid() || url.scheme().compare("https", Qt::CaseInsensitive) != 0)
        return false;
    const QString host = url.host().toLower();
    return host == QLatin1String("api.github.com") || host == QLatin1String("github.com") ||
           host == QLatin1String("objects.githubusercontent.com") ||
           host == QLatin1String("release-assets.githubusercontent.com");
}

bool StreamHubUpdater::IsSha256(const QString &value) const
{
    if (value.size() != 64)
        return false;
    for (const QChar character : value) {
        if (!character.isDigit() && (character.toLower() < 'a' || character.toLower() > 'f'))
            return false;
    }
    return true;
}

bool StreamHubUpdater::IsNewerVersion(const QString &candidate) const
{
    const QVector<int> current = VersionParts(currentVersion_);
    const QVector<int> incoming = VersionParts(candidate);
    if (current.isEmpty() || incoming.isEmpty())
        return false;
    const int count = qMax(current.size(), incoming.size());
    for (int index = 0; index < count; ++index) {
        const int left = index < incoming.size() ? incoming.at(index) : 0;
        const int right = index < current.size() ? current.at(index) : 0;
        if (left != right)
            return left > right;
    }
    return false;
}

bool StreamHubUpdater::ValidateManifest(const QJsonObject &release, const QJsonObject &manifest)
{
    candidateVersion_ = NormalizeVersion(AssetValue(manifest, "version"));
    currentVersion_ = QString::fromUtf8(PLUGIN_VERSION);
    releaseTag_ = release.value("tag_name").toString().trimmed();
    const QString bundleVersion = AssetValue(manifest, "bundleVersion");
    bool bundleVersionOk = false;
    const int parsedBundleVersion = bundleVersion.toInt(&bundleVersionOk);
    if (!bundleVersionOk || parsedBundleVersion < 1 ||
        !IsNewerVersion(candidateVersion_) || NormalizeVersion(releaseTag_) != candidateVersion_)
        return false;
    if (AssetValue(manifest, "platform") != QLatin1String(kPlatform))
        return false;
    if (AssetValue(manifest, "dll") != QLatin1String(kDllName) ||
        AssetValue(manifest, "helper") != QLatin1String(kHelperName))
        return false;
    if (!IsSha256(AssetValue(manifest, "dllSha256")) ||
        !IsSha256(AssetValue(manifest, "helperSha256")))
        return false;

    QHash<QString, QJsonObject> assets;
    for (const auto &value : release.value("assets").toArray()) {
        const QJsonObject asset = value.toObject();
        assets.insert(asset.value("name").toString(), asset);
    }
    for (const QString &name : {QString::fromLatin1(kDllName), QString::fromLatin1(kHelperName)}) {
        const QJsonObject asset = assets.value(name);
        const QUrl url(asset.value("browser_download_url").toString());
        if (asset.isEmpty() || !IsAllowedUrl(url))
            return false;
    }

    pendingAssets_.clear();
    const QJsonObject dllAsset = assets.value(QString::fromLatin1(kDllName));
    const QJsonObject helperAsset = assets.value(QString::fromLatin1(kHelperName));
    const QString updateDir = UpdateDirectory();
    if (updateDir.isEmpty())
        return false;
    updateDirectory_ = updateDir;
    pendingIndex_ = 0;
    pendingAssets_ = {
        {QString::fromLatin1(kDllName), QUrl(dllAsset.value("browser_download_url").toString()),
         AssetValue(manifest, "dllSha256"), QDir(updateDir).filePath(kDllName),
         QDir(updateDir).filePath(QStringLiteral("%1.download").arg(kDllName))},
        {QString::fromLatin1(kHelperName), QUrl(helperAsset.value("browser_download_url").toString()),
         AssetValue(manifest, "helperSha256"), QDir(updateDir).filePath(kHelperName),
         QDir(updateDir).filePath(QStringLiteral("%1.download").arg(kHelperName))},
    };
    return true;
}

QString StreamHubUpdater::UpdateDirectory() const
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty())
        return {};
    const QString path = QDir(root).filePath("StreamHub/updates");
    return QDir().mkpath(path) ? path : QString();
}

QString StreamHubUpdater::ModuleDllPath() const
{
#if defined(_WIN32)
    const char *raw = obs_get_module_file_name(obs_current_module());
    return raw ? QString::fromUtf8(raw) : QString();
#else
    return {};
#endif
}

QString StreamHubUpdater::HelperPath() const
{
    return QDir(updateDirectory_).filePath(kHelperName);
}

QByteArray StreamHubUpdater::DownloadedHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
        hash.addData(file.read(1024 * 1024));
    return hash.result().toHex();
}

void StreamHubUpdater::ReadManifest(const QJsonObject &release, const QJsonObject &manifest)
{
#if defined(_WIN32)
    if (ValidateManifest(release, manifest))
        AskToDownload();
#else
    Q_UNUSED(release);
    Q_UNUSED(manifest);
#endif
}

void StreamHubUpdater::AskToDownload()
{
    QMessageBox box(QMessageBox::Information, tr("Atualização StreamHub disponível"),
                    tr("Versão %1 disponível. Baixar arquivos em segundo plano?").arg(candidateVersion_),
                    QMessageBox::NoButton, window_);
    auto *download = box.addButton(tr("Baixar em segundo plano"), QMessageBox::AcceptRole);
    box.addButton(tr("Depois"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == download)
        DownloadNext();
}

void StreamHubUpdater::DownloadNext()
{
    if (pendingIndex_ >= pendingAssets_.size()) {
        AskToInstall();
        return;
    }
    const PendingAsset &asset = pendingAssets_.at(pendingIndex_);
    QFile::remove(asset.temporaryPath);
    activeFile_.setFileName(asset.temporaryPath);
    if (!activeFile_.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(window_, tr("Atualização StreamHub"),
                             tr("Não foi possível criar arquivo temporário de atualização."));
        return;
    }
    QNetworkRequest request(asset.url);
    request.setRawHeader("User-Agent", "StreamHub-OBS-Plugin");
    activeReply_ = network_->get(request);
    connect(activeReply_, &QNetworkReply::readyRead, this, [this]() {
        activeFile_.write(activeReply_->readAll());
    });
    connect(activeReply_, &QNetworkReply::finished, this, &StreamHubUpdater::FinishDownload);
}

void StreamHubUpdater::FinishDownload()
{
    if (!activeReply_)
        return;
    activeFile_.write(activeReply_->readAll());
    activeFile_.flush();
    activeFile_.close();
    QNetworkReply *reply = activeReply_;
    activeReply_ = nullptr;
    const PendingAsset asset = pendingAssets_.at(pendingIndex_);
    const bool valid = reply->error() == QNetworkReply::NoError &&
                       DownloadedHash(asset.temporaryPath).compare(asset.sha256.toLatin1(), Qt::CaseInsensitive) == 0;
    const QString error = reply->errorString();
    reply->deleteLater();
    if (!valid) {
        QFile::remove(asset.temporaryPath);
        QMessageBox::warning(window_, tr("Atualização StreamHub"),
                             error.isEmpty() ? tr("Hash do arquivo de atualização não confere.") : error);
        return;
    }
    QFile::remove(asset.finalPath);
    if (!QFile::rename(asset.temporaryPath, asset.finalPath)) {
        QMessageBox::warning(window_, tr("Atualização StreamHub"), tr("Não foi possível preparar arquivo de atualização."));
        return;
    }
    ++pendingIndex_;
    DownloadNext();
}

void StreamHubUpdater::AskToInstall()
{
    downloadedDllPath_ = pendingAssets_.at(0).finalPath;
    downloadedHelperPath_ = pendingAssets_.at(1).finalPath;
    QMessageBox box(QMessageBox::Information, tr("Atualização pronta"),
                    tr("Versão %1 baixada. Reiniciar OBS agora para aplicar?").arg(candidateVersion_),
                    QMessageBox::NoButton, window_);
    auto *restart = box.addButton(tr("Reiniciar agora"), QMessageBox::AcceptRole);
    box.addButton(tr("Depois"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == restart)
        LaunchInstaller();
}

void StreamHubUpdater::LaunchInstaller()
{
#if defined(_WIN32)
    const QString target = ModuleDllPath();
    if (target.isEmpty() || !QFileInfo::exists(downloadedDllPath_) || !QFileInfo::exists(downloadedHelperPath_)) {
        QMessageBox::warning(window_, tr("Atualização StreamHub"), tr("Arquivos de atualização não estão disponíveis."));
        return;
    }
    const QStringList arguments{
        "--pid", QString::number(QCoreApplication::applicationPid()),
        "--source", downloadedDllPath_,
        "--target", target,
        "--sha256", pendingAssets_.at(0).sha256,
        "--restart", QCoreApplication::applicationFilePath(),
    };
    if (!QProcess::startDetached(downloadedHelperPath_, arguments, updateDirectory_)) {
        QMessageBox::warning(window_, tr("Atualização StreamHub"), tr("Não foi possível iniciar atualizador."));
        return;
    }
    QCoreApplication::quit();
#endif
}
