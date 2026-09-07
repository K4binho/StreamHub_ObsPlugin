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
- **Kick:** canal público por protocolo usado pelo site; validado ponta a ponta.
- **YouTube:** requer chave da YouTube Data API v3 e o ID da live ativa.
- **TikTok:** requer o usuário sem `@`; permanece experimental até validação ponta a ponta em uma live real.
- **Facebook:** ainda não possui conector de chat.

Kick e TikTok dependem de protocolos não oficiais e podem exigir manutenção caso as plataformas mudem seus serviços.

## Execução manual para desenvolvimento

```powershell
npm install
Copy-Item config.example.json config.json
npm start
```

O painel de múltiplas saídas é nativo em C++ e compartilha os recursos do OBS. O servidor Node cuida dos chats, overlay e chamadas autenticadas das plataformas; ele não recebe nem controla stream keys.

Este plugin é fornecido gratuitamente. Projeto original: [SoraYuki](https://github.com/sorayuki/obs-multi-rtmp). Melhorias StreamHub: K4binho — [apoiar via LivePix](https://livepix.gg/k4binho).
