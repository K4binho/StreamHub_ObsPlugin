# StreamHub OBS Plugin — Manual e Build

**Versão base:** OBS Studio 32.2.1+
**Bundle StreamHub:** 40

StreamHub é fork de `obs-multi-rtmp`, com saídas RTMP nativas, chat unificado, contas, moderação, recompensas, editor de transmissão, overlay e tema K4binho — Má Fase.

## 1. Recursos

- **Múltiplas saídas · K4:** transmissão principal do OBS sempre aparece primeiro; destinos Twitch, YouTube, Kick, TikTok, Facebook ou RTMP personalizado aparecem abaixo.
- **Iniciar tudo:** inicia transmissão principal do OBS antes dos destinos sincronizados.
- **Ativação automática:** após conta conectada fornecer RTMP e stream key válidos, destino entra automaticamente em **Iniciar tudo**. Configuração de chat permanece independente.
- **Parar tudo:** encerra destinos auxiliares e transmissão principal.
- **StreamHub Chat · K4:** leitura por plataforma, filtros, envio, deduplicação e status.
- **ADM:** timeout, ban, unban, modo lento, seguidores, inscritos, emotes, recompensas e resgates conforme autorização.
- **Informações de transmissão K4:** contas, OAuth, prévia, título, categoria, tags, idioma e visibilidade conforme API.
- **Overlay:** fonte de navegador transparente em `http://localhost:605/overlay.html`.
- **Node.js embutido:** runtime do sistema é usado quando disponível; caso contrário, plugin baixa runtime portátil e instala dependências.
- **Tema:** **K4binho — Má Fase**, ícones, branding e fundo 1920×1080.

Twitch, sync RTMP/key Twitch, sync RTMP/key Kick e sync RTMP/key YouTube estão implementados e testados pelo canal interno. OAuth Kick e YouTube usam broker HTTPS compartilhado no fluxo normal; Client Secrets ficam no broker, fora da DLL. **Avançado** mantém OAuth local com credenciais manual ou `.env` como fallback. Chat YouTube, consulta, atualização da live e transmissão RTMP real ainda precisam de validação. TikTok completo e chat Facebook permanecem pendentes.

## 2. Build Windows

### Pré-requisitos

- Visual Studio Community 2026 com **Desenvolvimento para desktop com C++**.
- CMake 3.28+.
- Git.
- OBS Studio compatível com dependências do projeto.

Node.js não é pré-requisito. Plugin usa Node instalado no PATH ou baixa runtime portátil.

### Comandos

Execute na raiz do repositório:

```powershell
cmake --preset windows-x64
```

Compile:

```powershell
cmake --build --preset windows-x64 --config RelWithDebInfo
```

Preset usa OBS Studio 32.2.1, dependências pré-compiladas de 2026-07-15, Qt6 e gerador `Visual Studio 18 2026`. Build usa `Qt6::Core`, `Qt6::Widgets` e `Qt6::Network`; QtWebSockets não é necessário.

DLL costuma ser gerada em:

```text
build_x64\RelWithDebInfo\obs-multi-rtmp.dll
```

Nome final segue `buildspec.json`.

### Instalação da DLL

1. Feche OBS.
2. Copie `obs-multi-rtmp.dll` para a pasta `obs-plugins\64bit` da instalação OBS usada.
3. Não copie `data/` manualmente quando usar DLL com bundle embutido.
4. Abra OBS.
5. Procure linhas `[streamhub]` no log atual.
6. Confirme os três docks em **Painéis**.

Recursos são extraídos pela DLL para caminho gravável do OBS. `config.json`, tokens e `node_modules` existentes não devem ser sobrescritos.

## 3. Primeira execução

Plugin resolve Node nesta ordem:

1. Caminho salvo em `data/node-runtime/resolved-node-path.txt`.
2. Node disponível no PATH.
3. Runtime portátil oficial baixado para `data/node-runtime/`.

Depois, plugin valida `.dependencies-sha256` contra `package.json`. Marcador ausente ou desatualizado dispara `npm install` novamente. Não feche OBS durante primeira instalação.

