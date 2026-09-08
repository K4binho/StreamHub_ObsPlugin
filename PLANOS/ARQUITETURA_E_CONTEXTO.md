# StreamHub OBS Plugin — Arquitetura, Contexto e Plano de Ação

**Última atualização:** 08/09/2026

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
- Twitch OAuth, leitura, envio, dados da live, moderação e recompensas implementados.
- OAuth YouTube validado no navegador externo, callback local e retorno **YouTube conectado ao StreamHub**.
- Chat Twitch e Kick validado anteriormente no dock.
- Mensagem enviada em **Todos** aparece uma vez; ecos são deduplicados.
- Configuração do chat usa formulário Qt, gravação atômica e preservação de campos JSON desconhecidos.
- Tema **K4binho — Má Fase**, ícones, branding e overlay embutidos.
- Relay/FFmpeg não faz parte da inicialização atual.

### Pendente ou não validado

- Consulta YouTube falha com `Parâmetros incompatíveis especificados na solicitação: mine, broadcastStatus`. A combinação precisa ser separada conforme regras da API.
- Resultado Twitch menciona `notificação não existem na API da Twitch.`. Campo inexistente deve ser ignorado ou mostrado como limitação, não como falha.
- Transmissão RTMP real ainda não foi validada de ponta a ponta.
- Kick ainda não possui OAuth oficial, refresh token, envio autenticado, moderação nem leitura autorizada de stream key.
- TikTok continua experimental. Facebook possui modelo de destino RTMP, sem conector de chat funcional.
- Sincronização automática entre contas Node e destinos nativos C++ ainda não existe.
- Watchdog Node e Job Object Windows precisam ser concluídos e validados.
- Ponte IPC Node/C++ ainda não existe.
- Client Secret Kick precisa ser recriada após testes.

Não marcar item pendente como implementado sem teste correspondente.

## 3. Arquitetura

### 3.1 Servidor Node

Diretório-fonte: `data/streamhub-server/`.

- `server/index.js`: entrypoint, servidor HTTP local, histórico, long-poll e eventos de status.
- `server/accounts.js`: OAuth, renovação e persistência privada de tokens por plataforma.
- `server/routes/api.js`: endpoints locais para contas, transmissão, chat, moderação e recompensas.
- `server/chat/*.js`: conectores Twitch, Kick, YouTube e TikTok.
- `server/config-store.js`: leitura e gravação da configuração pública.
- `public/overlay.html` e arquivos associados: overlay transparente do chat.

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

Launcher deve manter processo Node pertencente ao plugin, evitar processos duplicados e parar servidor no encerramento.

Implementação alvo para Windows:

- Job Object com `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`;
- encerramento gracioso com `terminate()`;
- fallback controlado para `kill()` quando processo não responder;
- PID passado de forma segura para Watchdog;
- nenhuma dependência de processo órfão após OBS fechar.

Watchdog ainda é roadmap, não implementação concluída.

## 5. IPC e sincronização futura

Node precisa obter dados oficiais de plataformas. C++ precisa alterar `GlobalMultiOutputConfig()`. Nenhuma ponte entre essas autoridades existe hoje.

Desenho obrigatório:

1. Canal local autenticado e temporário: `QLocalServer`/`QLocalSocket` no macOS/Linux ou Named Pipes no Windows.
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

1. Corrigir consulta YouTube e separar descoberta autenticada de filtros incompatíveis. Corrigir resultado Twitch para ignorar notificação inexistente. Incrementar bundle se QRC/servidor mudar.
2. Implementar Watchdog em `server/index.js`, usando `STREAMHUB_OBS_PID`, heartbeat e encerramento limpo.
3. Garantir Job Object no launcher C++ com `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
4. Definir e implementar IPC local autenticado e temporário Node/C++.
5. Integrar Kick OAuth oficial com navegador externo, callback loopback, PKCE, `state`, expiração e refresh token.
6. Usar somente escopos Kick oficiais liberados, incluindo `streamkey:read`, `chat:write`, `channel:read`, `channel:write`, `moderation:ban` e `moderation:chat_message:manage` quando disponíveis.
7. Implementar chat autenticado e moderação Kick conforme API autorizada.
8. Ler servidor RTMP e stream key Kick somente por capacidade oficial autorizada.
9. Definir fluxo oficial YouTube para servidor e stream key; sem valor oficial, solicitar configuração segura.
10. Sincronizar contas conectadas com `GlobalMultiOutputConfig()` sem duplicação e sem perda de configuração avançada.
11. Atualizar UI com estados sincronizado, pendente, indisponível e falha, sempre mascarando segredo.
12. Validar build, `node --check`, instalação limpa, chats, sincronização, início/parada e transmissão real.
13. Recriar Client Secret Kick após testes.

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
