# StreamHub OBS Plugin — Arquitetura, Contexto e Plano de Ação

**Última atualização:** 10/09/2026

## 1. Escopo

StreamHub é plugin C++/Qt para OBS Studio, baseado em `obs-multi-rtmp`. Plugin mantém saídas RTMP nativas e adiciona chat unificado, moderação, recompensas, contas OAuth, editor de transmissão, overlay e tema K4binho.

Componentes principais:

- Dock **Múltiplas saídas · K4**: transmissão principal do OBS como primeiro cartão fixo e destinos RTMP adicionais.
- Dock **StreamHub Chat · K4**: leitura, filtros, envio, overlay e botão ADM.
- Dock **Informações de transmissão K4**: contas e edição dos dados da live.
- Servidor Node.js embutido e extraído pela DLL.
- Recursos Qt embutidos: servidor, locale, ícones, branding, tema e overlay.

## 2. Estado atual

### Implementado e validado

- Build CMake com `AUTOMOC`, `AUTOUIC` e `AUTORCC`.
- Qt usa `Qt6::Core`, `Qt6::Widgets` e `Qt6::Network`; chat usa long-poll HTTP, não WebSocket.
- `StreamHub_EnsureBundledData()` extrai recursos embutidos antes do primeiro uso de locale.
- Extração preserva `streamhub-server/config.json` e `node_modules` do usuário.
- Launcher Node assíncrono, provisionamento de runtime, validação de dependências e reparação de `node_modules` por hash de `package.json`.
- Caminhos convertidos para absolutos antes de mudar diretório do processo filho.
- Servidor usa `localhost` como URL pública padrão, porta `605`, e mantém `server.port` explícita quando configurada.
- Lifecycle local autenticado implementado: `runtime.json`, PID do Node, PID do OBS, token aleatório por instância, `GET /internal/status` e `POST /internal/shutdown`.
- Shutdown Node idempotente trata `SIGTERM` e `SIGINT`, watchdog verifica o PID do OBS a cada 2 segundos, e launcher tenta shutdown gracioso antes de `terminate()` e `kill()`.
- Job Object Windows com `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` implementado no launcher.
- Conector YouTube descobre `activeLiveChatId` por `videos.list` e lê mensagens por `liveChatMessages.list`; não usa mais a combinação incompatível `mine` e `broadcastStatus`.
- Twitch OAuth, leitura, envio, dados da live, moderação e recompensas implementados.
- OAuth YouTube foi validado anteriormente no navegador externo, com retorno **YouTube conectado ao StreamHub**.
- Chat Twitch e Kick validado anteriormente no dock.
- Mensagem enviada em **Todos** aparece uma vez; ecos são deduplicados.
- Configuração do chat usa formulário Qt, gravação atômica e preservação de campos JSON desconhecidos.
- Tema **K4binho — Má Fase**, ícones, branding e overlay embutidos.
- Relay/FFmpeg não faz parte da inicialização atual.

### Pendente ou não validado

