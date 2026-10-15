#include "streamhub-launcher.h"
#include "streamhub-node-provision.h"
#include "streamhub-paths.h"

#include <cerrno>
#include <limits>
#include <memory>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>
#include <QTcpSocket>
#include <QHostAddress>

#ifndef _WIN32
#include <signal.h>
#endif

#ifdef _WIN32
#include <Windows.h>
#endif

#include "obs.h"

#include "plugin-support.h"

namespace {

bool ValidPid(qint64 pid)
{
    return pid > 0 && pid <= (std::numeric_limits<qint64>::max)();
}

bool ProcessExists(qint64 pid)
{
    if (!ValidPid(pid))
        return false;

#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE, static_cast<DWORD>(pid));
    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;

    DWORD exitCode = 0;
    const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return running;
#else
    if (kill(static_cast<pid_t>(pid), 0) == 0)
        return true;
    return errno == EPERM;
#endif
}

qint64 CurrentProcessId()
{
    return static_cast<qint64>(QCoreApplication::applicationPid());
}

} // namespace

bool StreamHubReadRuntime(const QString &path, StreamHubRuntime *runtime)
{
    if (!runtime || path.isEmpty())
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject object = document.object();
    const qint64 nodePid = object.value("nodePid").toVariant().toLongLong();
    const qint64 obsPid = object.value("obsPid").toVariant().toLongLong();
    const int port = object.value("port").toInt();
    const QString token = object.value("token").toString();
    if (!ValidPid(nodePid) || !ValidPid(obsPid) || port < 1 || port > 65535 || token.size() < 32)
        return false;

    runtime->nodePid = nodePid;
    runtime->obsPid = obsPid;
    runtime->port = port;
    runtime->token = token;
    return true;
}

bool StreamHubWriteRuntime(const QString &path, const StreamHubRuntime &runtime)
{
    if (path.isEmpty() || !ValidPid(runtime.nodePid) || !ValidPid(runtime.obsPid) ||
        runtime.port < 1 || runtime.port > 65535 || runtime.token.size() < 32) {
        return false;
    }

    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    const QJsonObject object{{"nodePid", runtime.nodePid},
                             {"obsPid", runtime.obsPid},
                             {"port", runtime.port},
                             {"token", runtime.token}};
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) < 0 ||
        !file.commit()) {
        return false;
    }

    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void StreamHubRemoveRuntime(const QString &path)
{
    if (!path.isEmpty())
        QFile::remove(path);
}

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
    if (process_ != nullptr || npmInstallProcess_ != nullptr)
        return;

    const QString writableDataPath = StreamHubWritableDataPath();
    serverDir_ = StreamHubServerPath();
    if (serverDir_.isEmpty())
        serverDir_ = StreamHubAbsolutePath(serverDir);

    runtimePath_ = StreamHubRuntimePath();
    if (runtimePath_.isEmpty())
        runtimePath_ = StreamHubModuleConfigPath("runtime.json");
    if (runtimePath_.isEmpty() && !writableDataPath.isEmpty())
        runtimePath_ = QDir(writableDataPath).filePath("runtime.json");

    port_ = port;
    stopping_ = false;
    restartPending_ = false;

    const QString dataPath = writableDataPath.isEmpty()
                                 ? StreamHubAbsolutePath(pluginDataDir)
                                 : writableDataPath;
    pendingNodePath_.clear();
    pendingNpmCliPath_.clear();
    previousInstancePreparationPending_ = true;
    PreparePreviousInstanceAsync();

    emit statusChanged(tr("Preparando StreamHub..."));
    provisioner_->Ensure(dataPath);
}

