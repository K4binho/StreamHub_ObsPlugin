# StreamHub — Modularização e responsabilidades

**Última atualização:** 18/10/2026

## Diagnóstico

O StreamHub possui uma boa separação de componentes no nível do projeto, mas alguns arquivos internos acumulavam responsabilidades demais. A avaliação foi feita pelos seguintes critérios:

1. Cada módulo deve ter um propósito claro.
2. Interfaces devem esconder detalhes internos.
3. Dependências entre módulos devem ser pequenas e explícitas.
4. Comportamentos repetidos devem ser centralizados.

Conclusão: a modularização estrutural estava boa, mas a modularização interna era parcial. O projeto tinha aproximadamente estes pontos de concentração:

- `plugins/obs/src/app/obs-multi-rtmp.cpp`: ciclo de vida, docks e composição da aplicação.
- `plugins/obs/src/broadcast/streamhub-control-dock.cpp`: UI, HTTP, OAuth, sincronização, categorias e capas.
- `plugins/obs/src/chat/streamhub-chat-dock.cpp`: rede, estado de conexão, filtros e renderização.
- `apps/local-server/server/accounts.js`: credenciais, OAuth, chamadas HTTP, persistência e regras das plataformas.

## Correções aplicadas

As extrações abaixo preservam os contratos públicos e reduzem responsabilidades concentradas nos módulos coordenadores.

### Configuração de plataformas

Arquivo: `apps/local-server/server/accounts/platform-config.js`

Responsabilidade única:

- URLs dos provedores;
- escopos OAuth;
- IDs públicos;
- URLs de callback;
- URL do broker compartilhado.

`accounts.js` deixou de ser dono da configuração fixa das plataformas e agora apenas consome essa interface.

### Construção do pedido de atualização da transmissão

Arquivos: `plugins/obs/src/broadcast/streamhub-broadcast-request.h/.cpp`

Responsabilidade única:

- transformar os campos editados no dock em payload JSON para a API local;
- normalizar a lista de tags;
- manter os metadados Twitch, Kick e YouTube no contrato esperado pelo servidor.

O construtor não depende de widgets, rede ou estado do OBS. `StreamHubControlDock` permanece responsável por ler a tela, validar a plataforma principal e enviar a requisição HTTP.

### Carregamento e cache de capas de categoria

Arquivos: `plugins/obs/src/broadcast/streamhub-category-cover-service.h/.cpp`

Responsabilidade única:

- buscar capas de categoria no servidor local;
- validar a resposta e decodificar a imagem;
- manter cache limitado a 100 capas e repetir até duas vezes em caso de falha.

A prévia da transmissão e a lista de resultados da busca usam o mesmo serviço. O serviço não conhece widgets; ele emite sinais com a capa carregada ou com a falha, e o dock decide onde exibi-la.

### Persistência de contas

Arquivo: `apps/local-server/server/accounts/account-store.js`

Responsabilidade única:

- ler `accounts-private.json`;
- manter o snapshot em memória;
- gravar de forma temporária e renomear atomicamente;
- manter a persistência de tokens fora da regra OAuth.

Segredos continuam fora do código e não são expostos em logs ou respostas.

### Cliente HTTP dos provedores

Arquivo: `apps/local-server/server/accounts/http-client.js`

Responsabilidade única:

- enviar formulários OAuth;
- fazer requisições JSON;
- padronizar timeout;
- transformar respostas de erro dos provedores em erros seguros.

As integrações Twitch, Kick e YouTube reutilizam o mesmo comportamento sem duplicar tratamento de erro.

### Resultado

`apps/local-server/server/accounts.js` continua sendo a fachada compatível usada pelas rotas, mas agora coordena serviços menores. A API existente foi preservada para evitar impacto no plugin OBS.

## O que ainda não foi dividido

Os arquivos C++ grandes ainda devem ser divididos em etapas pequenas, com compilação e abertura do OBS após cada corte:

1. `streamhub-control-dock.cpp`
   - `PlatformAccountService`
   - `TransmissionSyncService`
   - `TransmissionDockView`
2. `obs-multi-rtmp.cpp`
   - `PluginLifecycle`
   - `DockRegistration`
   - `RuntimeCoordinator`
3. `streamhub-chat-dock.cpp`
   - `ChatTransport`
   - `ChatMessageModel`
   - `ChatFilterBar`
   - `ChatDockView`
4. `apps/local-server/server/index.js`
   - `RuntimeLifecycle`
   - `ChatConnectorCoordinator`
   - `InternalSyncRoutes`
   - `EventHistory`

Essas divisões devem preservar a DLL única e os contratos existentes. Não devemos fazer uma grande movimentação sem validar o OBS, porque esse código participa diretamente da inicialização da DLL e já houve crashes causados por ordem incorreta de inicialização.

## Regras para as próximas mudanças

- A interface visual não chama diretamente APIs de plataforma quando puder usar um serviço intermediário.
- Serviços de plataforma não conhecem widgets Qt.
- Persistência não deve conhecer OAuth nem a UI.
- Modelos de domínio não devem depender de `QJsonObject` em toda a aplicação.
- O broker OAuth permanece separado do servidor local e do plugin.
- Cada nova integração deve possuir um adaptador de plataforma e testes próprios.
- Toda refatoração deve manter o contrato público e ser validada antes da instalação da DLL.

## Validação desta etapa

- Compilação CMake do alvo `obs-multi-rtmp` em `RelWithDebInfo`: aprovada após a extração do construtor do pedido e do serviço de capas.
- `git diff --check`: aprovado.
- A DLL foi compilada, mas não instalada no OBS nem submetida a teste de abertura nesta etapa.
- O contrato HTTP enviado ao servidor permaneceu com os mesmos campos e estrutura.

## Próxima etapa recomendada

Separar o serviço de consulta/sincronização das transmissões do `streamhub-control-dock.cpp`, mantendo o dock como coordenador visual. Esse passo precisa preservar o fluxo de OAuth e os sinais que entregam a configuração RTMP ao dock de múltiplas saídas.