- Leitura do chat YouTube, consulta de transmissão e atualização de live ainda não foram validadas em live real. O conector atual descobre `activeLiveChatId` por `videos.list` e lê mensagens por `liveChatMessages.list`; rotas de contas e edição ainda precisam de validação ponta a ponta.
- Resultado Twitch ainda informa notificação como limitação da API; esse campo não é enviado à Twitch.
- Transmissão RTMP real ainda não foi validada de ponta a ponta.
- Kick envio autenticado, moderação e chat OAuth permanecem pendentes. Chat atual continua experimental.
- TikTok continua experimental. Facebook possui modelo de destino RTMP, sem conector de chat funcional.
- Fluxo normal Kick/YouTube usa broker OAuth HTTPS compartilhado: usuário abre página oficial, autoriza e volta ao OBS. Broker mantém Client Secrets fora da DLL; Node local recebe tokens somente por transação curta e claim token. Fallback avançado mantém credenciais manual ou `.env`.
- UI Kick, YouTube e Twitch em **Informações de transmissão K4** atualiza estado e inicia OAuth pelo broker. **Sincronizar** continua ação manual separada. Estado OAuth **Conectada** fica separado de erro ou ausência de dados RTMP; aviso de sincronização usa mensagem auxiliar do cartão.
- Sincronização cria um único destino por plataforma ausente ou atualiza somente `serviceParam.server` e `serviceParam.key`, preservando demais configurações.
- Twitch usa escopo `channel:read:stream_key`, endpoint `/helix/streams/key` e servidor fixo `rtmps://live.twitch.tv/app`. Sync interno respondeu `HTTP 200` com plataforma, servidor, chave e PIDs válidos; teste pelo botão e transmissão RTMP real ainda pendentes.
- Kick usa escopo `streamkey:read`; API respondeu servidor RTMP e stream key em `channel.stream.url` e `channel.stream.key`. Sync interno respondeu `HTTP 200`; transmissão RTMP real ainda pendente.
- YouTube respondeu `HTTP 200` em teste interno com servidor e stream key presentes; transmissão RTMP real ainda pendente.
- Credenciais Kick e YouTube aceitam `.env` como fallback independente; valores manuais preenchidos têm prioridade e campos vazios removem configuração manual.
- Watchdog e Job Object estão implementados no código, mas teste de encerramento anormal do OBS, PID reutilizado e garantia de limpeza por Job Object ainda estão pendentes.

Não marcar item pendente como implementado sem teste correspondente.

## 3. Arquitetura

### 3.1 Servidor Node

Diretório-fonte: `data/streamhub-server/`.

- `server/index.js`: entrypoint, servidor HTTP local em `localhost`, histórico, long-poll, eventos de status, watchdog e lifecycle autenticado.
- `server/accounts.js`: OAuth, renovação e persistência privada de tokens por plataforma.
- `server/routes/api.js`: endpoints locais para contas, transmissão, chat, moderação e recompensas.
- `server/chat/*.js`: conectores Twitch, Kick, YouTube e TikTok.
- `server/config-store.js`: leitura e gravação da configuração pública.
- `public/overlay.html` e arquivos associados: overlay transparente do chat.

Porta padrão: `605`. `PORT` e `config.server.port` explícitos continuam respeitados.

`GET /internal/status` e `POST /internal/shutdown` aceitam somente loopback e header `X-StreamHub-Token`. Endpoints `POST /internal/kick-sync/nonce`, `POST /internal/kick-sync`, `POST /internal/youtube-sync/nonce` e `POST /internal/youtube-sync` emitem/consomem nonce de uso único e retornam dados de transmissão somente no canal interno autenticado. `runtime.json` mantém PID do Node, PID do OBS, porta e token da instância para validação do launcher.

Node é autoridade para APIs de plataformas, OAuth e credenciais. Falha de um conector não pode derrubar servidor, outros conectores ou saídas nativas.

Servidor escuta somente localmente. Chaves RTMP não pertencem ao fluxo comum de chat, long-poll, Socket.IO, URL, log ou evento público.

### 3.2 Plugin C++/Qt

- `src/obs-multi-rtmp.cpp`: dock de múltiplas saídas, cartão principal e `obs_module_load()`.
- `src/push-widget.cpp`: cartão, estado, início e parada de cada destino.
- `src/output-config.h/.cpp`: `GlobalMultiOutputConfig()`, persistência de `targets`, encoders, vídeo e áudio.
- `src/streamhub-chat-dock.cpp/.h`: polling HTTP, filtros, deduplicação, envio e renderização.
- `src/streamhub-chat-settings.cpp`: formulário e gravação atômica de `config.json`.
- `src/streamhub-chat-admin.cpp/.h`: moderação, configurações administrativas, recompensas e resgates.
- `src/streamhub-control-dock.cpp/.h`: contas, OAuth, editor da live, prévia e busca de categoria.
- `src/streamhub-launcher.cpp`: ciclo de vida Node, runtime, `npm install`, processo filho e reinício.
- `src/streamhub-node-provision.cpp`: resolução/download do Node portátil e reparação de dependências.
- `src/streamhub-paths.h`: resolução de caminhos graváveis, servidor e configuração.
- `src/streamhub-bundle.cpp`: extração versionada do Qt Resource System.
- `src/streamhub-platforms.cpp`: presets, ícones, cores e servidores conhecidos.
- `qrc/streamhub-data.qrc`: arquivos embutidos na DLL.

