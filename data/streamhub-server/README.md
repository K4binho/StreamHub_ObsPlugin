# Servidor de chat do StreamHub

Este servidor Node é extraído e iniciado automaticamente pela DLL do StreamHub. Ele alimenta o dock nativo e o overlay com o mesmo fluxo de mensagens, mantém a autorização da Twitch e executa as ações de canal. A configuração pública fica em `config.json`; tokens ficam separados em `accounts-private.json`.

O dock **StreamHub Chat · K4** contém o campo de envio. O filtro selecionado define o destino; em **Todos**, a linha local usa o selo Todos e as cópias devolvidas pelos conectores são suprimidas. O botão **ADM** abre timeout, banimento, configurações do chat, recompensas e resgates.

## Overlay para a transmissão

Na engrenagem do chat, abra a aba **Overlay**. Ela permite definir o tempo de exibição, o nome do canal usado no destaque de menções e o filtro de comandos iniciados por `!` para cada plataforma. Use **Copiar URL** para obter o endereço completo conforme a porta configurada.

Adicione a URL como **Fonte de navegador** no OBS. O caminho padrão é:

```text
http://localhost:605/overlay.html
```

O overlay tem fundo transparente, ícone e cor fixa por plataforma, badges de mod/sub/vip, destaque de menções e expiração automática. Estados comuns de conexão não são desenhados sobre a live. A opção **Abrir prévia** mostra mensagens de demonstração para ajustar a cena.

## Conectores

- **Twitch:** leitura pública; envio, dados da live, moderação e recompensas usam autorização OAuth persistente pelo Device Code Flow. Este fluxo não fornece stream key; configure a chave manualmente no destino nativo Twitch.
- **Kick e YouTube:** fluxo normal usa OAuth compartilhado quando broker HTTPS real está implantado e `STREAMHUB_OAUTH_BROKER_URL` está configurado: clique **Conectar**, autorize na página oficial e volte ao OBS. Broker mantém Client Secrets fora da DLL e troca o código OAuth. **Avançado** mantém OAuth local com credenciais manual ou `.env` como fallback.
- As UI Kick e YouTube permitem conectar pelo broker sem Client ID ou Client Secret local. **Avançado** permite configurar credenciais localmente; tokens continuam privados em `accounts-private.json`.
- Sincronização usa ponte local autenticada entre Node e C++ com nonce de uso único. Busca dados oficiais e cria ou atualiza um único destino da plataforma, sem enviar chave por long-poll, Socket.IO, URL, log ou documentação. Erro de sincronização aparece na mensagem auxiliar do cartão; estado OAuth **Conectada** permanece separado.
- Credenciais manuais preenchidas vencem `.env`; campos manuais vazios usam `KICK_*` ou `YOUTUBE_*` correspondente. Kick e YouTube resolvem credenciais e fluxos independentemente.
- Endpoints, escopos e campos de stream key Kick ainda precisam confirmação oficial e teste real; envio autenticado e moderação permanecem pendentes.

Contexto detalhado: [PLANOS/ARQUITETURA_E_CONTEXTO.md](../../PLANOS/ARQUITETURA_E_CONTEXTO.md).

O servidor é iniciado pelo launcher do plugin com `STREAMHUB_OBS_PID`, `STREAMHUB_INSTANCE_TOKEN` e porta configurada. O servidor escuta em `localhost`, com porta padrão `605`. O lifecycle local usa `runtime.json`, `GET /internal/status` e `POST /internal/shutdown`; ambos endpoints exigem loopback e `X-StreamHub-Token`. Watchdog verifica o PID do OBS a cada 2 segundos. Sincronização Kick usa `POST /internal/kick-sync/nonce` e `POST /internal/kick-sync`; YouTube usa `POST /internal/youtube-sync/nonce` e `POST /internal/youtube-sync`. Ambas usam mesmo handshake e nonce temporário.

<!-- histórico -->

- **Kick:** canal público por protocolo usado pelo site; validado ponta a ponta. (Registro histórico; implementação atual descrita acima.)
- **YouTube:** OAuth persistente e descoberta de `activeLiveChatId` por `videos.list`; leitura usa `liveChatMessages.list`. Consulta, atualização e leitura em live real ainda precisam de validação.
- **TikTok:** requer o usuário sem `@`; permanece experimental até validação ponta a ponta em uma live real.
- **Facebook:** ainda não possui conector de chat.

Kick e TikTok dependem de protocolos não oficiais e podem exigir manutenção caso as plataformas mudem seus serviços.

### OAuth do YouTube

