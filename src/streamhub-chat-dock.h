#pragma once

#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

// Dock nativo (Qt puro, sem CEF) que consulta o endpoint HTTP
// /api/chat/poll do servidor StreamHub e mostra as mensagens do chat
// unificado dentro do próprio OBS, como um painel — igual às outras docks
// do OBS.
//
// Usa long-polling via QNetworkAccessManager (módulo Qt6::Network, já
// presente no pacote de dependências do OBS) em vez de QWebSocket
// (módulo Qt6::WebSockets, que precisava ser compilado manualmente à
// parte). O servidor segura cada requisição até ~25s ou até ter mensagem
// nova pra mandar, então a latência percebida é praticamente a mesma de
// um WebSocket de verdade.
class StreamHubChatDock : public QDockWidget {
    Q_OBJECT

public:
    explicit StreamHubChatDock(QWidget *parent = nullptr);

    void ConnectTo(int port);

public slots:
    // Mostra o progresso de preparação (baixando Node, instalando deps,
    // etc.) no topo da dock, enquanto o servidor ainda não respondeu à
    // primeira consulta. Some sozinho assim que a primeira resposta chega.
    void SetStatus(const QString &status);

private slots:
    void PollOnce();
    void OnPollFinished(QNetworkReply *reply);

private:
    void AppendChatLine(const QString &platform, const QString &user, const QString &text);

    QLabel *statusLabel_ = nullptr;
    QListWidget *list_ = nullptr;
    QNetworkAccessManager *net_ = nullptr;
    QTimer *retryTimer_ = nullptr;

    int port_ = 3000;
    qint64 since_ = 0;
    bool connected_ = false;
    bool pollInFlight_ = false;

    static constexpr int kMaxItems = 200;
};
