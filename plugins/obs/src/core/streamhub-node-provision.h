#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// Resolve um Node.js utilizável (sistema ou runtime portátil baixado sob
// demanda) sem NUNCA bloquear a thread principal do OBS.
//
// Ordem de resolução:
//   1. Runtime portátil já baixado antes (data/streamhub-server/../node-runtime)
//      -> usa direto, sem rede, sem esperar nada (caminho comum, rápido).
//   2. Node.js do sistema (PATH) -> usa direto.
//   3. Nenhum dos dois -> baixa o runtime portátil oficial (nodejs.org) pra
//      pasta de dados do plugin, extrai com a ferramenta nativa do SO
//      (Expand-Archive no Windows, tar no mac/Linux) e usa esse.
//
// Tudo acontece via QNetworkAccessManager/QProcess assíncronos; o resultado
// chega pelos sinais ready()/failed()/statusChanged().
class StreamHubNodeProvision : public QObject {
    Q_OBJECT

public:
    explicit StreamHubNodeProvision(QObject *parent = nullptr);

    // pluginDataDir = pasta de dados do plugin (obs_get_module_data_path).
    // O runtime baixado (se precisar) fica em pluginDataDir/node-runtime.
    void Ensure(const QString &pluginDataDir);

    // Caminho absoluto do binário node.exe/node já resolvido, uma vez que
    // ready() tenha sido emitido. Vazio antes disso.
    QString NodePath() const { return nodePath_; }

    // Caminho do npm-cli.js correspondente a NodePath(), se encontrado.
    // Pode ficar vazio (ver comentário em ResolveNpmCli) — nesse caso o
    // chamador deve cair pra invocar "npm" via linha de comando do SO.
    QString NpmCliPath() const { return npmCliPath_; }

signals:
    void statusChanged(const QString &status);
    void ready(const QString &nodePath, const QString &npmCliPath);
    void failed(const QString &error);

private:
    void TryBundledRuntime();
    void TrySystemNode();
    void DownloadPortableRuntime();
    void ExtractAndFinish(const QString &archivePath);
    void FinishWithNode(const QString &nodeExePath);
    void SaveMarker() const;
    QString ResolveNpmCli(const QString &nodeExePath) const;

    // Monta a URL oficial de download do Node.js portátil pro SO/arquitetura
    // atual. Retorna string vazia se a plataforma não for suportada
    // automaticamente (aí falha com uma mensagem clara pro usuário).
    static QString PlatformDownloadUrl();
    static QString PlatformArchiveExtension(); // ".zip" ou ".tar.gz"

    QNetworkAccessManager *net_ = nullptr;
    QNetworkReply *activeDownload_ = nullptr;
    QProcess *extractProcess_ = nullptr;

    QString pluginDataDir_;
    QString nodePath_;
    QString npmCliPath_;

    static constexpr const char *kNodeVersion = "20.11.1"; // LTS "Iron"
};