void StreamHubLauncher::OnNodeReady(const QString &nodePath, const QString &npmCliPath)
{
    nodePath_ = nodePath;
    if (previousInstancePreparationPending_) {
        pendingNodePath_ = nodePath;
        pendingNpmCliPath_ = npmCliPath;
        return;
    }

    if (stopping_)
        return;

    QDir nodeModules(QDir(serverDir_).filePath("node_modules"));
    if (nodeModules.exists() && DependenciesAreComplete()) {
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
        QProcess *process = process_;
        RequestInternalJsonAsync(QStringLiteral("/internal/shutdown"), instanceToken_, process->processId(),
                                 CurrentProcessId(), port_, {}, [this, process](bool, const QJsonObject &) {
            if (process_ == process && process->state() != QProcess::NotRunning)
                process->terminate();
        });
        QTimer::singleShot(750, this, [this, process]() {
            if (process_ == process && process->state() != QProcess::NotRunning)
                process->kill();
        });
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
        npmInstallProcess_->start(nodePath, {npmCliPath, "install", "--omit=dev"});
    } else {
#if defined(_WIN32)
        npmInstallProcess_->start("cmd", {"/c", "npm", "install", "--omit=dev"});
#else
        npmInstallProcess_->start("npm", {"install", "--omit=dev"});
#endif
    }

    connect(npmInstallProcess_, &QProcess::readyReadStandardError, this, [this]() {
        if (npmInstallProcess_)
            blog(LOG_INFO, "[streamhub-npm] %s", npmInstallProcess_->readAllStandardError().constData());
    });

    connect(npmInstallProcess_, &QProcess::finished, this,
            [this, nodePath](int exitCode, QProcess::ExitStatus) {
                if (!npmInstallProcess_)
                    return;
                npmInstallProcess_->deleteLater();
                npmInstallProcess_ = nullptr;

                if (stopping_)
                    return;
                if (exitCode != 0) {
                    emit statusChanged(tr("npm install falhou (código %1) — confira o log do OBS.").arg(exitCode));
                    blog(LOG_WARNING, "[streamhub] npm install terminou com código %d", exitCode);
                    return;
                }

                if (!SaveDependencyMarker())
                    blog(LOG_WARNING, "[streamhub] dependências instaladas, mas não consegui salvar o marcador");
                emit statusChanged(tr("Dependências instaladas — iniciando servidor..."));
                StartServerProcess(nodePath);
            });

    connect(npmInstallProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        blog(LOG_WARNING, "[streamhub] não consegui rodar o npm install (código %d)", (int)err);
        emit statusChanged(tr("Não consegui rodar o npm install — confira se o Node.js está OK."));
    });
}

QString StreamHubLauncher::NewInstanceToken() const
{
    QByteArray token;
    token.reserve(64);
    for (int i = 0; i < 4; ++i) {
        token += QByteArray::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, '0');
    }
    return QString::fromLatin1(token);
}

bool StreamHubLauncher::WriteRuntime(qint64 nodePid) const
{
    return StreamHubWriteRuntime(runtimePath_, {nodePid, CurrentProcessId(), port_, instanceToken_});
}

void StreamHubLauncher::RemoveOwnedRuntime() const
{
    StreamHubRuntime runtime;
    if (!StreamHubReadRuntime(runtimePath_, &runtime))
        return;
    if (runtime.token == instanceToken_)
        StreamHubRemoveRuntime(runtimePath_);
}