C++ é autoridade para saídas RTMP, `GlobalMultiOutputConfig()` e `obs-multi-rtmp.json` no perfil ativo do OBS.

### 3.3 Bundle e distribuição

`qrc/streamhub-data.qrc` embute recursos necessários para DLL funcionar sem `data/` copiado manualmente. `StreamHub_EnsureBundledData()` extrai para caminho gravável retornado pelo OBS.

`kBundleVersion` em `src/streamhub-bundle.cpp` controla reextração. Ao alterar qualquer arquivo listado no QRC, aumentar `kBundleVersion`. Não confundir versão do bundle com `PLUGIN_VERSION`.

Extração nunca deve sobrescrever:

- `streamhub-server/config.json`;
- `accounts-private.json`;
- `node_modules`;
- qualquer credencial ou configuração criada pelo usuário.

Locale usado por `obs_module_text()` precisa existir na pasta extraída. Extração precisa ocorrer antes do primeiro uso de locale.

## 4. Regras estritas

### 4.1 Caminhos

Nunca hardcodear caminhos de máquina como `C:\...` ou `E:\...`.

Usar:

- `StreamHubWritableDataPath()` para dados graváveis do plugin;
- `StreamHubServerPath()` para servidor Node;
- `obs_module_config_path()` via `StreamHubModuleConfigPath()` para configuração do módulo;
- caminhos absolutos antes de iniciar `QProcess` ou trocar diretório de trabalho.

Código precisa funcionar em OBS instalado e OBS portátil.

### 4.2 Configuração

- `config.example.json` define defaults.
- `config.json` é configuração pública local do usuário.
- `accounts-private.json` guarda tokens e deve permanecer privado.
- Configuração antiga precisa continuar carregando com defaults seguros para campos ausentes.
- Formulários devem preservar campos JSON desconhecidos.
- Salvamento deve ser atômico e falha não pode destruir configuração existente.
- Saídas nativas devem preservar ordem, nome, `videoConfig`, `audioConfig`, `outputParam`, `syncStart`, `syncStop`, encoders e opções avançadas.

### 4.3 Segredos

Nunca registrar, commitar, enviar ou inserir em documentação:

- Client Secret;
- access token;
- refresh token;
- stream key;
- URL de callback contendo `code=`;
- credenciais em screenshots, eventos comuns ou respostas públicas.

Server RTMP e stream key ficam mascarados na UI. API sem capacidade oficial de fornecer chave exige configuração segura; nunca inventar ou inferir valor.

### 4.4 Processos

Launcher mantém processo Node pertencente ao plugin, evita processos duplicados e para servidor no encerramento.

Implementação atual para Windows:

- Job Object com `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`;
- shutdown interno autenticado antes de `terminate()`;
- fallback controlado para `kill()` quando processo não responder;
- PID do OBS e token aleatório passados por `QProcessEnvironment`;
- watchdog Node encerra quando não encontra PID do OBS;
- `runtime.json` removido somente quando pertence ao token da instância atual.

Validação confirmou `node --check`, `git diff --check` e build CMake com `AUTOMOC`, `AUTOUIC` e `AUTORCC`. Smoke runtime ficou bloqueado neste checkout porque `data/streamhub-server/node_modules` e `data/streamhub-server/config.json` não existem. Ainda falta teste real de encerramento anormal do OBS, PID reutilizado e Job Object após crash.

## 5. IPC e sincronização Kick

IPC HTTP local autenticado controla lifecycle e sincronização Kick. Node continua autoridade para APIs e credenciais; C++ continua autoridade para `GlobalMultiOutputConfig()`. A ponte Kick usa dois endpoints internos: nonce temporário e operação de leitura. Ambos exigem loopback, `X-StreamHub-Token`, PID Node esperado e PID OBS esperado no cliente C++.

