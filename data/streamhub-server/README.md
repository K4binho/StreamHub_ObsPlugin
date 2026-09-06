# Servidor de chat do StreamHub

Este servidor Node é extraído e iniciado automaticamente pela DLL do StreamHub. Ele alimenta o dock nativo e o overlay com o mesmo fluxo de mensagens. A configuração fica em `config.json` e pode ser alterada pela engrenagem do **StreamHub Chat · K4**.

## Overlay para a transmissão

Na engrenagem do chat, abra a aba **Overlay**. Ela permite definir o tempo de exibição, o nome do canal usado no destaque de menções e o filtro de comandos iniciados por `!` para cada plataforma. Use **Copiar URL** para obter o endereço completo conforme a porta configurada.

Adicione a URL como **Fonte de navegador** no OBS. O caminho padrão é:

```text
http://127.0.0.1:3000/overlay.html
```

O overlay tem fundo transparente, ícone e cor fixa por plataforma, badges de mod/sub/vip, destaque de menções, expiração automática e aviso de reconexão. A opção **Abrir prévia** mostra mensagens de demonstração para ajustar a cena.

## Conectores

- **Twitch:** canal público, sem token; validado ponta a ponta.
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

O painel de múltiplas saídas é nativo em C++ e compartilha os recursos do OBS. O servidor Node cuida somente dos chats e do overlay.

Este plugin é fornecido gratuitamente. Projeto original: [SoraYuki](https://github.com/sorayuki/obs-multi-rtmp). Melhorias StreamHub: K4binho — [apoiar via LivePix](https://livepix.gg/k4binho).
