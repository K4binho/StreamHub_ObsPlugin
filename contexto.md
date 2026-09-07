# Contexto atual — StreamHub OBS Plugin

**Data:** 07/09/2026
**Escopo:** integração YouTube/Kick, controle de transmissão e sincronização de destinos nativos.

## Estado atual

- Plugin C++/Qt para OBS Studio 32.2.1, baseado em `obs-multi-rtmp`.
- DLL autocontida: Node.js, locale, ícones, branding e tema entram na DLL por Qt Resource System e são extraídos por `StreamHub_EnsureBundledData()`.
- Dock **Múltiplas saídas · K4** mostra transmissão principal do OBS e destinos RTMP adicionais.
- Dock **StreamHub Chat · K4** usa long-poll HTTP, leitura de Twitch/Kick/YouTube, envio autenticado disponível conforme conector e botão ADM para moderação/recompensas.
- Dock **Informações de transmissão K4** contém contas e edição dos dados da transmissão.
- Twitch OAuth, dados da live, envio, moderação e recompensas estão implementados.
- YouTube OAuth foi validado no navegador externo. Callback local retornou **YouTube conectado ao StreamHub**. Leitura/envio de chat e atualização de live existem no código, mas ainda aguardam validação em live real.
- Kick possui leitura de chat pelo protocolo atualmente usado pelo site, mas OAuth oficial, envio autenticado, moderação e leitura autorizada de stream key ainda não foram integrados.
- TikTok permanece experimental. Facebook possui somente modelo de destino RTMP.

## Problemas observados

1. **YouTube:** o painel falha ao carregar a transmissão com:
   `Parâmetros incompatíveis especificados na solicitação: mine, broadcastStatus`
   A consulta atual combina `mine=true` e `broadcastStatus`, combinação rejeitada pela API. Corrigir antes de considerar edição YouTube concluída.
2. **Twitch:** o painel mostra:
   `Twitch: Atualizada; notificação não existem na API da Twitch.`
   Notificação não é campo disponível nessa API. Resultado deve marcar campo como ignorado ou informar limitação sem sugerir falha.
3. **Sincronização:** servidor RTMP, stream key e destinos nativos ainda exigem configuração manual. Requisito confirmado: sincronizar após conexão da plataforma e criar destino correspondente em **Múltiplas saídas**, mesmo sem live ou destino previamente configurado.

## Arquitetura relevante

### Servidor Node

- `data/streamhub-server/server/accounts.js`: tokens OAuth, renovação e armazenamento privado em `accounts-private.json`.
- `data/streamhub-server/server/routes/api.js`: endpoints locais de contas, live, chat, moderação e recompensas.
- `data/streamhub-server/server/index.js`: inicia servidor, histórico, long-poll e conectores isolados.
- `data/streamhub-server/server/chat/kick.js`: leitura atual do chat Kick via Pusher usado pelo site.
- `data/streamhub-server/server/platform-actions.js`: envio e sincronização de ações entre plataformas; não controla saídas RTMP nativas.

### Saídas nativas C++

- `src/output-config.h/.cpp`: `GlobalMultiOutputConfig()`, persistência de `targets`, encoders e áudio em `obs-multi-rtmp.json` dentro do perfil ativo do OBS.
- `src/obs-multi-rtmp.cpp`: UI, criação de destinos, `AddPushWidget()`, reordenação, carregamento e salvamento.
- `src/push-widget.cpp`: estado/início/parada de cada destino. Exige `serviceParam.server` e `serviceParam.key` não vazios.
- `src/streamhub-platforms.h/.cpp`: presets, ícones, cores e servidores públicos conhecidos. Kick ainda não possui servidor automático.
- `src/streamhub-launcher.h/.cpp`: ciclo de vida assíncrono do Node e reinício do chat.
- `src/streamhub-control-dock.h/.cpp`: UI OAuth Twitch/YouTube, carregamento e aplicação de dados da live.

