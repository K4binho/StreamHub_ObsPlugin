# StreamHub OBS Plugin — Manual e Build

**Versão base:** OBS Studio 32.2.1+

StreamHub é fork de `obs-multi-rtmp`, com saídas RTMP nativas, chat unificado, contas, moderação, recompensas, editor de transmissão, overlay e tema K4binho — Má Fase.

## 1. Recursos

- **Múltiplas saídas · K4:** transmissão principal do OBS sempre aparece primeiro; destinos Twitch, YouTube, Kick, TikTok, Facebook ou RTMP personalizado aparecem abaixo.
- **Iniciar tudo:** inicia transmissão principal do OBS antes dos destinos selecionados.
- **Parar tudo:** encerra destinos auxiliares e transmissão principal.
- **StreamHub Chat · K4:** leitura por plataforma, filtros, envio, deduplicação e status.
- **ADM:** timeout, ban, unban, modo lento, seguidores, inscritos, emotes, recompensas e resgates conforme autorização.
- **Informações de transmissão K4:** contas, OAuth, prévia, título, categoria, tags, idioma e visibilidade conforme API.
- **Overlay:** fonte de navegador transparente em `http://127.0.0.1:3000/overlay.html`.
- **Node.js embutido:** runtime do sistema é usado quando disponível; caso contrário, plugin baixa runtime portátil e instala dependências.
- **Tema:** **K4binho — Má Fase**, ícones, branding e fundo 1920×1080.

Twitch está implementada. OAuth YouTube foi validado, mas consulta de transmissão ainda tem erro conhecido. Kick oficial, TikTok completo e chat Facebook permanecem pendentes.

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

## 4. Docks

### Múltiplas saídas · K4

1. Abra **Painéis > Múltiplas saídas · K4**.
2. Use **Adicionar destino**.
3. Escolha Twitch, Kick, YouTube, TikTok, Facebook ou RTMP personalizado.
4. Informe servidor RTMP e stream key.
5. Use **Configurações avançadas** para encoder, vídeo, áudio, `outputParam`, sincronização e opções disponíveis.
6. Marque destinos desejados.
7. Use **Iniciar tudo** ou controles individuais.

Servidor e stream key ficam ocultos por padrão. Use **Ver/Ocultar** e **Copiar** com cuidado.

Sincronização automática por OAuth ainda não está disponível. Até implementação da ponte Node/C++, configurar destinos manualmente.

### StreamHub Chat · K4

1. Abra **Painéis > StreamHub Chat · K4**.
2. Use engrenagem para canais e overlay.
3. Selecione **Todos**, Twitch, Kick, YouTube ou outra plataforma disponível.
4. Envie mensagem pelo campo inferior.
5. Use **ADM** para moderação e recompensas autorizadas.

Em **Todos**, mensagem local aparece uma vez com selo **Todos**. Ecos retornados por conectores são suprimidos durante janela de deduplicação.

### Informações de transmissão K4

1. Abra **Painéis > Informações de transmissão K4**.
2. Em **Contas**, conecte Twitch ou YouTube conforme configuração OAuth.
3. Em **Transmissão**, carregue dados atuais, busque categoria e edite campos disponíveis.
4. Salve alterações.

OAuth YouTube validado no navegador externo e callback local. Fluxo de carregamento ainda falha com `Parâmetros incompatíveis especificados na solicitação: mine, broadcastStatus`; correção está no roadmap.

## 5. Overlay

1. Abra engrenagem de **StreamHub Chat · K4**.
2. Selecione aba **Overlay**.
3. Ajuste duração, canal para menções e filtros de comandos `!`.
4. Clique **Copiar URL**.
5. No OBS, adicione **Fonte de navegador**.
6. Cole URL e escolha dimensão, por exemplo 540×900.

URL padrão:

```text
http://127.0.0.1:3000/overlay.html
```

Parâmetros temporários podem ajustar fonte:

```text
?duration=30&channel=k4binho&mentions=1
```

Overlay usa fundo transparente, ícone/cor por plataforma, badges, destaque de menções e expiração automática. Estados normais online/offline não aparecem sobre a transmissão.

## 6. Contas e APIs

### YouTube

1. Crie projeto no Google Cloud.
2. Ative **YouTube Data API v3**.
3. Configure Google Auth Platform com público **Externo**.
4. Adicione usuários de teste durante desenvolvimento.
5. Adicione escopo `https://www.googleapis.com/auth/youtube.force-ssl`.
6. Crie cliente OAuth **Aplicativo para computador**.
7. Informe Client ID e Client Secret em **Informações de transmissão K4 > Contas**.
8. Clique **Conectar** e autorize no navegador.

Tokens ficam em armazenamento privado local. Não compartilhe URL de callback contendo `code=`. Aplicativo em teste pode exigir nova autorização; distribuição pública pode exigir verificação Google.

### Twitch

Twitch usa autorização própria do plugin. Leitura, envio, dados da live, moderação e recompensas dependem de conta e escopos autorizados.

Mensagem antiga `Twitch: Atualizada; notificação não existem na API da Twitch.` indica campo inexistente e será corrigida para resultado ignorado, não erro.

### Kick

Leitura atual de chat pode funcionar pelo protocolo usado pelo site. OAuth oficial, envio autenticado, moderação e `streamkey:read` ainda estão pendentes.

Roadmap usa navegador externo, callback local, PKCE, refresh token e escopos oficiais disponíveis no aplicativo Kick. Não informe stream key por chat ou URL.

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