void StreamHubLauncher::RequestInternalJsonAsync(
    const QString &path, const QString &token, qint64 expectedPid, qint64 expectedObsPid,
    int port, const QJsonObject &body,
    std::function<void(bool, const QJsonObject &)> callback, int timeoutMs)
{
    if (!ValidPid(expectedPid) || !ValidPid(expectedObsPid) || token.size() < 32 ||
        port < 1 || port > 65535 || !path.startsWith("/internal/")) {
        callback(false, {{QStringLiteral("error"), QStringLiteral("invalid internal request")}});
        return;
    }

    auto *socket = new QTcpSocket(this);
    auto response = std::make_shared<QByteArray>();
    auto completed = std::make_shared<bool>(false);
    const auto finish = [socket, completed, callback](bool ok, const QJsonObject &object) {
        if (*completed)
            return;
        *completed = true;
        socket->deleteLater();
        callback(ok, object);
    };

    connect(socket, &QTcpSocket::connected, this,
            [socket, path, token, body]() {
        const QByteArray payload = body.isEmpty()
                                       ? QByteArray{}
                                       : QJsonDocument(body).toJson(QJsonDocument::Compact);
        const QByteArray method = path.endsWith(QStringLiteral("/status")) ? "GET" : "POST";
        const QByteArray request = method + ' ' + path.toUtf8() + " HTTP/1.1\r\n"
                                   "Host: localhost\r\n"
                                   "X-StreamHub-Token: " + token.toUtf8() + "\r\n"
                                   "Content-Type: application/json\r\n"
                                   "Connection: close\r\n"
                                   "Content-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload;
        socket->write(request);
    });

    const auto consumeResponse = [response, expectedPid, expectedObsPid, finish]() {
        const qsizetype headerEnd = response->indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray headers = response->left(headerEnd).toLower();
        const qsizetype statusEnd = headers.indexOf("\r\n");
        const QByteArray statusLine = statusEnd < 0 ? headers : headers.left(statusEnd);
        const QByteArray lengthHeader = "\r\ncontent-length:";
        const qsizetype lengthStart = headers.indexOf(lengthHeader);
        if (lengthStart < 0)
            return;
        const qsizetype valueStart = lengthStart + lengthHeader.size();
        const qsizetype valueEnd = headers.indexOf("\r\n", valueStart);
        bool lengthOk = false;
        const qint64 contentLength = headers.mid(valueStart, valueEnd < 0 ? -1 : valueEnd - valueStart)
                                         .trimmed()
                                         .toLongLong(&lengthOk);
        if (!lengthOk || contentLength < 0 || contentLength > 64 * 1024) {
            finish(false, {{QStringLiteral("error"), QStringLiteral("invalid response length")}});
            return;
        }
        if (response->size() < headerEnd + 4 + contentLength)
            return;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
            response->mid(headerEnd + 4, contentLength), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            finish(false, {{QStringLiteral("error"), QStringLiteral("invalid response JSON")}});
            return;
        }
        const QJsonObject object = document.object();
        const bool statusOk = statusLine.contains(" 200 ");
        const bool pidsMatch = object.value("pid").toVariant().toLongLong() == expectedPid &&
                               object.value("obsPid").toVariant().toLongLong() == expectedObsPid;
        if (!statusOk || !pidsMatch) {
            finish(false, object);
            return;
        }
        finish(true, object);
    };

    connect(socket, &QTcpSocket::readyRead, this, [socket, response, consumeResponse]() {
        *response += socket->readAll();
        consumeResponse();
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [finish](QAbstractSocket::SocketError) {
        finish(false, {{QStringLiteral("error"), QStringLiteral("connection failed")}});
    });
    connect(socket, &QTcpSocket::disconnected, this, [consumeResponse, finish]() {
        consumeResponse();
        finish(false, {{QStringLiteral("error"), QStringLiteral("connection closed before response")}});
    });
    QTimer::singleShot(timeoutMs, socket, [finish]() {
        finish(false, {{QStringLiteral("error"), QStringLiteral("local request timed out")}});
    });
    socket->connectToHost(QHostAddress(QHostAddress::LocalHost), static_cast<quint16>(port));
}

void StreamHubLauncher::PreparePreviousInstanceAsync()
{
    StreamHubRuntime previous;
    if (!StreamHubReadRuntime(runtimePath_, &previous)) {
        if (QFileInfo::exists(runtimePath_))
            StreamHubRemoveRuntime(runtimePath_);
        FinishPreviousInstancePreparation(true);
        return;
    }

    if (previous.nodePid == CurrentProcessId() || !ProcessExists(previous.nodePid)) {
        StreamHubRemoveRuntime(runtimePath_);
        FinishPreviousInstancePreparation(true);
        return;
    }

    previousRuntime_ = previous;
    previousInstanceShutdownPolls_ = 0;
    RequestInternalJsonAsync(QStringLiteral("/internal/status"), previous.token,
                             previous.nodePid, previous.obsPid, previous.port, {},
                             [this, previous](bool ok, const QJsonObject &) {
        if (!previousInstancePreparationPending_)
            return;
        if (!ok) {
            blog(LOG_WARNING, "[streamhub] instância anterior sem handshake; preservando o processo");
            FinishPreviousInstancePreparation(true);
            return;
        }
        RequestInternalJsonAsync(QStringLiteral("/internal/shutdown"), previous.token,
                                 previous.nodePid, previous.obsPid, previous.port, {},
                                 [this, previous](bool shutdownOk, const QJsonObject &) {
            if (!previousInstancePreparationPending_)
                return;
            if (!shutdownOk) {
                blog(LOG_WARNING, "[streamhub] shutdown autenticado da instância anterior não confirmou");
                FinishPreviousInstancePreparation(true);
                return;
            }

            previousRuntime_ = previous;
            QTimer::singleShot(100, this, &StreamHubLauncher::PollPreviousInstanceShutdown);
        });
    });
}