Contrato aplicado à sincronização Kick:

1. Canal local autenticado e temporário, separado dos endpoints de lifecycle.
2. Endpoint com nonce, autenticação de processo e escopo mínimo.
3. Stream key trafega somente durante operação autorizada de sincronização; nunca em log, URL, evento comum ou arquivo público.
4. Resposta normal retorna estado mascarado, plataforma, servidor disponível e resultado da operação; não retorna chave.
5. C++ localiza destino por identidade estável de plataforma.
6. Destino ausente é criado mesmo sem live ativa ou destino prévio.
7. Destino existente é atualizado somente com valores oficiais válidos.
8. Respostas vazias ou não autorizadas não apagam chave existente.
9. Não duplicar plataforma que já seja transmissão principal sem regra explícita.
10. Sincronização preserva ordem, nomes personalizados, encoders, áudio, vídeo, `outputParam`, `syncStart`, `syncStop` e demais configurações.
11. Operação idempotente; repetição não cria cartões duplicados.

## 6. Roadmap

### 6.1 Plano de execução faseado — Etapa 2

Objetivo da Etapa 2: manter o servidor Node ligado somente enquanto a instância correta do OBS existir, impedir colisão com instância anterior e garantir encerramento gracioso antes de finalizar o processo.

#### Fase 1 — Paths, runtime e contrato de ambiente

**Plano:**

- Resolver dados graváveis por `StreamHubWritableDataPath()`, servidor por `StreamHubServerPath()` e configuração do módulo por `StreamHubModuleConfigPath()`/`obs_module_config_path()`.
- Não inserir caminhos absolutos de máquina.
- Criar leitura e gravação atômica de `runtime.json`, contendo somente `nodePid`, `obsPid`, `port` e `token`.
- Restringir permissões do arquivo quando o sistema permitir.

**Estado:** implementado no launcher C++. `QSaveFile` grava o runtime de forma atômica; token aleatório é mantido em memória e escrito somente para descoberta e controle local. Runtime inválido, incompleto ou sem caminho válido não é usado para controlar processo. O arquivo é removido quando pertence ao token da instância atual.

**Validação pendente:** testar permissões efetivas em instalação OBS instalada e portátil, corrupção de `runtime.json` e recuperação após encerramento durante a gravação.

#### Fase 2 — Servidor Node, endpoints internos e watchdog

**Plano:**

- Ler `STREAMHUB_OBS_PID`, `STREAMHUB_INSTANCE_TOKEN` e `PORT` fornecidos pelo launcher.
- Validar PID como inteiro positivo.
- Expor `GET /internal/status` e `POST /internal/shutdown` somente para loopback e com `X-StreamHub-Token` válido; responder `401` sem autorização.
- Implementar `shutdown()` idempotente: parar watchdog, cancelar long-polls, parar conectores, fechar Socket.IO/HTTP e sair após concluir o fechamento.
- Tratar `SIGTERM` e `SIGINT` pelo mesmo fluxo de shutdown.
- Verificar o processo do OBS a cada 2 segundos sem depender de texto externo.
- Preservar compatibilidade com conectores que retornam `stop()` ou `disconnect()`.

**Estado:** implementado em `data/streamhub-server/server/index.js`. O servidor escuta em `localhost`, usa porta padrão `605`, encerra quando `process.kill(OBS_PID, 0)` não encontra o OBS, cancela o watchdog durante shutdown e chama `process.exit()` somente depois do fechamento. O modo standalone exige `STREAMHUB_ALLOW_STANDALONE=1`; nesse modo watchdog não inicia.

**Validação concluída:** `node --check` passou; testes anteriores confirmaram `401` sem token e com token inválido, `200` com token válido, resposta com PID/porta esperados e shutdown autenticado.