Se instalação for interrompida, abra OBS novamente. Plugin detecta dependências incompletas e repara.

### Lifecycle local

Launcher inicia servidor em `localhost` e grava `runtime.json` no diretório gravável do plugin. Arquivo contém PID Node, PID OBS, porta e token da instância. Token não aparece em URL, log ou resposta pública.

Servidor expõe `GET /internal/status` e `POST /internal/shutdown` somente para loopback com header `X-StreamHub-Token` válido. Watchdog verifica PID OBS a cada 2 segundos. Ao fechar OBS, Node encerra conectores, long-polls, Socket.IO e HTTP antes de sair. Launcher tenta shutdown autenticado, espera encerramento, usa `terminate()` e reserva `kill()` para fallback.

Job Object Windows usa `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` para associar Node ao launcher. Encerramento anormal do OBS e limpeza por Job Object ainda precisam de validação ponta a ponta.

Porta padrão é `605`; configuração explícita em `config.json` continua respeitada. URL padrão do overlay:

```text
http://localhost:605/overlay.html
```

## 4. Docks

### Múltiplas saídas · K4

1. Abra **Painéis > Múltiplas saídas · K4**.
2. Use **Adicionar destino**.
3. Escolha Twitch, Kick, YouTube, TikTok, Facebook ou RTMP personalizado.
4. Informe servidor RTMP e stream key.
5. Use **Configurações avançadas** para encoder, vídeo, áudio, `outputParam`, sincronização e opções disponíveis.
6. Marque **Iniciar junto com a transmissão principal do OBS** no destino desejado.
7. Use **Iniciar tudo** ou controles individuais.

OAuth Twitch exige escopo `channel:read:stream_key`. Clique **Sincronizar** em **Contas** para buscar stream key via `GET https://api.twitch.tv/helix/streams/key`; servidor oficial usado é `rtmps://live.twitch.tv/app`. Destino sem servidor ou chave mostra **Pendente**, fica desligado e não inicia.

Servidor e stream key ficam ocultos por padrão. Use **Ver/Ocultar** e **Copiar** com cuidado.

Twitch: conecte ou reconecte conta para conceder `channel:read:stream_key`, depois clique **Sincronizar**. Se Twitch for transmissão principal do OBS, plugin não cria destino Twitch auxiliar. Se houver destino Twitch auxiliar legítimo, plugin atualiza destino existente sem duplicar. Sem servidor ou chave válida, cartão mostra **Pendente** e não inicia.

Kick: em **Contas**, clique **Conectar** e autorize na página oficial. Broker faz callback público HTTPS, troca o código e entrega autorização ao Node local por transação curta. **Avançado** mantém credenciais locais e callback loopback como fallback. Use **Sincronizar** separadamente para buscar dados Kick pelo canal interno autenticado e criar ou atualizar um único cartão Kick em **Múltiplas saídas · K4**. Atualização altera somente servidor e stream key; nome, ordem, encoders, áudio, vídeo e opções avançadas permanecem.

Se API não retornar valores oficiais válidos, destino permanece sem alteração e UI mostra falha. Não envie credenciais ou stream key por chat, URL ou documentação. Kick usa `streamkey:read`; resposta oficial fornece `channel.stream.url` e `channel.stream.key`. YouTube ainda precisa validação de transmissão RTMP real.

### StreamHub Chat · K4

1. Abra **Painéis > StreamHub Chat · K4**.
2. Use engrenagem para canais e overlay.
3. Selecione **Todos**, Twitch, Kick, YouTube ou outra plataforma disponível.
4. Envie mensagem pelo campo inferior.
5. Use **ADM** para moderação e recompensas autorizadas.

Em **Todos**, mensagem local aparece uma vez com selo **Todos**. Ecos retornados por conectores são suprimidos durante janela de deduplicação.

### Informações de transmissão K4

1. Abra **Painéis > Informações de transmissão K4**.
2. Em **Contas**, conecte Twitch, Kick e YouTube. Kick e YouTube usam **Conectar** e **Sincronizar**; **Avançado** fica reservado para fallback local.
3. Em **Transmissão**, carregue dados atuais, busque categoria e edite campos disponíveis.
4. Salve alterações.