void StreamHubLauncher::PollPreviousInstanceShutdown()
{
    if (!previousInstancePreparationPending_)
        return;
    if (!ProcessExists(previousRuntime_.nodePid)) {
        StreamHubRuntime runtime;
        if (StreamHubReadRuntime(runtimePath_, &runtime) && runtime.token == previousRuntime_.token)
            StreamHubRemoveRuntime(runtimePath_);
        FinishPreviousInstancePreparation(true);
        return;
    }
    if (++previousInstanceShutdownPolls_ >= 10) {
        blog(LOG_WARNING, "[streamhub] instância anterior ainda ativa; continuando sem bloquear o OBS");
        FinishPreviousInstancePreparation(true);
        return;
    }
    QTimer::singleShot(100, this, &StreamHubLauncher::PollPreviousInstanceShutdown);
}

void StreamHubLauncher::FinishPreviousInstancePreparation(bool allowStart)
{
    if (!previousInstancePreparationPending_)
        return;

    previousInstancePreparationPending_ = false;
    if (!allowStart) {
        blog(LOG_WARNING, "[streamhub] inicialização pausada por estado anterior não validado");
        return;
    }

    if (pendingNodePath_.isEmpty() || stopping_)
        return;

    const QString nodePath = pendingNodePath_;
    const QString npmCliPath = pendingNpmCliPath_;
    pendingNodePath_.clear();
    pendingNpmCliPath_.clear();
    OnNodeReady(nodePath, npmCliPath);
}

void StreamHubLauncher::AttachJobObject(QProcess *process)
{
#ifdef _WIN32
    if (!process || !process->processId() || jobHandle_)
        return;

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        blog(LOG_WARNING, "[streamhub] não consegui criar Job Object para Node (erro %lu)", GetLastError());
        return;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        blog(LOG_WARNING, "[streamhub] não consegui configurar Job Object para Node (erro %lu)", GetLastError());
        CloseHandle(job);
        return;
    }

    HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | SYNCHRONIZE,
                               FALSE, static_cast<DWORD>(process->processId()));
    if (!child || !AssignProcessToJobObject(job, child)) {
        blog(LOG_WARNING, "[streamhub] não consegui associar Node ao Job Object (erro %lu)", GetLastError());
        if (child)
            CloseHandle(child);
        CloseHandle(job);
        return;
    }

    CloseHandle(child);
    jobHandle_ = job;
#else
    Q_UNUSED(process);
#endif
}

void StreamHubLauncher::CloseJobObject()
{
#ifdef _WIN32
    if (jobHandle_) {
        CloseHandle(static_cast<HANDLE>(jobHandle_));
        jobHandle_ = nullptr;
    }
#endif
}