**Validação pendente:** repetir teste com runtime provisionado nesta cópia, fechar OBS durante execução real, enviar sinais repetidos e confirmar que shutdown concorrente não duplica parada de conector nem deixa processo Node órfão.

#### Fase 3 — Launcher C++ e instância anterior

**Plano:**

- Gerar token por processo usando API criptográfica Qt.
- Ler runtime anterior e validar PID, token, porta e processo esperado.
- Solicitar `/internal/status` e depois `/internal/shutdown` por loopback antes de encerrar instância anterior.
- Nunca usar `taskkill /IM node.exe` nem matar PID sem validação.
- Passar PID do OBS e token por `QProcessEnvironment`.
- Gravar runtime somente depois de Node iniciar.
- Executar encerramento em fases: endpoint autenticado, `terminate()`, espera limitada e `kill()` como último recurso.
- Evitar corrida entre restart, sinal `finished` e destrutor.

**Estado:** implementado em `src/streamhub-launcher.cpp` e `src/streamhub-launcher.h`. `PreparePreviousInstance()` remove runtime obsoleto quando PID não existe, preserva processo quando handshake falha e só solicita shutdown após resposta autenticada com PID do Node e PID do OBS esperados. `Stop()` tenta endpoint interno, espera até 3 segundos por encerramento gracioso e usa `kill()` somente como fallback.

**Validação pendente:** testar PID reutilizado por processo diferente, runtime com token válido apontando para porta errada, restart repetido e falha de resposta durante shutdown.

#### Fase 4 — Windows process ownership

**Plano:**

- Associar Node a Windows Job Object após início.
- Configurar `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
- Manter handle no launcher e fechá-lo no encerramento/destrutor.
- Se criação ou associação falhar, registrar aviso e manter fallback `terminate()`/`kill()` sem derrubar OBS.
- Manter caminho POSIX compilável sem APIs Windows.

**Estado:** implementado sob `_WIN32` em `AttachJobObject()` e `CloseJobObject()`. O Job Object é associado ao PID retornado por `QProcess`; falhas são tratadas sem interromper OBS.

**Validação pendente:** encerrar OBS de forma anormal e confirmar que Node termina pelo fechamento do Job Object; repetir com processo filho e em instalação não portátil.

#### Fase 5 — Integração OBS e bundle

**Plano:**

- Usar PID da instância atual do OBS, sem caminho fixo.
- Manter `obs_module_load()` e o evento `OBS_FRONTEND_EVENT_EXIT` compatíveis com launcher estático.
- Não substituir `config.json`, `accounts-private.json`, `node_modules` ou configurações do usuário durante extração e runtime.
- Versionar reextração por `kBundleVersion` quando conteúdo de `qrc/streamhub-data.qrc` mudar.

**Estado:** launcher usa `QCoreApplication::applicationPid()` e bundle usa extração versionada. `src/streamhub-bundle.cpp` preserva `streamhub-server/config.json`; arquivos privados e dependências existentes não devem ser substituídos. O servidor embutido escuta `127.0.0.1` e anuncia `localhost` na porta `605` por padrão. `kBundleVersion` está em `32` após mudanças Twitch, Kick e YouTube; futuras mudanças em arquivo embutido exigem novo incremento antes de distribuir a DLL.

#### Fase 6 — Validação integrada

**Plano:**

1. Rodar `node --check` em cada arquivo Node alterado.
2. Testar endpoints internos com token ausente, incorreto e correto.
3. Testar watchdog com PID inexistente, shutdown repetido e sinais `SIGTERM`/`SIGINT`.
4. Configurar e compilar CMake com `AUTOMOC`, `AUTOUIC` e `AUTORCC`.
5. Testar DLL carregada pelo OBS, criação/preservação de bundle, `runtime.json` e encerramento.
6. Conferir diff, caminhos dinâmicos e ausência de `taskkill /IM node.exe`.

**Resultado atual:** `node --check` passou para `accounts.js`, `routes/api.js`, `index.js` e `oauth-broker/server.js`; `git diff --check` não encontrou erros de whitespace; `cmake --preset windows-x64` e `cmake --build --preset windows-x64 --config RelWithDebInfo` passaram; DLL foi reconstruída com bundle `32` e carregada pelo OBS; sync interno Twitch respondeu `200` com servidor `rtmps://live.twitch.tv/app`, chave presente e PIDs validados; sync interno Kick respondeu `200` com servidor e chave presentes; sync interno YouTube respondeu `200` com servidor e chave presentes. Teste anterior de lifecycle confirmou `401` sem autorização, `200` com token correto, shutdown Node e remoção de `runtime.json`, mantendo OBS aberto. Broker OAuth ainda precisa hospedagem HTTPS real e configuração de `STREAMHUB_OAUTH_BROKER_URL` antes do fluxo compartilhado funcionar em instalação final.