Node é autoridade para APIs e credenciais de plataformas. C++ é autoridade para saídas RTMP e configuração nativa. Não existe ponte Node/C++ pronta para alterar `GlobalMultiOutputConfig()`.

## Requisitos de implementação

- Integrar Kick OAuth por navegador externo, PKCE, callback loopback, `state`, expiração e refresh token.
- Usar somente escopos oficiais necessários, incluindo `streamkey:read`, `chat:write`, `channel:read`, `channel:write`, `moderation:ban` e `moderation:chat_message:manage`, conforme permissões disponíveis no aplicativo Kick.
- Permitir envio autenticado de chat Kick e ações de moderação quando conta tiver autorização: ban, timeout, unban e gerenciamento de mensagens conforme API/escopos suportados.
- Ler servidor RTMP e stream key Kick pela capacidade oficial autorizada. Nunca obter chave pelo chat, inferir valor ou inventar chave.
- Definir fluxo oficial para stream key YouTube; OAuth não deve criar ou expor chave sem endpoint/capacidade válida.
- Ao conectar plataforma, criar ou atualizar destino nativo correspondente sem duplicação, mesmo sem live ativa ou destino prévio.
- Atualizar somente servidor/chave oficiais quando valor válido estiver disponível. Não apagar chave existente quando API retornar vazio ou indisponível.
- Preservar destino existente, ordem, nome personalizado, `videoConfig`, `audioConfig`, `outputParam`, `syncStart`, `syncStop`, encoders e configurações avançadas.
- Evitar duplicar plataforma que já seja transmissão principal do OBS sem confirmação clara da regra de destino.
- Manter servidor e stream key mascarados na UI. Não escrever segredos em logs, URLs, eventos de status, Markdown, screenshots, `config.json` público, commits ou push.
- Usar ponte local autenticada e temporária entre Node e C++, com escopo mínimo e sem expor chave em respostas normais do servidor.
- Falha de Twitch, Kick ou YouTube não pode derrubar outros conectores, servidor local ou saídas nativas.
- Preservar alterações pré-existentes do usuário neste worktree.

## Segurança de credenciais

- Client ID pode ficar na configuração local.
- Client Secret, access token, refresh token e stream key ficam somente em armazenamento local protegido. `accounts-private.json` e `config.json` estão no `.gitignore`.
- Não registrar credenciais em documentação ou memória persistente.
- Client Secret Kick fornecida durante testes não deve ser reproduzida. Deve ser recriada após validação.
- Conferir diff e arquivos staged antes de qualquer commit/push.

## Verificação já concluída

- `node --check` passou nos arquivos Node alterados anteriormente.
- `cmake --preset windows-x64` passou.
- `cmake --build --preset windows-x64 --config RelWithDebInfo` passou.
- DLL gerada em `build_x64/RelWithDebInfo/obs-multi-rtmp.dll`.
- OAuth YouTube e callback externo/local foram validados.
- Chats Twitch/Kick foram validados anteriormente nesta máquina.

## Verificação pendente

- Corrigir e testar consulta YouTube.
- Corrigir mensagem de notificação Twitch.
- Integrar e testar OAuth Kick, refresh, chat, moderação e `streamkey:read`.
- Testar ponte Node/C++ autenticada.
- Conectar plataforma sem live pré-configurada e confirmar criação automática de destino.
- Confirmar preservação de destinos, ordem e configurações avançadas.
- Testar início/parada real de saídas Twitch/Kick/YouTube.
- Repetir regressão dos chats e testar em instalação limpa.
- Recriar Client Secret Kick após testes.

## Processo obrigatório

Esta atualização documental precede qualquer alteração de código da próxima etapa. O plano detalhado deve ser aprovado antes da implementação. Commit/push desta etapa deve conter somente documentação e contexto seguro; implementação não deve ser commitada ou enviada sem pedido explícito separado.