YouTube usa OAuth compartilhado HTTPS no fluxo normal, navegador externo e escopo `https://www.googleapis.com/auth/youtube.force-ssl`. Broker mantém Client Secret. **Avançado** mantém credenciais locais e `.env` como fallback; valor manual preenchido vence `.env`. Após conexão, **Sincronizar** busca dados oficiais e atualiza/cria destino YouTube nativo sem duplicação. O conector de chat descobre `activeLiveChatId` por `videos.list` e lê mensagens por `liveChatMessages.list`; leitura real, consulta/edição da live e RTMP ainda precisam validação em live real.

## 5. Overlay

1. Abra engrenagem de **StreamHub Chat · K4**.
2. Selecione aba **Overlay**.
3. Ajuste duração, canal para menções e filtros de comandos `!`.
4. Clique **Copiar URL**.
5. No OBS, adicione **Fonte de navegador**.
6. Cole URL e escolha dimensão, por exemplo 540×900.

URL padrão:

```text
http://localhost:605/overlay.html
```

Parâmetros temporários podem ajustar fonte:

```text
?duration=30&channel=k4binho&mentions=1
```

Overlay usa fundo transparente, ícone/cor por plataforma, badges, destaque de menções e expiração automática. Estados normais online/offline não aparecem sobre a transmissão.

## 6. Contas e APIs

### YouTube

Fluxo normal não exige criação de app, cópia de Client ID, Client Secret, `.env` ou abertura de **Avançado**. O operador do broker registra aplicativo OAuth Google, callback HTTPS e mantém Client Secret no serviço. Usuário final clica **Conectar** e autoriza no navegador. Para desenvolvimento/operador, **Avançado** aceita credenciais locais e `.env` como fallback.

Para usar `.env`, copie `data/streamhub-server/.envexemplo` para `.env` no diretório runtime extraído pelo plugin. Use `KICK_CLIENT_ID`, `KICK_CLIENT_SECRET`, `YOUTUBE_CLIENT_ID` e `YOUTUBE_CLIENT_SECRET`. Não adicione `.env` ao QRC, Git ou documentação. Kick e YouTube podem conectar simultaneamente; falha ou sincronização de uma não altera outra. Falha de sincronização não desfaz estado OAuth conectado; aviso aparece separado no cartão da plataforma.

Tokens ficam em armazenamento privado local. Não compartilhe URL de callback contendo `code=`. Aplicativo em teste pode exigir nova autorização; distribuição pública pode exigir verificação Google.

### Twitch

Twitch usa autorização própria do plugin. Leitura, envio, dados da live, moderação e recompensas dependem de conta e escopos autorizados. Stream key usa escopo `channel:read:stream_key` e endpoint `GET https://api.twitch.tv/helix/streams/key`; servidor RTMP oficial é `rtmps://live.twitch.tv/app`.

Campos Twitch usam somente propriedades suportadas pela API da Twitch. Descrição, visibilidade e notificação não são enviados ao endpoint Twitch.

### Kick

Leitura atual de chat pode funcionar pelo protocolo usado pelo site. Fluxo normal usa broker OAuth HTTPS, callback público, PKCE, `state`, refresh token e armazenamento em `accounts-private.json`. **Avançado** mantém OAuth local, callback loopback e configuração de Client ID/Client Secret somente para fallback; nada vai para `config.json`.

Após OAuth, sincronização interna autenticada busca servidor e stream key e atualiza/cria cartão Kick sem duplicação. Kick usa escopo `streamkey:read`; API oficial fornece dados em `channel.stream.url` e `channel.stream.key`. Sync interno respondeu `HTTP 200`; transmissão RTMP real ainda pendente. Não informe stream key por chat ou URL.

### TikTok e Facebook

TikTok permanece experimental até teste ponta a ponta em live real. Facebook oferece destino RTMP, mas não conector de chat funcional no servidor.

## 7. Tema K4binho — Má Fase

