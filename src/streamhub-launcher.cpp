#include "streamhub-launcher.h"
#include "streamhub-node-provision.h"
#include "streamhub-paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QSaveFile>

#include "obs.h"

#include "plugin-support.h"

StreamHubLauncher::StreamHubLauncher(QObject *parent) : QObject(parent)
{
    provisioner_ = new StreamHubNodeProvision(this);
    connect(provisioner_, &StreamHubNodeProvision::statusChanged, this, &StreamHubLauncher::statusChanged);
    connect(provisioner_, &StreamHubNodeProvision::ready, this, &StreamHubLauncher::OnNodeReady);
    connect(provisioner_, &StreamHubNodeProvision::failed, this, [this](const QString &error) {
        blog(LOG_WARNING, "[streamhub] %s", error.toUtf8().constData());
        emit statusChanged(error);
    });
}

StreamHubLauncher::~StreamHubLauncher()
{
    Stop();
}

void StreamHubLauncher::Start(const QString &pluginDataDir, const QString &serverDir, int port)
{
    if (process_ != nullptr || npmInstallProcess_ != nullptr) {
        return; // já está rodando ou já está sendo preparado
    }

    serverDir_ = StreamHubAbsolutePath(serverDir);
    port_ = port;
    stopping_ = false;

    // Tudo a partir daqui é assíncrono (sinais/slots) — esta função sempre
    // retorna na hora, então o OBS nunca fica travado esperando isso,
    // mesmo na primeiríssima execução numa máquina sem Node.js instalado.
    emit statusChanged(tr("Preparando StreamHub..."));
    provisioner_->Ensure(StreamHubAbsolutePath(pluginDataDir));
}

void StreamHubLauncher::OnNodeReady(const QString &nodePath, const QString &npmCliPath)
{
    nodePath_ = nodePath;
    QDir nodeModules(QDir(serverDir_).filePath("node_modules"));
    if (nodeModules.exists() && DependenciesAreComplete()) {
        // Dependências já instaladas (aberturas anteriores do OBS já
        // resolveram isso) — pula direto pro servidor.
        StartServerProcess(nodePath);
        return;
    }

    if (nodeModules.exists()) {
        blog(LOG_WARNING, "[streamhub] dependências incompletas ou desatualizadas; executando npm install para reparar");
    }
    RunNpmInstallThenStart(nodePath, npmCliPath);
}

void StreamHubLauncher::Restart()
{
    if (nodePath_.isEmpty()) {
        emit statusChanged(tr("O serviço ainda está sendo preparado; tente aplicar novamente em instantes."));
        return;
    }

    restartPending_ = true;
    stopping_ = false;
    emit statusChanged(tr("Reiniciando chats do StreamHub..."));
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->terminate();
        return;
    }

    restartPending_ = false;
    StartServerProcess(nodePath_);
}

