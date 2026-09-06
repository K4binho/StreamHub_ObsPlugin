#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class StreamHubNodeProvision;

// Sobe o servidor Node.js (server/index.js do projeto StreamHub) como
// processo filho assim que o OBS carrega o plugin, e derruba ele quando o
// OBS fecha. Isso é o que faz "instalar o plugin já configura tudo" —
// o usuário não precisa abrir terminal nenhum, instalar Node.js, nem
// esperar o OBS travar na primeira abertura.
//
// Todo o trabalho pesado (resolver/baixar Node.js, rodar npm install) é
// assíncrono — Start() retorna na hora e o OBS nunca fica travado, nem na
// primeira execução. Use statusChanged() pra mostrar progresso numa dock.
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

    int Port() const { return port_; }
    bool IsRunning() const;

signals:
    // Emitido em cada etapa (baixando Node, instalando deps, iniciando
    // servidor...) pra quem quiser mostrar isso numa dock/label.
    void statusChanged(const QString &status);

private:
    void OnNodeReady(const QString &nodePath, const QString &npmCliPath);
    void RunNpmInstallThenStart(const QString &nodePath, const QString &npmCliPath);
    void StartServerProcess(const QString &nodePath);
    bool DependenciesAreComplete() const;
    bool SaveDependencyMarker() const;

    StreamHubNodeProvision *provisioner_ = nullptr;
    QProcess *npmInstallProcess_ = nullptr;
    QProcess *process_ = nullptr;

    QString serverDir_;
    QString nodePath_;
    int port_ = 3000;
    bool restartPending_ = false;
    bool stopping_ = false;
};