### Instalação automática

1. Copie DLL e abra OBS.
2. Plugin extrai tema para diretório de temas do OBS.
3. Feche e abra OBS novamente; lista de temas é carregada antes dos plugins.
4. Acesse **Configurações → Aparência → Tema**.
5. Selecione **K4binho — Má Fase**.

### Instalação manual

1. Feche OBS.
2. Extraia conteúdo de `theme` para `data\obs-studio\themes`.
3. Confirme `data\obs-studio\themes\K4binho_Ma_Fase.obt`.
4. Confirme `data\obs-studio\themes\K4binho-Ma-Fase\crown-watermark.png`.
5. Abra OBS e selecione tema.

Fundo de cena:

```text
K4binho-Ma-Fase\backgrounds\ma-fase-background-1920x1080.png
```

Adicione como fonte **Imagem** e mova para o fim da lista de fontes.

## 8. Flatpak

Build Flatpak usa `flatpak/com.obsproject.Studio.Plugin.MultiRTMP.yml`, extensão `com.obsproject.Studio.Plugin.MultiRTMP`, runtime `com.obsproject.Studio//stable` e SDK `org.freedesktop.Sdk//25.08`.

### Preparar WSL openSUSE

```powershell
wsl -u root -e zypper --non-interactive install flatpak flatpak-builder git cmake ninja
```

Se `/etc/mtab` não existir:

```powershell
wsl -u root -e sh -lc 'test -e /etc/mtab || ln -s /proc/self/mounts /etc/mtab'
```

Adicionar Flathub e OBS:

```powershell
wsl -e sh -lc 'flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo'
wsl -e sh -lc 'flatpak --user install -y flathub com.obsproject.Studio'
```

### Build e instalação

Use `flatpak/build.sh` a partir do caminho do repositório no WSL:

```powershell
wsl -e bash /caminho/para/streamhub-obs-plugin/flatpak/build.sh
```

Instalar bundle:

```powershell
wsl -e flatpak --user install -y /caminho/para/streamhub-obs-plugin/release/obs-multi-rtmp.flatpak
```

Atualizar bundle:

```powershell
wsl -e flatpak --user install --reinstall -y /caminho/para/streamhub-obs-plugin/release/obs-multi-rtmp.flatpak
```

Verificar:

```powershell
wsl -e flatpak --user info com.obsproject.Studio
wsl -e flatpak --user info com.obsproject.Studio.Plugin.MultiRTMP
wsl -e flatpak --user list --runtime
```

Cache temporário fica em `~/.cache/obs-multi-rtmp-flatpak` dentro do WSL. Não colocar build Flatpak em `/mnt/c` ou `/mnt/d` quando `rofiles-fuse` exigir montagem nativa.

## 9. Diagnóstico

### Dock não aparece

1. Feche OBS.
2. Abra localização de configuração do OBS.
3. Faça backup de `global.ini`.
4. Remova somente entrada `DockState=` se layout estiver corrompido.
5. Abra OBS e confirme **Painéis**.

### Node não conecta

- Procure `[streamhub]` no log.
- Confirme que `config.json` existe ou deixe plugin criá-lo de `config.example.json`.
- Aguarde `npm install` terminar na primeira execução.
- Não reutilize `node_modules` sem `.dependencies-sha256` válido.
- Confirme que porta local não está ocupada.

### Build usa cache antigo

Configure em diretório limpo quando cache apontar para outra pasta:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

Não reutilize build com caminhos de outra máquina sem reconfigurar.

## 10. Segurança

Nunca compartilhe Client Secret, access token, refresh token, stream key ou URL OAuth com `code=`. Não grave segredo em log, Markdown, screenshot, URL, evento comum, Git ou push.

Client Secret exposta deve ser revogada e substituída. Stream key deve vir de configuração/API oficial; nunca inventar, inferir ou extrair de chat.

## 11. Apoio

- StreamHub e melhorias K4binho: [LivePix](https://livepix.gg/k4binho)
- Projeto original SoraYuki: [PayPal](https://paypal.me/sorayuki0)
