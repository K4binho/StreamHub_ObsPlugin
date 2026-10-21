# StreamHub para OBS

**Transmita para vários destinos, acompanhe os chats e atualize as informações da live em um só lugar.**

O StreamHub é um plugin para OBS Studio que reúne saídas RTMP, chat multiplataforma, ferramentas de transmissão e um overlay personalizável. A interface e o tema foram feitos para o fluxo de trabalho do canal K4binho, e o projeto mantém seus componentes organizados para facilitar manutenção e contribuição.

[Baixar a versão mais recente](https://github.com/K4binho/StreamHub_ObsPlugin/releases/latest) · [Ver documentação](docs/project/BUILD_AND_USAGE.md) · [Arquitetura](docs/project/ARCHITECTURE.md) · [Reportar um problema](https://github.com/K4binho/StreamHub_ObsPlugin/issues)

## Visão geral

<p align="center">
  <img src="docs/images/presentation/obs-docks.png" alt="OBS com os docks de múltiplas saídas e StreamHub Chat abertos" width="100%">
</p>

O StreamHub adiciona três áreas principais ao OBS: **Múltiplas saídas · K4**, **StreamHub Chat · K4** e **Informações de transmissão K4**. O servidor local e o overlay acompanham a DLL, sem exigir que o usuário copie manualmente as pastas internas do projeto.

## O que você encontra

| Recurso | O que faz |
| --- | --- |
| **Múltiplas saídas** | Organiza a transmissão principal do OBS e destinos RTMP adicionais em cartões, com controles individuais ou ações para iniciar e parar tudo. |
| **Destinos** | Permite configurar Twitch, Kick, YouTube, TikTok, Facebook e RTMP personalizado. Servidor e chave ficam ocultos por padrão. |
| **Chat unificado** | Reúne mensagens por plataforma, permite filtrar e enviar mensagens, e evita repetir no dock a mesma mensagem enviada para vários chats. |
| **Moderação** | Oferece ações disponíveis conforme a plataforma e as permissões autorizadas da conta. |
| **Informações da transmissão** | Conecta contas e organiza título, categoria, tags, idioma, descrição e outras opções suportadas por cada plataforma. |
| **Overlay** | Exibe mensagens em uma fonte de navegador transparente do OBS, com duração, canal destacado e filtros configuráveis. |
| **K4 Noise Suppressor** | Inclui o filtro de redução de ruído baseado em RNNoise na DLL principal do plugin. |
| **Tema K4binho — Má Fase** | Aplica identidade visual, ícones e tema integrado ao OBS. |

## Capturas do projeto

### Contas e transmissão

<p align="center">
  <img src="docs/images/presentation/contas-conectadas.png" alt="Contas Twitch, Kick e YouTube conectadas no dock do StreamHub" width="49%">
  <img src="docs/images/presentation/editor-transmissao.png" alt="Editor de informações da transmissão do StreamHub" width="49%">
</p>

### Chat e overlay

<p align="center">
  <img src="docs/images/presentation/config-chat.png" alt="Configurações de conexão e estado do chat" width="49%">
  <img src="docs/images/presentation/config-overlay.png" alt="Opções de personalização do overlay" width="49%">
</p>

### Destinos RTMP, áudio e aparência

<p align="center">
  <img src="docs/images/presentation/adicionar-destino.png" alt="Seleção de plataformas para adicionar uma saída" width="49%">
  <img src="docs/images/presentation/supressor-ruido.png" alt="Controles do filtro K4 Noise Suppressor" width="49%">
</p>

<p align="center">
  <img src="docs/images/presentation/tema-obs.png" alt="Tema K4binho nas configurações de aparência do OBS" width="70%">
</p>

## Compatibilidade e estado dos conectores

O projeto oferece destinos RTMP para Twitch, Kick, YouTube, TikTok, Facebook e servidores personalizados. Isso configura a saída de vídeo; não significa que todas essas plataformas tenham integração de chat ou moderação.

| Plataforma | Saída RTMP | Chat e recursos da conta |
| --- | --- | --- |
| **Twitch** | Destino RTMP e sincronização de servidor/chave disponíveis. | Chat, envio e moderação conforme permissões concedidas. |
| **Kick** | Destino RTMP e fluxo OAuth/sincronização disponíveis. | Chat e ações dependem da autorização e dos recursos atualmente disponíveis na API. |
| **YouTube** | Destino RTMP e fluxo OAuth disponíveis. | O chat exige uma live com chat ativo; leitura, edição da live e envio precisam ser validados em uma transmissão real. |
| **TikTok** | Pode ser configurado como destino RTMP. | Integração de chat experimental. |
| **Facebook** | Pode ser configurado como destino RTMP. | O conector de chat não está implementado. |
| **RTMP personalizado** | Endereço e chave configuráveis pelo usuário. | Sem integração de chat fornecida pelo StreamHub. |

Os recursos das APIs podem variar por conta, permissões, região e estado da transmissão. A documentação técnica registra o que foi validado e o que ainda está pendente; consulte [Manual e build](docs/project/BUILD_AND_USAGE.md) e [Arquitetura e contexto](docs/project/ARCHITECTURE.md).

## Instalação

1. Baixe a DLL no [release mais recente](https://github.com/K4binho/StreamHub_ObsPlugin/releases/latest).
2. Feche o OBS antes de substituir o arquivo.
3. Copie a DLL para `obs-plugins/64bit` da instalação do OBS que você usa.
4. Abra o OBS e confira os docks no menu **Painéis**.

Os recursos do servidor local, ícones, tema e overlay são incorporados à DLL e extraídos pelo plugin quando necessário. Não substitua a DLL enquanto o OBS estiver aberto. Consulte o [guia completo de instalação e uso](docs/project/BUILD_AND_USAGE.md) para detalhes sobre permissões, contas, primeira execução e solução de problemas.

## Compilar no Windows

### Requisitos

- Windows 10 ou 11 de 64 bits;
- Visual Studio 2022 ou Build Tools com **Desenvolvimento para desktop com C++**;
- CMake 3.28 ou superior;
- Git.

### Comandos

Na pasta `plugins/obs`:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

A DLL é gerada em `plugins/obs/build_x64/RelWithDebInfo/obs-multi-rtmp.dll`. A compilação incorpora o RNNoise à DLL principal; não é necessário instalar uma segunda DLL do supressor.

## Como o repositório está organizado

```text
apps/
  local-server/       servidor local, conectores e overlay
  oauth-broker/       broker OAuth hospedado
plugins/obs/          plugin C++/Qt para OBS
libraries/            bibliotecas do projeto, incluindo o supressor
tests/                testes automatizados
third_party/          dependências externas
deployment/           arquivos de implantação
docs/                 manuais, arquitetura e imagens de apresentação
tools/                utilitários de build, atualização e publicação
```

O plugin OBS controla os docks e as saídas RTMP. O servidor local concentra conectores, chamadas às plataformas e tokens. O broker OAuth trata os fluxos que precisam de um retorno HTTPS público. Essa separação ajuda a manter cada parte com uma função definida; veja o [documento de modularização](docs/project/MODULARIZATION.md).

## Segurança e privacidade

- Não compartilhe nem publique stream keys, tokens ou arquivos privados de contas.
- Chaves RTMP ficam ocultas na interface por padrão.
- Client Secrets do fluxo OAuth compartilhado ficam no broker, fora da DLL.
- O servidor local escuta em loopback por padrão.
- Não envie arquivos `.env` ou `accounts-private.json` ao GitHub.

## Desenvolvimento e contribuição

Antes de alterar o projeto, confira `docs/project/ARCHITECTURE.md`, `docs/project/BUILD_AND_USAGE.md` e `docs/project/MODULARIZATION.md`. Mudanças no plugin devem preservar configurações existentes do OBS, opções avançadas de saída e arquivos privados do usuário.

Para relatar um erro, abra uma [issue](https://github.com/K4binho/StreamHub_ObsPlugin/issues) e inclua a versão do OBS, a versão do StreamHub e as etapas para reproduzir. Remova tokens, URLs de callback com códigos e chaves dos logs antes de anexá-los.

## Licença

Consulte a [licença do plugin](plugins/obs/LICENSE) e as licenças das dependências, como `third_party/rnnoise/COPYING`.
