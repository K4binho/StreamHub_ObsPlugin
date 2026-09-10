# StreamHub OBS Plugin

Plugin nativo para OBS Studio, desenvolvido e mantido principalmente por K4binho.

## Recursos

- Saídas RTMP nativas para múltiplas plataformas.
- Transmissão principal do OBS exibida como primeiro cartão.
- Docks nativos Qt para saídas, chat e informações da transmissão.
- Chat unificado com filtros por plataforma, envio e deduplicação.
- Moderação e recompensas pelo painel **ADM**, conforme autorização da conta.
- Twitch OAuth para chat, dados da live, moderação e recompensas.
- YouTube e Kick usam OAuth compartilhado por navegador quando broker HTTPS real está implantado e `STREAMHUB_OAUTH_BROKER_URL` está configurado: usuário clica **Conectar**, autoriza na página oficial e volta ao OBS. Broker OAuth mantém Client Secrets fora da DLL; configuração manual ou `.env` fica como fallback avançado até essa configuração.
- Overlay transparente para **Fonte de navegador** do OBS.
- Runtime Node.js provisionado automaticamente, sem instalação externa obrigatória.
- Servidor Node.js, locale, ícones, branding e tema embutidos na DLL via Qt Resource System.
- Tema **K4binho — Má Fase** e fundo de cena 1920×1080.
- Compatibilidade com OBS instalado e portátil por resolução dinâmica de caminhos.

## Estado atual

- Twitch: leitura, envio, dados da live, moderação e recompensas implementados.
- YouTube: OAuth compartilhado por broker, status, renovação, descoberta de transmissão e sincronização nativa integrados. Configuração manual ou `.env` permanece fallback avançado. Conta permanece **Conectada** mesmo quando sincronização manual não encontra live ou dados RTMP; Chat, consulta/edição da live e transmissão RTMP real ainda precisam validação.
- Servidor Node: `localhost:605` por padrão, watchdog de 2 segundos e lifecycle interno autenticado por token em `runtime.json`. `server.port` explícita continua respeitada.
- Launcher C++: shutdown gracioso, validação de instância anterior e Job Object Windows implementados. Teste real de encerramento anormal do OBS ainda pendente.
- Kick: leitura de chat existente; OAuth e sincronização nativa preliminares integrados. Contrato oficial de endpoints, escopos e campos de stream key, envio autenticado, moderação e teste real ainda pendentes.
- Twitch: OAuth não fornece stream key neste fluxo. Preencha chave manualmente no cartão Twitch e ative o toggle antes de usar **Iniciar tudo**.
- TikTok: experimental.
- Facebook: destino RTMP disponível; chat ainda não possui conector funcional.
- Kick e YouTube conectados buscam servidor RTMP e stream key pelos canais internos autenticados e criam ou atualizam um único destino por plataforma, preservando configurações existentes. Credenciais manuais vazias usam `.env`; manual preenchida vence `.env`.
- Transmissão RTMP real ainda não foi validada de ponta a ponta. Destino sem stream key fica **Pendente** e não inicia.

## Instalação rápida

1. Baixe ou compile o plugin.
2. Feche OBS.
3. Copie `obs-multi-rtmp.dll` para `obs-plugins\64bit` na instalação do OBS.
4. Abra OBS e aguarde primeira extração do bundle e instalação das dependências Node.
5. Abra **Painéis** e confirme:
   - **Múltiplas saídas · K4**;
   - **StreamHub Chat · K4**;
   - **Informações de transmissão K4**.

Não copie `data/` manualmente quando usar DLL com recursos embutidos. Não sobrescreva configuração, tokens ou `node_modules` existentes.

## Build Windows

Pré-requisitos:

- Visual Studio Community 2026 com **Desenvolvimento para desktop com C++**;
- CMake 3.28+;
- Git.

Na raiz do repositório:

```powershell
cmake --preset windows-x64
```

```powershell
cmake --build --preset windows-x64 --config RelWithDebInfo
```

DLL gerada normalmente:

```text
build_x64\RelWithDebInfo\obs-multi-rtmp.dll
```

Node.js não é pré-requisito. Plugin usa Node do PATH ou baixa runtime portátil na primeira execução. QtWebSockets não é necessário; chat usa long-poll HTTP via `Qt6::Network`.

## Overlay

Abra engrenagem de **StreamHub Chat · K4**, selecione **Overlay** e clique **Copiar URL**. URL padrão:

```text
http://localhost:605/overlay.html
```

Adicione URL como **Fonte de navegador** no OBS. Fundo permanece transparente.

## Documentação

- [Arquitetura, contexto, regras de desenvolvimento e roadmap](PLANOS/ARQUITETURA_E_CONTEXTO.md)
- [Manual de uso, build, instalação, overlay, tema e Flatpak](PLANOS/MANUAL_E_BUILD.md)
- [Build Flatpak especializado](FLATPAK.md)
- [Instalação do tema](themes/INSTALACAO.md)
- [Documentação do servidor](data/streamhub-server/README.md)

## Segurança

Nunca publique Client Secret, access token, refresh token, stream key ou URL OAuth contendo `code=`. Segredos não devem aparecer em logs, Markdown, screenshots, URLs, eventos comuns, commits ou releases.

## Créditos

- Desenvolvimento e manutenção: K4binho
- Apoio ao StreamHub: [LivePix](https://livepix.gg/k4binho)
- Referência técnica: [`obs-multi-rtmp`](https://github.com/sorayuki/obs-multi-rtmp) · apoio ao projeto: [PayPal](https://paypal.me/sorayuki0)

## Licença

- [GPL-2.0 — tradução para português do Brasil](LICENSE.pt-BR)
- [GPL-2.0 — texto original em inglês](LICENSE)

A tradução PT-BR é não oficial e pode conter erros por ter sido traduzida do inglês. Em caso de divergência, `LICENSE` contém versão juridicamente válida. Consulte também licenças de dependências incluídas antes de redistribuir.
