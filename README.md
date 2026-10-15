# StreamHub OBS Plugin

Plugin nativo para OBS Studio que reúne multistream RTMP, chat, contas, edição de informações da transmissão e ferramentas do canal em docks Qt. O projeto é mantido principalmente por K4binho e parte da base do [`obs-multi-rtmp`](https://github.com/sorayuki/obs-multi-rtmp).

## Em poucas palavras

O StreamHub adiciona ao OBS:

- múltiplas saídas RTMP em uma única tela;
- chat unificado com filtros, envio e deduplicação;
- conexão de contas e sincronização de dados da live;
- moderação e recompensas, quando a plataforma e a autorização permitem;
- overlay transparente para fonte de navegador;
- servidor Node.js local gerenciado pelo plugin;
- tema K4binho — Má Fase;
- supressor de ruído RNNoise dentro da mesma DLL principal.

## O que está disponível

| Área | Situação |
| --- | --- |
| Saídas RTMP múltiplas | Disponível para destinos configuráveis |
| Twitch | Chat, dados da live, moderação e recompensas integrados |
| Kick | Chat existente e sincronização nativa preliminar |
| YouTube | OAuth, renovação, descoberta e sincronização nativa integrados |
| TikTok | Experimental |
| Facebook | Destino RTMP; chat ainda não integrado |
| Supressor RNNoise | Integrado à `obs-multi-rtmp.dll` |
| Overlay | Disponível em `http://localhost:605/overlay.html` |

Alguns recursos dependem de contas reais e de permissões da plataforma. Chat YouTube, edição completa da live, transmissão RTMP ponta a ponta e validação de encerramento anormal do OBS continuam pendentes de validação real. Um destino sem servidor ou stream key válidos fica **Pendente** e não é iniciado.

## Instalação rápida

1. Baixe uma DLL compilada ou compile o projeto.
2. Feche o OBS.
3. Copie `obs-multi-rtmp.dll` para `obs-plugins\\64bit` da instalação do OBS.
4. Abra o OBS e aguarde a primeira extração dos recursos e a preparação do Node.js.
5. Em **Painéis**, confirme:
   - **Múltiplas saídas · K4**;
   - **StreamHub Chat · K4**;
   - **Informações de transmissão K4**.

Quando a DLL contém o bundle, não é necessário copiar a pasta `data/` manualmente. O plugin preserva `config.json`, tokens, configurações desconhecidas e `node_modules` existentes.

## Como usar

### Múltiplas saídas

Abra **Painéis → Múltiplas saídas · K4**, adicione um destino, preencha servidor RTMP e stream key e use **Iniciar tudo** ou o controle individual. A transmissão principal do OBS aparece como o primeiro cartão e os destinos auxiliares ficam abaixo dela.

### Contas e informações da live

Abra **Painéis → Informações de transmissão K4**. Na aba **Contas**, conecte Twitch, Kick ou YouTube. Na aba **Transmissão**, use **Carregar atuais**, edite os campos suportados e aplique as alterações.

O fluxo normal de Kick e YouTube usa OAuth pelo navegador quando `STREAMHUB_OAUTH_BROKER_URL` aponta para um broker HTTPS configurado. O Client Secret permanece fora da DLL. A seção **Avançado** e o `.env` são alternativas para desenvolvimento e fallback local.

### Chat e overlay

Abra **Painéis → StreamHub Chat · K4** para filtrar por plataforma, enviar mensagens e acessar o painel ADM. Na engrenagem do chat, copie a URL do overlay e adicione-a ao OBS como **Fonte de navegador**:

```text
http://localhost:605/overlay.html
```

### Supressor de ruído

Adicione **StreamHub — Supressor de ruído (RNNoise)** aos filtros de áudio da fonte do microfone. Comece pelo preset **Equilibrado**. O filtro oferece redução RNNoise, gate adaptativo, remoção de graves, ganho, limitador e bypass.

O processamento usa quadros de 480 amostras a 48 kHz. Outras taxas passam pelo resamplador do OBS e podem adicionar latência. Não instale nem empilhe uma segunda DLL RNNoise. O identificador `obs_noise_suppressor_filter` foi preservado para manter filtros já salvos no OBS.

## Build Windows

### Requisitos

- Windows x64;
- Visual Studio 2022 ou Build Tools com **Desenvolvimento para desktop com C++**;
- CMake 3.28 ou mais recente;
- Git.

Node.js não é obrigatório: o plugin usa o Node do PATH ou baixa um runtime portátil na primeira execução. O projeto usa OBS Studio 32.2.1, Qt6 e `Qt6::Network`; QtWebSockets não é necessário.

### Compilar

Na raiz do repositório:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

DLL gerada:

```text
build_x64\RelWithDebInfo\obs-multi-rtmp.dll
```

Teste do processamento de áudio:

```powershell
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

Verificação sintática do servidor:

```powershell
Get-ChildItem data/streamhub-server,oauth-broker -Recurse -Filter *.js |
  ForEach-Object { node --check $_.FullName }
```

## Arquitetura

O plugin mantém responsabilidades separadas:

- `src/app`: entrada da DLL e inicialização do OBS;
- `src/core`: paths, bundle, launcher, Node, updater e utilitários;
- `src/streaming`: saídas RTMP, protocolos e widgets de destino;
- `src/chat`: chat, configurações, moderação e recompensas;
- `src/broadcast`: contas, OAuth, prévia e edição da transmissão;
- `src/ui`: tema e integração visual;
- `src/audio`: DSP e filtro RNNoise;
- `data/streamhub-server`: servidor local, APIs e frontend do overlay;
- `oauth-broker`: serviço OAuth externo opcional;
- `tests`: testes automatizados;
- `docs`: arquitetura, manual e documentação específica.

Node gerencia APIs, OAuth e credenciais. C++ gerencia a DLL, os docks Qt e as saídas RTMP nativas. O servidor local escuta em `localhost:605` por padrão, possui lifecycle autenticado por token e watchdog que acompanha a instância correta do OBS.

## Documentação

- [Arquitetura, regras e roadmap](docs/project/ARCHITECTURE.md)
- [Manual de uso, build, instalação e operação](docs/project/BUILD_AND_USAGE.md)
- [Detalhes do supressor RNNoise](docs/audio/noise-suppressor.md)
- [Documentação do servidor Node](data/streamhub-server/README.md)
- [Build Flatpak](FLATPAK.md)
- [Instalação do tema](themes/INSTALACAO.md)

## Segurança

Nunca publique Client Secret, access token, refresh token, stream key ou URL OAuth contendo `code=`. Segredos não devem aparecer em logs, screenshots, Markdown, URLs, eventos comuns, commits ou releases. Uma Client Secret exposta deve ser revogada e substituída.

## Licenças e créditos

- Projeto principal: [GPL-2.0](LICENSE) · [tradução PT-BR](LICENSE.pt-BR)
- RNNoise incorporado: [aviso de licença](licenses/RNNOISE.txt)
- Desenvolvimento e manutenção: K4binho
- Apoio ao projeto: [LivePix](https://livepix.gg/k4binho)
- Referência técnica: [`obs-multi-rtmp`](https://github.com/sorayuki/obs-multi-rtmp)

A tradução PT-BR da GPL é não oficial. Em caso de divergência, `LICENSE` contém a versão juridicamente válida.