void StreamHubLauncher::StartServerProcess(const QString &nodePath)
{
    if (process_ && process_->state() != QProcess::NotRunning)
        return;

    instanceToken_ = NewInstanceToken();
    auto *serverProcess = new QProcess(this);
    process_ = serverProcess;
    serverProcess->setWorkingDirectory(serverDir_);

    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("PORT", QString::number(port_));
    env.insert("STREAMHUB_OBS_PID", QString::number(CurrentProcessId()));
    env.insert("STREAMHUB_INSTANCE_TOKEN", instanceToken_);
    serverProcess->setProcessEnvironment(env);

    connect(serverProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        blog(LOG_WARNING, "[streamhub] não consegui iniciar o servidor Node (código %d).", (int)err);
        emit statusChanged(tr("Não consegui iniciar o servidor do StreamHub."));
    });

    connect(serverProcess, &QProcess::started, this, [this, serverProcess]() {
        if (process_ != serverProcess || stopping_)
            return;
        AttachJobObject(serverProcess);
        if (!WriteRuntime(serverProcess->processId()))
            blog(LOG_WARNING, "[streamhub] não consegui gravar runtime.json");
        emit statusChanged(tr("StreamHub rodando (porta %1).").arg(port_));
    });

    connect(serverProcess, &QProcess::readyReadStandardError, this, [serverProcess]() {
        blog(LOG_INFO, "[streamhub-node] %s", serverProcess->readAllStandardError().constData());
    });

    connect(serverProcess, &QProcess::finished, this,
            [this, serverProcess](int exitCode, QProcess::ExitStatus) {
                const bool wasCurrent = process_ == serverProcess;
                if (wasCurrent)
                    process_ = nullptr;
                RemoveOwnedRuntime();
                CloseJobObject();
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
}

void StreamHubLauncher::SyncTwitchTransmission()
{
    if (twitchSyncInFlight_ || !IsRunning()) {
        if (!IsRunning())
            emit twitchTransmissionFailed(tr("Serviço StreamHub ainda não está pronto."));
        return;
    }
    twitchSyncInFlight_ = true;
    const qint64 nodePid = process_->processId();
    RequestInternalJsonAsync(QStringLiteral("/internal/twitch-sync/nonce"), instanceToken_, nodePid,
                             CurrentProcessId(), port_, {},
                             [this, nodePid](bool nonceOk, const QJsonObject &nonceResponse) {
        const QString nonce = nonceResponse.value("nonce").toString();
        if (!nonceOk || nonce.isEmpty()) {
            twitchSyncInFlight_ = false;
            emit twitchTransmissionFailed(tr("Não foi possível iniciar sincronização Twitch."));
            return;
        }
        RequestInternalJsonAsync(QStringLiteral("/internal/twitch-sync"), instanceToken_, nodePid,
                                 CurrentProcessId(), port_, {{"nonce", nonce}},
                                 [this](bool ok, const QJsonObject &response) {
            twitchSyncInFlight_ = false;
            if (!ok) {
                const QString error = response.value("error").toString().trimmed();
                emit twitchTransmissionFailed(error.isEmpty()
                                                  ? tr("Não foi possível obter dados oficiais da Twitch.")
                                                  : error);
                return;
            }
            if (response.value("platform").toString().compare("twitch", Qt::CaseInsensitive) != 0 ||
                response.value("server").toString().trimmed().isEmpty() ||
                response.value("streamKey").toString().trimmed().isEmpty()) {
                emit twitchTransmissionFailed(tr("Twitch não forneceu servidor RTMP e stream key válidos."));
                return;
            }
            emit twitchTransmissionReady(response);
        }, 15000);
    });
}

void StreamHubLauncher::SyncKickTransmission()
{
    if (kickSyncInFlight_ || !IsRunning()) {
        if (!IsRunning())
            emit kickTransmissionFailed(tr("Serviço StreamHub ainda não está pronto."));
        return;
    }
    kickSyncInFlight_ = true;
    const qint64 nodePid = process_->processId();
    RequestInternalJsonAsync(QStringLiteral("/internal/kick-sync/nonce"), instanceToken_, nodePid,
                             CurrentProcessId(), port_, {},
                             [this, nodePid](bool nonceOk, const QJsonObject &nonceResponse) {
        const QString nonce = nonceResponse.value("nonce").toString();
        if (!nonceOk || nonce.isEmpty()) {
            kickSyncInFlight_ = false;
            emit kickTransmissionFailed(tr("Não foi possível iniciar sincronização Kick."));
            return;
        }
        RequestInternalJsonAsync(QStringLiteral("/internal/kick-sync"), instanceToken_, nodePid,
                                 CurrentProcessId(), port_, {{"nonce", nonce}},
                                 [this](bool ok, const QJsonObject &response) {
            kickSyncInFlight_ = false;
            if (!ok) {
                const QString error = response.value("error").toString().trimmed();
                emit kickTransmissionFailed(error.isEmpty()
                                                 ? tr("Não foi possível obter dados oficiais da Kick.")
                                                 : error);
                return;
            }
            if (response.value("platform").toString().compare("kick", Qt::CaseInsensitive) != 0 ||
                response.value("server").toString().trimmed().isEmpty() ||
                response.value("streamKey").toString().trimmed().isEmpty()) {
                emit kickTransmissionFailed(tr("Kick não forneceu servidor RTMP e stream key válidos."));
                return;
            }
            emit kickTransmissionReady(response);
        }, 15000);
    });
}

void StreamHubLauncher::SyncYoutubeTransmission()
{
    if (youtubeSyncInFlight_ || !IsRunning()) {
        if (!IsRunning())
            emit youtubeTransmissionFailed(tr("Serviço StreamHub ainda não está pronto."));
        return;
    }
    youtubeSyncInFlight_ = true;
    const qint64 nodePid = process_->processId();
    RequestInternalJsonAsync(QStringLiteral("/internal/youtube-sync/nonce"), instanceToken_, nodePid,
                             CurrentProcessId(), port_, {},
                             [this, nodePid](bool nonceOk, const QJsonObject &nonceResponse) {
        const QString nonce = nonceResponse.value("nonce").toString();
        if (!nonceOk || nonce.isEmpty()) {
            youtubeSyncInFlight_ = false;
            emit youtubeTransmissionFailed(tr("Não foi possível iniciar sincronização YouTube."));
            return;
        }
        RequestInternalJsonAsync(QStringLiteral("/internal/youtube-sync"), instanceToken_, nodePid,
                                 CurrentProcessId(), port_, {{"nonce", nonce}},
                                 [this](bool ok, const QJsonObject &response) {
            youtubeSyncInFlight_ = false;
            if (!ok) {
                const QString error = response.value("error").toString().trimmed();
                emit youtubeTransmissionFailed(error.isEmpty()
                                                   ? tr("Não foi possível obter dados oficiais do YouTube.")
                                                   : error);
                return;
            }
            if (response.value("platform").toString().compare("youtube", Qt::CaseInsensitive) != 0 ||
                response.value("server").toString().trimmed().isEmpty() ||
                response.value("streamKey").toString().trimmed().isEmpty()) {
                emit youtubeTransmissionFailed(tr("YouTube não forneceu servidor RTMP e stream key válidos."));
                return;
            }
            emit youtubeTransmissionReady(response);
        }, 15000);
    });
}

void StreamHubLauncher::Stop()
{
    stopping_ = true;
    restartPending_ = false;
    previousInstancePreparationPending_ = false;
    pendingNodePath_.clear();
    pendingNpmCliPath_.clear();

    if (npmInstallProcess_ != nullptr) {
        npmInstallProcess_->kill();
        npmInstallProcess_->deleteLater();
        npmInstallProcess_ = nullptr;
    }

    QProcess *process = process_;
    if (process == nullptr) {
        RemoveOwnedRuntime();
        CloseJobObject();
        return;
    }
    if (stopPending_)
        return;
    stopPending_ = true;

    const qint64 pid = process->processId();
    RequestInternalJsonAsync(QStringLiteral("/internal/shutdown"), instanceToken_, pid,
                             CurrentProcessId(), port_, {},
                             [this, process](bool, const QJsonObject &) {
        if (process_ != process)
            return;
        if (process->state() != QProcess::NotRunning)
            process->terminate();
        QTimer::singleShot(750, this, [this, process]() {
            if (process_ != process)
                return;
            if (process->state() != QProcess::NotRunning)
                process->kill();
            QTimer::singleShot(250, this, [this, process]() {
                if (process_ != process)
                    return;
                RemoveOwnedRuntime();
                CloseJobObject();
                if (process->state() == QProcess::NotRunning) {
                    disconnect(process, nullptr, this, nullptr);
                    process->deleteLater();
                    process_ = nullptr;
                }
            });
        });
    });
}

bool StreamHubLauncher::IsRunning() const
{
    return process_ != nullptr && process_->state() != QProcess::NotRunning;
}
