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

## Correções aplicadas nesta etapa

O primeiro corte foi feito no servidor local, que concentra credenciais e integrações externas. O comportamento público não foi alterado.

### Configuração de plataformas

Arquivo: `apps/local-server/server/accounts/platform-config.js`

Responsabilidade única:

- URLs dos provedores;
- escopos OAuth;
- IDs públicos;
- URLs de callback;
- URL do broker compartilhado.

`accounts.js` deixou de ser dono da configuração fixa das plataformas e agora apenas consome essa interface.

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

Os arquivos C++ grandes devem ser divididos em uma etapa separada, com testes de compilação e abertura do OBS após cada corte:

1. `streamhub-control-dock.cpp`
   - `PlatformAccountService`
   - `TransmissionSyncService`
   - `CategoryCoverService`
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

- Testes do servidor local: 2 aprovados.
- Testes do broker OAuth: 3 aprovados.
- `node --check` deve ser executado para os módulos alterados.
- Nenhuma alteração foi feita no contrato do plugin OBS ou na DLL nesta etapa.

## Próxima etapa recomendada

Dividir `streamhub-control-dock.cpp` por responsabilidade, começando pela extração do serviço de sincronização de transmissão. Esse é o maior ponto de acoplamento da interface e oferece o melhor retorno sem mexer primeiro no ciclo de vida do plugin.
