# Servidor de chat do StreamHub

Este servidor Node é extraído e iniciado automaticamente pela DLL do StreamHub. Ele alimenta o dock nativo e o overlay com o mesmo fluxo de mensagens, mantém a autorização da Twitch e executa as ações de canal. A configuração pública fica em `config.json`; tokens ficam separados em `accounts-private.json`.

O dock **StreamHub Chat · K4** contém o campo de envio. O filtro selecionado define o destino; em **Todos**, a linha local usa o selo Todos e as cópias devolvidas pelos conectores são suprimidas. O botão **ADM** abre timeout, banimento, configurações do chat, recompensas e resgates.

## Overlay para a transmissão

Na engrenagem do chat, abra a aba **Overlay**. Ela permite definir o tempo de exibição, o nome do canal usado no destaque de menções e o filtro de comandos iniciados por `!` para cada plataforma. Use **Copiar URL** para obter o endereço completo conforme a porta configurada.

Adicione a URL como **Fonte de navegador** no OBS. O caminho padrão é:

```text
http://127.0.0.1:3000/overlay.html
```

O overlay tem fundo transparente, ícone e cor fixa por plataforma, badges de mod/sub/vip, destaque de menções e expiração automática. Estados comuns de conexão não são desenhados sobre a live. A opção **Abrir prévia** mostra mensagens de demonstração para ajustar a cena.

## Conectores

- **Twitch:** leitura pública; envio, dados da live, moderação e recompensas usam autorização OAuth persistente pelo Device Code Flow.
- **Kick:** leitura atual pelo protocolo usado pelo site; OAuth oficial, envio autenticado, moderação e leitura autorizada de stream key ainda estão pendentes.
- A próxima etapa usará navegador externo, callback local, PKCE, refresh token e escopos oficiais, incluindo `streamkey:read` quando liberado pelo aplicativo Kick.
- A sincronização de servidor RTMP, stream key e destino nativo exigirá ponte local autenticada entre Node e C++. Nunca enviar chave por long-poll, Socket.IO, URL, log ou documentação.

Contexto detalhado: [PLANOS/ARQUITETURA_E_CONTEXTO.md](../../PLANOS/ARQUITETURA_E_CONTEXTO.md).

**Implementação aguarda aprovação do plano detalhado.**

<!-- histórico -->

- **Kick:** canal público por protocolo usado pelo site; validado ponta a ponta. (Registro histórico; implementação atual descrita acima.)
- **YouTube:** OAuth persistente pelo navegador para descobrir a live, ler/enviar no chat e atualizar os dados da transmissão. A antiga chave de API + ID da live continua aceita como compatibilidade somente para leitura.
- **TikTok:** requer o usuário sem `@`; permanece experimental até validação ponta a ponta em uma live real.
- **Facebook:** ainda não possui conector de chat.

Kick e TikTok dependem de protocolos não oficiais e podem exigir manutenção caso as plataformas mudem seus serviços.

### OAuth do YouTube

Ative a YouTube Data API v3 no Google Cloud, configure o consentimento externo, adicione o escopo `youtube.force-ssl` e crie um cliente OAuth do tipo **Aplicativo para computador**. O Client ID e o Client Secret são informados no cartão do YouTube em **Informações de transmissão K4 > Contas**. O retorno usa uma URL loopback em `127.0.0.1`, e os tokens são salvos apenas em `accounts-private.json`.

O modo de teste do Google deve listar cada usuário autorizado. Para uso por qualquer pessoa, publique o aplicativo e conclua a verificação que o Google solicitar para o escopo. A API normalmente usa cota gratuita diária, sem cobrança por chamada pelo StreamHub.

OAuth YouTube já foi validado no navegador externo e retornou a página **YouTube conectado ao StreamHub**. O carregamento dos dados da live ainda precisa corrigir uma consulta que combina parâmetros incompatíveis `mine` e `broadcastStatus`.

Stream keys não pertencem ao servidor de chat. Qualquer sincronização futura de servidor RTMP e stream key com **Múltiplas saídas** precisa usar API ou credencial oficial, preservar destinos existentes e manter chaves fora de logs, documentação e controle de versão. Se uma plataforma não fornecer chave de modo autorizado, o servidor deve solicitar configuração segura em vez de salvar valor inventado.

## Estado de validação — 07/09/2026

- OAuth YouTube: concluído com conta de teste.
- Leitura/envio de chat e atualização de live: implementados no código, ainda sem validação em live real.
- Painel de transmissão: erro conhecido na consulta YouTube `Parâmetros incompatíveis especificados na solicitação: mine, broadcastStatus`.
- Painel Twitch: mensagem atual informa `Twitch: Atualizada; notificação não existem na API da Twitch.`; notificação não é campo da API Twitch e deve ser reportada como ignorada, não como falha.
- Sincronização automática de servidor/chave e criação de destinos sem configuração prévia: requisito pendente.

## Execução manual para desenvolvimento

```powershell
npm install
Copy-Item config.example.json config.json
npm start
```

O painel de múltiplas saídas é nativo em C++ e compartilha os recursos do OBS. O servidor Node cuida dos chats, overlay e chamadas autenticadas das plataformas; ele não recebe nem controla stream keys. A sincronização futura usará ponte local autenticada e temporária, com chave transitando somente no canal privado necessário para atualizar o destino nativo.

A implementação da próxima etapa começa somente após aprovação do plano detalhado. Ver [PLANOS/ARQUITETURA_E_CONTEXTO.md](../../PLANOS/ARQUITETURA_E_CONTEXTO.md) para estado, arquitetura, requisitos e critérios de validação.

**Segurança:** nunca registrar Client Secret, tokens ou stream keys em logs, URLs, documentação, screenshots ou Git.

## Plano de ação pendente

1. Corrigir consulta YouTube e resultado Twitch.
2. Integrar OAuth Kick, refresh, chat e moderação.
3. Ler credenciais oficiais de transmissão quando escopos permitirem.
4. Criar ponte Node/C++ e sincronização idempotente sem duplicação.
5. Testar compatibilidade, segurança, criação sem live/destino prévio e transmissão real.

<!-- histórico -->

Este plugin é fornecido gratuitamente. Projeto original: [SoraYuki](https://github.com/sorayuki/obs-multi-rtmp). Melhorias StreamHub: K4binho — [apoiar via LivePix](https://livepix.gg/k4binho).