bool StreamHubLauncher::DependenciesAreComplete() const
{
    QFile packageFile(QDir(serverDir_).filePath("package.json"));
    QFile markerFile(QDir(serverDir_).filePath(".dependencies-sha256"));
    if (!packageFile.open(QIODevice::ReadOnly) ||
        !markerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QByteArray expected =
        QCryptographicHash::hash(packageFile.readAll(), QCryptographicHash::Sha256).toHex();
    return markerFile.readAll().trimmed() == expected;
}

bool StreamHubLauncher::SaveDependencyMarker() const
{
    QFile packageFile(QDir(serverDir_).filePath("package.json"));
    if (!packageFile.open(QIODevice::ReadOnly))
        return false;

    const QByteArray digest =
        QCryptographicHash::hash(packageFile.readAll(), QCryptographicHash::Sha256).toHex();
    QSaveFile markerFile(QDir(serverDir_).filePath(".dependencies-sha256"));
    if (!markerFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    if (markerFile.write(digest + '\n') < 0)
        return false;
    return markerFile.commit();
}

void StreamHubLauncher::RunNpmInstallThenStart(const QString &nodePath, const QString &npmCliPath)
{
    emit statusChanged(tr("Primeira execução — instalando dependências do Node em segundo plano "
                           "(o OBS continua liberado normalmente)..."));

    npmInstallProcess_ = new QProcess(this);
    npmInstallProcess_->setWorkingDirectory(serverDir_);

    if (!npmCliPath.isEmpty()) {
        // Caminho mais confiável: chama o npm-cli.js direto com o node que
        // já resolvemos, sem depender de wrapper .cmd/.sh nenhum.
        npmInstallProcess_->start(nodePath, {npmCliPath, "install", "--omit=dev"});
    } else {
        // Fallback: invoca o "npm" do sistema via linha de comando do SO
        // (necessário no Windows pra resolver o npm.cmd corretamente).
#if defined(_WIN32)
        npmInstallProcess_->start("cmd", {"/c", "npm", "install", "--omit=dev"});
#else
        npmInstallProcess_->start("npm", {"install", "--omit=dev"});
#endif
    }

    connect(npmInstallProcess_, &QProcess::readyReadStandardError, this, [this]() {
        blog(LOG_INFO, "[streamhub-npm] %s", npmInstallProcess_->readAllStandardError().constData());
    });

    // SEM waitForFinished() — o resultado chega pelo sinal finished(),
    // então a thread principal do OBS nunca é bloqueada, nem por um
    // segundo, mesmo que o npm install demore minutos numa internet lenta.
    connect(npmInstallProcess_, &QProcess::finished, this,
            [this, nodePath](int exitCode, QProcess::ExitStatus) {
                npmInstallProcess_->deleteLater();
                npmInstallProcess_ = nullptr;

                if (exitCode != 0) {
                    emit statusChanged(tr("npm install falhou (código %1) — confira o log do OBS.").arg(exitCode));
                    blog(LOG_WARNING, "[streamhub] npm install terminou com código %d", exitCode);
                    return;
                }

                if (!SaveDependencyMarker()) {
                    blog(LOG_WARNING, "[streamhub] dependências instaladas, mas não consegui salvar o marcador");
                }
                emit statusChanged(tr("Dependências instaladas — iniciando servidor..."));
                StartServerProcess(nodePath);
            });

    connect(npmInstallProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        blog(LOG_WARNING, "[streamhub] não consegui rodar o npm install (código %d)", (int)err);
        emit statusChanged(tr("Não consegui rodar o npm install — confira se o Node.js está OK."));
    });
}

void StreamHubLauncher::StartServerProcess(const QString &nodePath)
{
    auto *serverProcess = new QProcess(this);
    process_ = serverProcess;
    serverProcess->setWorkingDirectory(serverDir_);

    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("PORT", QString::number(port_));
    serverProcess->setProcessEnvironment(env);

    connect(serverProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        blog(LOG_WARNING, "[streamhub] não consegui iniciar o servidor Node (código %d).", (int)err);
        emit statusChanged(tr("Não consegui iniciar o servidor do StreamHub."));
    });

    connect(serverProcess, &QProcess::readyReadStandardError, this, [serverProcess]() {
        blog(LOG_INFO, "[streamhub-node] %s", serverProcess->readAllStandardError().constData());
    });

    connect(serverProcess, &QProcess::finished, this,
            [this, serverProcess](int exitCode, QProcess::ExitStatus) {
                if (process_ == serverProcess)
                    process_ = nullptr;
                serverProcess->deleteLater();
                if (restartPending_ && !stopping_) {
                    restartPending_ = false;
                    StartServerProcess(nodePath_);
                } else if (!stopping_ && exitCode != 0) {
                    emit statusChanged(tr("O serviço de chat encerrou com erro (código %1).").arg(exitCode));
                }
            });

    const QString scriptPath = QDir(serverDir_).absoluteFilePath("server/index.js");
    blog(LOG_INFO, "[streamhub] iniciando Node: diretório=%s; script=%s",
         serverDir_.toUtf8().constData(), scriptPath.toUtf8().constData());
    serverProcess->start(nodePath, {scriptPath});
    emit statusChanged(tr("StreamHub rodando (porta %1).").arg(port_));
}

void StreamHubLauncher::Stop()
{
    stopping_ = true;
    restartPending_ = false;
    if (npmInstallProcess_ != nullptr) {
        npmInstallProcess_->kill();
        npmInstallProcess_->deleteLater();
        npmInstallProcess_ = nullptr;
    }

    if (process_ == nullptr) {
        return;
    }

    process_->terminate();
    if (!process_->waitForFinished(3000)) {
        process_->kill();
    }

    disconnect(process_, nullptr, this, nullptr);
    process_->deleteLater();
    process_ = nullptr;
}

bool StreamHubLauncher::IsRunning() const
{
    return process_ != nullptr && process_->state() != QProcess::NotRunning;
}