Fluxo normal YouTube usa broker OAuth HTTPS compartilhado. O operador do broker configura a aplicação OAuth, callback público e Client Secret; usuário final não cria app nem informa credenciais. **Avançado** mantém cliente OAuth local, callback loopback e credenciais somente para desenvolvimento/fallback. Tokens são salvos apenas em `accounts-private.json`.

O modo de teste do Google deve listar cada usuário autorizado. Para uso por qualquer pessoa, publique o aplicativo e conclua a verificação que o Google solicitar para o escopo. A API normalmente usa cota gratuita diária, sem cobrança por chamada pelo StreamHub.

OAuth YouTube já foi validado no navegador externo e retornou a página **YouTube conectado ao StreamHub**. O conector de chat agora descobre `activeLiveChatId` por `videos.list` e lê mensagens por `liveChatMessages.list`; carregamento e atualização dos dados da live ainda precisam de validação em live real.

Stream keys não pertencem ao fluxo comum de chat. Sincronização de servidor RTMP e stream key com **Múltiplas saídas** precisa usar API ou credencial oficial, preservar destinos existentes e manter chaves fora de logs, documentação e controle de versão. Kick possui ponte preliminar; outras plataformas só devem sincronizar quando fornecerem chave de modo autorizado. Sem valor oficial, servidor deve solicitar configuração segura em vez de salvar valor inventado.

## Estado de validação — 09/09/2026

- OAuth YouTube: concluído com conta de teste.
- Leitura/envio de chat e atualização de live: implementados no código, ainda sem validação em live real.
- Painel de transmissão: consulta, atualização da live e leitura real do chat YouTube ainda não foram validadas em live real.
- Painel Twitch: mensagem atual informa `Twitch: Atualizada; notificação não existem na API da Twitch.`; notificação não é campo da API Twitch e deve ser reportada como ignorada, não como falha.
- Kick OAuth/sincronização: código integrado, mas endpoints, escopos e campos oficiais de stream key ainda não confirmados; teste real pendente.
- YouTube OAuth, configuração por `.env` ou manual, sincronização Node/C++ e destino nativo estão integrados; teste interno retornou `HTTP 200` com servidor e stream key presentes. Teste RTMP real e validação em live real continuam pendentes.
- Destino nativo sem servidor ou stream key fica **Pendente** e não inicia. Para Twitch, informe a stream key manualmente e ative o destino no cartão de **Múltiplas saídas · K4**.
## Execução manual para desenvolvimento

O servidor foi projetado para ser iniciado pelo plugin OBS, que injeta `PORT`, `STREAMHUB_OBS_PID` e `STREAMHUB_INSTANCE_TOKEN`. Execução manual exige ambiente standalone explícito; use somente em desenvolvimento local.

```powershell
npm install
Copy-Item config.example.json config.json
$env:STREAMHUB_ALLOW_STANDALONE = "1"
npm start
```

No modo standalone, endpoints internos continuam sem token válido e watchdog não inicia. Launcher OBS permanece caminho suportado.

O painel de múltiplas saídas é nativo em C++ e compartilha os recursos do OBS. O servidor Node cuida dos chats, overlay, OAuth e chamadas autenticadas das plataformas; C++ controla destinos RTMP. Durante sincronização Kick ou YouTube, stream key transita somente na resposta HTTP interna autenticada e fica em memória para atualizar o destino; não é registrada em log ou resposta pública.

A execução pelo plugin OBS é caminho suportado. Ver [PLANOS/ARQUITETURA_E_CONTEXTO.md](../../PLANOS/ARQUITETURA_E_CONTEXTO.md) para estado, arquitetura, requisitos e critérios de validação.

**Segurança:** nunca registrar Client Secret, tokens ou stream keys em logs, URLs, documentação, screenshots ou Git.

## Plano de ação pendente

1. Confirmar contrato oficial Kick: endpoints, escopos, PKCE e campos de servidor RTMP/stream key.
2. Revogar Client Secret Kick fornecido anteriormente e criar substituto antes de teste real.
3. Validar OAuth Kick, refresh, busca oficial de transmissão, criação/atualização idempotente de destino e transmissão RTMP.
4. Integrar envio autenticado, chat OAuth e moderação Kick somente conforme capacidades oficiais confirmadas.
5. Validar watchdog, Job Object, instalação limpa e testes automatizados de segurança e preservação de configuração.
6. Validar YouTube em live real, consulta/edição da live e transmissão RTMP ponta a ponta.

<!-- histórico -->

Este plugin é fornecido gratuitamente. Projeto original: [SoraYuki](https://github.com/sorayuki/obs-multi-rtmp). Melhorias StreamHub: K4binho — [apoiar via LivePix](https://livepix.gg/k4binho).
