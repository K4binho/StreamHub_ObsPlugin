#include "streamhub-node-provision.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#include "plugin-support.h"

namespace {
constexpr const char *kMarkerFile = "resolved-node-path.txt";
constexpr const char *kArchiveName = "node-runtime-download";

QString FindExecutableUnder(const QString &rootDir, const QString &exeName)
{
    // Procura node.exe/node dentro da árvore extraída, ignorando
    // node_modules (evita descer em milhares de arquivos à toa).
    QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        if (path.contains("/node_modules/") || path.contains("\\node_modules\\")) {
            continue;
        }
        QFileInfo fi(path);
        if (fi.fileName().compare(exeName, Qt::CaseInsensitive) == 0) {
            return path;
        }
    }
    return {};
}
}

StreamHubNodeProvision::StreamHubNodeProvision(QObject *parent) : QObject(parent)
{
    net_ = new QNetworkAccessManager(this);
}

void StreamHubNodeProvision::Ensure(const QString &pluginDataDir)
{
    pluginDataDir_ = pluginDataDir;
    TryBundledRuntime();
}

void StreamHubNodeProvision::TryBundledRuntime()
{
    QString markerPath = QDir(pluginDataDir_).filePath(QString("node-runtime/%1").arg(kMarkerFile));
    QFile marker(markerPath);
    if (marker.exists() && marker.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&marker);
        QString savedNode = in.readLine();
        QString savedNpmCli = in.readLine();
        marker.close();

        if (!savedNode.isEmpty() && QFileInfo::exists(savedNode)) {
            // Já resolvemos isso em uma abertura anterior do OBS — usa
            // direto, sem rede, sem varrer diretório. Este é o caminho
            // percorrido em toda abertura normal, exceto a primeiríssima.
            nodePath_ = savedNode;
            npmCliPath_ = savedNpmCli;
            emit statusChanged(tr("Node.js pronto."));
            emit ready(nodePath_, npmCliPath_);
            return;
        }
    }

    TrySystemNode();
}

void StreamHubNodeProvision::TrySystemNode()
{
    QString systemNode = QStandardPaths::findExecutable("node");
    if (!systemNode.isEmpty()) {
        emit statusChanged(tr("Usando Node.js já instalado no sistema."));
        FinishWithNode(systemNode);
        return;
    }

    // Sem marker salvo e sem Node no PATH: só resta baixar o runtime
    // portátil. Só acontece na primeiríssima execução em máquinas sem
    // Node — nas seguintes o marker.txt resolve tudo na hora.
    DownloadPortableRuntime();
}