**Pendências:** watchdog após fechamento real do OBS; PID reutilizado; Job Object após crash; restart completo; instalação OBS não portátil; chat YouTube/Kick em live real; transmissão RTMP ponta a ponta; teste do botão Twitch no fluxo visual; preservação completa de metadados em todas as plataformas.

#### Ordem de execução usada

1. Helpers de paths, runtime e contrato de ambiente.
2. Shutdown, endpoints e watchdog Node.
3. Launcher C++ e limpeza segura de instância anterior.
4. Job Object Windows.
5. Integração OBS e bundle.
6. OAuth e sincronização Twitch, Kick e YouTube.
7. Checks de sintaxe, testes, instalação OBS e build.

### 6.2 Próximas entregas

1. Validar OAuth Kick completo: navegador, callback `localhost:605`, `state`, PKCE, refresh token, identificação de conta e permissões.
2. Validar sincronização Kick pelo botão: busca oficial, atualização de destino existente, criação idempotente de destino ausente e preservação de configurações avançadas.
3. Validar sincronização Twitch pelo botão após OAuth com `channel:read:stream_key`, incluindo regra de Twitch como transmissão principal sem destino duplicado.
4. Validar Kick RTMP real, incluindo início/parada, sem registrar stream key.
5. Validar YouTube em live real: descoberta de `activeLiveChatId`, leitura de chat, consulta, edição e RTMP.
6. Validar watchdog e Job Object após encerramento anormal do OBS, PID reutilizado, restart e instalação OBS não portátil.
7. Adicionar testes automatizados para nonce, autorização interna, respostas inválidas e preservação/criação de destinos.
8. Validar metadados de título, categoria, tags, idioma e visibilidade para cada plataforma.
9. Corrigir resultado Twitch para tratar notificação inexistente como limitação ignorada, não falha.
10. Validar build, instalação limpa, chats, sincronização, início/parada e transmissão real ponta a ponta.

## 7. Verificação

### Node

```powershell
node --check server/index.js
node --check server/accounts.js
node --check server/routes/api.js
```

Executar checks para arquivos Node alterados. Usar credenciais fictícias em testes.

### CMake

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

Confirmar Qt `AUTOMOC`, `AUTOUIC` e `AUTORCC`, DLL carregada e bundle extraído.

### Regressão OBS

- Testar DLL em diretório limpo, sem `data/` manual.
- Confirmar criação e preservação de `config.json` e `node_modules`.
- Verificar labels traduzidos e ícones de adicionar, iniciar, parar e plataformas.
- Salvar opção de horário e conferir efeito imediato.
- Enviar pela Twitch em **Todos** e confirmar uma única linha local.
- Abrir ADM e verificar moderação/recompensas com conta autorizada.
- Conectar plataforma sem live/destino prévio e confirmar criação idempotente quando sincronização existir.
- Testar falha isolada de conector.
- Testar fechamento do OBS sem Node órfão.
- Testar início/parada real de Twitch, Kick e YouTube.
- Conferir `git diff` e preservar alterações pré-existentes.

## 8. Política Git

Não fazer commit ou push sem pedido explícito. Antes de qualquer commit, conferir diff, arquivos staged e ausência de segredos. Mudança documental não autoriza commit de código pré-existente.