void StreamHubNodeProvision::DownloadPortableRuntime()
{
    QString url = PlatformDownloadUrl();
    if (url.isEmpty()) {
        emit failed(tr("Não sei baixar automaticamente o Node.js pra este sistema operacional/arquitetura. "
                        "Instale o Node.js manualmente (nodejs.org) e reabra o OBS."));
        return;
    }

    QDir().mkpath(QDir(pluginDataDir_).filePath("node-runtime"));
    QString archivePath =
        QDir(pluginDataDir_).filePath(QString("node-runtime/%1%2").arg(kArchiveName, PlatformArchiveExtension()));

    emit statusChanged(tr("Node.js não encontrado — baixando runtime portátil (~30 MB, só desta vez)..."));

    auto *file = new QFile(archivePath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        emit failed(tr("Não consegui criar o arquivo temporário do download em %1").arg(archivePath));
        file->deleteLater();
        return;
    }

    QNetworkRequest req{QUrl(url)};
    activeDownload_ = net_->get(req);

    connect(activeDownload_, &QNetworkReply::readyRead, this, [this, file]() {
        file->write(activeDownload_->readAll());
    });

    connect(activeDownload_, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            int pct = static_cast<int>((received * 100) / total);
            emit statusChanged(tr("Baixando runtime do Node.js... %1%").arg(pct));
        }
    });

    connect(activeDownload_, &QNetworkReply::finished, this, [this, file, archivePath]() {
        file->flush();
        file->close();
        file->deleteLater();

        auto *reply = activeDownload_;
        activeDownload_ = nullptr;

        if (reply->error() != QNetworkReply::NoError) {
            emit failed(tr("Falha ao baixar o Node.js portátil: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        reply->deleteLater();

        ExtractAndFinish(archivePath);
    });
}

void StreamHubNodeProvision::ExtractAndFinish(const QString &archivePath)
{
    emit statusChanged(tr("Extraindo runtime do Node.js..."));

    QString destDir = QDir(pluginDataDir_).filePath("node-runtime");

    extractProcess_ = new QProcess(this);

#if defined(_WIN32)
    // Sem dependência extra: usa o Expand-Archive do próprio Windows
    // (PowerShell já vem em qualquer Windows 10/11).
    QString cmd = QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                      .arg(archivePath, destDir);
    extractProcess_->start("powershell", {"-NoProfile", "-NonInteractive", "-Command", cmd});
#else
    // macOS e Linux: tar já vem instalado por padrão em ambos.
    extractProcess_->start("tar", {"-xzf", archivePath, "-C", destDir});
#endif

    connect(extractProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit failed(tr("Não consegui iniciar a ferramenta de extração do sistema "
                        "(PowerShell/tar). Instale o Node.js manualmente e reabra o OBS."));
    });

    connect(extractProcess_, &QProcess::finished, this,
            [this, destDir, archivePath](int exitCode, QProcess::ExitStatus) {
                QFile::remove(archivePath); // não precisa mais do .zip/.tar.gz

                if (exitCode != 0) {
                    emit failed(tr("Falha ao extrair o runtime do Node.js baixado (código %1).").arg(exitCode));
                    return;
                }

#if defined(_WIN32)
                QString exeName = "node.exe";
#else
                QString exeName = "node";
#endif
                QString nodeExe = FindExecutableUnder(destDir, exeName);
                if (nodeExe.isEmpty()) {
                    emit failed(tr("Extraí o runtime do Node.js mas não achei o executável dentro dele."));
                    return;
                }

                FinishWithNode(nodeExe);
            });
}

void StreamHubNodeProvision::FinishWithNode(const QString &nodeExePath)
{
    nodePath_ = nodeExePath;
    npmCliPath_ = ResolveNpmCli(nodeExePath);

    SaveMarker();

    emit statusChanged(tr("Runtime do Node.js pronto."));
    emit ready(nodePath_, npmCliPath_);
}

void StreamHubNodeProvision::SaveMarker() const
{
    // Salva o resultado (caminho do node + npm-cli.js resolvidos) pra
    // próxima abertura do OBS não precisar checar PATH nem rede de novo —
    // é isso que faz só a PRIMEIRA abertura ser mais lenta.
    QDir().mkpath(QDir(pluginDataDir_).filePath("node-runtime"));
    QFile marker(QDir(pluginDataDir_).filePath(QString("node-runtime/%1").arg(kMarkerFile)));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&marker);
        out << nodePath_ << "\n" << npmCliPath_ << "\n";
    }
}

QString StreamHubNodeProvision::ResolveNpmCli(const QString &nodeExePath) const
{
    QFileInfo fi(nodeExePath);
    QDir binDir = fi.absoluteDir();

    // Layout comum do build portátil do Windows: node.exe e
    // node_modules/npm/bin/npm-cli.js na mesma pasta.
    QString candidate1 = binDir.filePath("node_modules/npm/bin/npm-cli.js");
    if (QFileInfo::exists(candidate1)) {
        return candidate1;
    }

    // Layout comum em Unix (bin/node ao lado de ../lib/node_modules/npm/...).
    QDir parent = binDir;
    parent.cdUp();
    QString candidate2 = parent.filePath("lib/node_modules/npm/bin/npm-cli.js");
    if (QFileInfo::exists(candidate2)) {
        return candidate2;
    }

    // Não achei — quem for rodar "npm install" cai pra invocar o binário
    // "npm"/"npm.cmd" via linha de comando do SO em vez do npm-cli.js.
    return {};
}

QString StreamHubNodeProvision::PlatformDownloadUrl()
{
    QString base = QString("https://nodejs.org/dist/v%1/node-v%1-").arg(kNodeVersion);
#if defined(_WIN32)
    #if defined(_WIN64)
        return base + "win-x64.zip";
    #else
        return base + "win-x86.zip";
    #endif
#elif defined(Q_OS_MACOS)
    #if defined(__aarch64__)
        return base + "darwin-arm64.tar.gz";
    #else
        return base + "darwin-x64.tar.gz";
    #endif
#elif defined(Q_OS_LINUX)
    #if defined(__aarch64__)
        return base + "linux-arm64.tar.gz";
    #else
        return base + "linux-x64.tar.gz";
    #endif
#else
    return {};
#endif
}

QString StreamHubNodeProvision::PlatformArchiveExtension()
{
#if defined(_WIN32)
    return ".zip";
#else
    return ".tar.gz";
#endif
}
