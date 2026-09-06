# StreamHub — estado e diário de bordo

Atualizado em 05/09/2026 a partir do diário fornecido pelo usuário, histórico posterior e leitura do código. Testes em OBS aqui registrados foram relatados no histórico; não foram repetidos nesta atualização documental.

## Frontend nativo inicial — 06/09/2026

- O dock **StreamHub Chat** ganhou cabeçalho com estado Online/Offline, botão de configuração, filtros Todos/Twitch/Kick/YouTube/TikTok, mensagens estruturadas com hora, selo da plataforma, usuário colorido e indicação de modo leitura.
- O botão de configuração abre um formulário Qt para ativar e informar os canais da Twitch e Kick. O salvamento é atômico, preserva as demais seções do JSON e reinicia somente o Node para aplicar, sem reiniciar o OBS nem interferir nas saídas nativas.
- O dock **Múltiplas saídas** recebeu o mesmo tema escuro, cabeçalho, botão Adicionar destino, ações globais destacadas e cartões visuais para os destinos. As ações continuam ligadas ao código nativo já existente do `obs-multi-rtmp`.
- Build `RelWithDebInfo` concluído e DLL instalada em `E:\obs-studio\obs-plugins\64bit\obs-multi-rtmp.dll`, com backup da versão anterior. O hash da origem e do arquivo instalado é idêntico.
- O log `2026-09-06 00-35-36.txt` confirma plugin carregado, Node iniciado pelo caminho absoluto e dock conectado à porta 3000 às 00:36:04.
- [ ] Fazer validação visual dos docks e do formulário no OBS.
- [ ] Repetir mensagens Twitch/Kick após esta mudança de interface.
- [ ] Criar/configurar os destinos do perfil ativo e testar transmissão real; o log desta abertura mostra zero destinos nativos carregados.

### Refinamento visual instalado — 06/09/2026

- Corrigida a barra duplicada: `obs_frontend_add_dock_by_id()` já cria o `QDockWidget`, então `StreamHubChatDock` passou a ser apenas um `QWidget` de conteúdo.
- Letras provisórias foram substituídas por SVGs das plataformas nos filtros e nas mensagens; a engrenagem também passou a usar SVG embutido na DLL.
- O formulário agora cobre todos os conectores existentes: Twitch, Kick, YouTube (API key + ID da live) e TikTok (usuário). Facebook continua sem conector e não é anunciado como funcional.
- DLL instalada e OBS reaberto. O log `2026-09-06 00-48-46.txt` confirma Node iniciado pelo caminho absoluto e dock conectado à porta 3000 às 00:49:15.

## Correção de salvamento e créditos — 06/09/2026

- A janela de configuração mantinha `config.json` aberto para leitura durante todo o diálogo. No Windows, esse handle impedia o `QSaveFile` de substituir o arquivo no commit atômico. A leitura agora é encerrada antes da janela ser exibida.
- Se `config.json` não existir, a própria interface cria a pasta e copia `config.example.json` antes de abrir os campos. Erros de gravação agora mostram o motivo retornado pelo sistema.
- O painel Múltiplas saídas identifica o projeto como gratuito e credita SoraYuki pelo projeto original e K4binho pelas melhorias StreamHub, com links de apoio para PayPal e LivePix respectivamente.
- O README principal registra as alterações do fork: visual, chat unificado, configuração nativa, DLL autocontida e correções de instalação/inicialização.
- Correção compilada e instalada no OBS portátil. O arquivo instalado tem o mesmo SHA-256 da DLL gerada; falta somente confirmar o botão **Salvar e aplicar** pela interface.
- O cabeçalho do chat passa a exibir **Donate** abaixo da engrenagem, apontando para o LivePix do K4binho. Uma faixa no rodapé dá acesso às redes Twitch, Kick e YouTube do K4binho.

## Redesign de Múltiplas saídas — 06/09/2026

- Paleta aplicada: fundo `#080c14`, superfície `#101a2a`, hover `#15233a`, borda `#29496f`, azul `#00c8ff/#0077ff`, texto `#f2f7ff`, muted `#9eb2cb`, sucesso `#16d86a` e perigo `#d94155`.
- **Adicionar novo destino** abre um seletor visual para Twitch, Kick, YouTube, TikTok, Facebook ou RTMP personalizado. O modelo salva um campo `platform` novo; configurações antigas continuam compatíveis e têm a plataforma inferida pelo nome/servidor.
- Twitch, YouTube e Facebook recebem seus servidores públicos conhecidos. Kick e TikTok deixam o servidor vazio para receber o endereço fornecido pelo painel da conta.
- Cada cartão mostra ícone, nome, estado, qualidade, interruptor de iniciar/parar e engrenagem. Destinos incompletos não iniciam e orientam a preencher servidor e stream key.
- A engrenagem abre abaixo da lista um painel para nome, servidor, chave e sincronização com o OBS. Configurações avançadas de encoder/áudio continuam disponíveis e a janela antiga recebeu a mesma paleta.
- A textura enviada pelo usuário é usada no fundo do painel; a coroa K4binho aparece com baixa opacidade no canto. As imagens ficam embutidas na DLL e em cache durante a pintura.
- O aviso gratuito e os links de apoio a SoraYuki e K4binho permanecem no rodapé.
- Build `RelWithDebInfo` concluído, DLL instalada com SHA-256 conferido e OBS carregado. O log `2026-09-06 10-20-20.txt` confirma dois destinos nativos carregados e nenhum erro do módulo.
- Após o primeiro teste visual, a altura calculada da lista ganhou folga para preservar a borda inferior do último cartão. Estados longos foram reduzidos para **Pendente/Pronto**, mantendo a explicação completa no tooltip, para não cortar letras no dock estreito. A DLL com esse ajuste foi instalada no OBS portátil e conferida pelo SHA-256 `98572B9C9854EC54911A595EC743E5DD5788DEF5F7C79179D3EE5CEB92279CC7`.
- O refinamento seguinte aumentou a altura reservada dos cartões e botões globais. O rodapé gratuito/doações ficou fixo fora da área rolável. O painel de destino ganhou botão de fechar, confirmação após salvar e servidor RTMP/stream key ocultos por padrão, cada um com ações **Ver/Ocultar** e **Copiar**. Ativar um destino incompleto agora mostra um alerta orientando a configurá-lo.
- Os interruptores passaram a selecionar persistentemente quais destinos estão ativos, usando a cor de cada plataforma. Eles não tentam mais iniciar um codificador com a live principal parada. **Iniciar tudo** solicita primeiro o início da transmissão padrão do OBS; as saídas marcadas começam somente após o evento `STREAMING_STARTED`. **Parar tudo** encerra as saídas auxiliares e a transmissão principal.
- A confirmação de salvamento deixou de bloquear o OBS: agora aparece dentro do painel por 2,8 segundos. O dock de chat foi alinhado à mesma paleta azul do painel de saídas.
- [ ] Validar visualmente seletor, cartões, painel expansível e janela avançada na largura usada pelo usuário.
- [ ] Testar iniciar/parar de verdade; transmissão continua sem validação real.

## Correção do caminho — 06/09/2026

- O log `2026-09-06 00-00-08.txt` confirma a extração de **18 arquivos embutidos**. Essa etapa está validada.
- Os logs de 00:00:40, 00:01:35 e 00:03:19 mostram `MODULE_NOT_FOUND` com `data/obs-plugins/data/obs-plugins`: o script relativo era reinterpretado após o processo Node mudar de diretório.
- Correção em `src/streamhub-paths.h`, `src/obs-multi-rtmp.cpp` e `src/streamhub-launcher.cpp`: converter dados/servidor para caminhos absolutos no diretório de trabalho do OBS, antes da extração e da criação dos processos. Passar `server/index.js` como caminho absoluto. Não usar a pasta da DLL como base para o caminho relativo informado pelo OBS.
- Compilação `RelWithDebInfo` concluída neste Windows com VS2026/Qt. DLL gerada em `build_x64/RelWithDebInfo/obs-multi-rtmp.dll`.
- Teste `tests/paths` passou: reproduz a falha antiga e valida caminhos relativos/absolutos, espaços e mudança de diretório do processo filho.
- Preservados os `#include "obs.h"` no bundle, dock e launcher. A correção, o novo header e os testes foram incorporados aos commits posteriores da `main`.
- A DLL atual foi instalada com o OBS fechado. Os chats Twitch/Kick já foram validados em execução; a transmissão continua não testada.

## Reparação de instalação interrompida — 06/09/2026

- O log de 00:11:47 prova que o caminho absoluto já está correto.
- A falha seguinte, `Cannot find module './writer'` dentro de `protobufjs`, veio de `node_modules` incompleto. O OBS de 00:00:08 foi fechado durante o primeiro `npm install`.
- O launcher não considera mais apenas a existência de `node_modules`. Depois de um `npm install` concluído, grava `.dependencies-sha256` com o hash do `package.json`. Pasta sem marcador ou com versão diferente é reparada automaticamente por outro `npm install`.
- O marcador usa gravação atômica, portanto um novo fechamento durante a instalação continuará sendo detectado na abertura seguinte.
- DLL recompilada e instalada em `E:\obs-studio\obs-plugins\64bit\obs-multi-rtmp.dll`. Backup da anterior: `obs-multi-rtmp.dll.backup-20260906-002223`.
- O log `2026-09-06 00-24-35.txt` confirma: bundle atualizado de 1 para 2, 17 arquivos atuais extraídos, dependências detectadas como incompletas/desatualizadas, `npm install` concluído, Node iniciado pelo caminho absoluto e dock conectado à porta 3000 às 00:24:57.
- Os avisos `npm warn allow-scripts` não impediram a instalação nem a inicialização.
- `config.json` foi restaurado com Twitch e Kick habilitados no canal `K4binho`; YouTube/TikTok desabilitados e sem seção de relay.
- [x] Validação final da compilação corrigida: o usuário enviou `oi` na Twitch e na Kick, e ambas apareceram no dock como `[TWITCH] k4binho: oi` e `[KICK] K4binho: oi`.
- [x] Fluxo confirmado nesta máquina: DLL autocontida → extração do bundle → reparação automática do npm → Node com caminhos absolutos → long-poll HTTP → chats Twitch/Kick no dock.
- Transmissão continua não testada.

## DLL autocontida (compilada; extração e chats validados)

Implementado o que você pediu: só copiar `obs-multi-rtmp.dll` depois de formatar, sem passos manuais.

- **Servidor Node, painel/overlay e locale mínimo (en-US + pt-BR) agora são embutidos na própria DLL** via Qt Resources (`qrc/streamhub-data.qrc`), compilados dentro do binário pelo AUTORCC do Qt. Não é mais preciso copiar a pasta `data/` à parte.
- Nova função `StreamHub_EnsureBundledData()` (`src/streamhub-bundle.cpp`) roda logo no início de `obs_module_load()`, antes de qualquer leitura de locale: recria a pasta `data/obs-plugins/obs-multi-rtmp/` inteira a partir do que está embutido, se ainda não existir ou se a versão embutida mudou. **Nunca sobrescreve** `streamhub-server/config.json` nem `node_modules/` do usuário.
- **Removida a dependência do módulo Qt6 WebSockets** (o que exigia clonar/compilar `qtwebsockets` manualmente). O dock "StreamHub Chat" trocou de WebSocket para **long-polling HTTP** (`GET /api/chat/poll?since=N`, usando `QNetworkAccessManager`/Qt6::Network — módulo que já vem no pacote Qt do OBS). Latência parecida (servidor segura a resposta até ~25s ou até ter mensagem nova).
- `CMakeLists.txt` não pede mais `Qt6::WebSockets`; passa a linkar só `Core Widgets Network`.

Consequência prática: **não é mais preciso** compilar QtWebSockets nem copiar `Qt6WebSockets_relwithdebinfo.dll` para o OBS. Isso elimina os problemas #3 e #5 da lista abaixo para builds futuros.

**Verificação atual:**
- [x] Compilação com `Qt6::Network` e recursos embutidos concluída em 06/09/2026.
- [x] Extração de 18 arquivos confirmada no log fornecido pelo usuário.
- [ ] Validar o fluxo completo em instalação limpa (incluindo preparação automática e chats).
- [x] Repetir os testes de chat Twitch/Kick com o novo mecanismo de long-poll.
- [ ] Confirmar que reabrir o OBS não perde `config.json` (o marcador de versão do bundle, `.streamhub-bundle-version`, deve impedir reextração desnecessária).

## Decisão vigente

Transmissão pelo painel **Múltiplas saídas nativo**, compartilhando encoder quando compatível; **Node para chats/overlay**. Usuário confirmou essa direção. O relay foi retirado da inicialização e o primeiro formulário nativo de chat já está implementado: [PLANO_STREAMHUB.md](PLANO_STREAMHUB.md).

Não duplicar chaves RTMP no chat. A explicação anterior de que relay local economizaria upload estava incorreta: ambos enviam uma cópia pela internet por destino.

## Ambiente

- OBS portátil: `E:\obs-studio`.
- Projeto do build bem-sucedido: `C:\Users\heinr\Downloads\Streaming\Config OBS + PLugins\streamhub-obs-plugin`.
- Projeto atual: `E:\Streaming\Config OBS + PLugins\streamhub-obs-plugin`.
- Visual Studio Community 2026; CMake 4.4.3; VS Code com C/C++ e CMake Tools (Microsoft).
- Qt 6.11.1 do pacote `obs-deps-qt6-2026-07-15-x64`. QtWebSockets foi compilado manualmente no histórico, mas o plugin atual não depende mais dele.
- DLL necessária em `E:\obs-studio\obs-plugins\64bit\`: `obs-multi-rtmp.dll`. A antiga `Qt6WebSockets_relwithdebinfo.dll` pode permanecer no disco, mas não é carregada pelo StreamHub atual.
- Dados em `E:\obs-studio\data\obs-plugins\obs-multi-rtmp\`, com `streamhub-server` embutido.
- Chat configurado no histórico: Twitch e Kick habilitados, canal `K4binho`; YouTube e TikTok desabilitados. Nenhuma credencial deve ser registrada neste diário.

## Problemas resolvidos

1. **CMake não reconhecido no PowerShell:** reinstalado com PATH habilitado; aberto terminal novo.
2. **Visual Studio não encontrado:** preset mudou de `Visual Studio 17 2022` para `Visual Studio 18 2026`. O diário registra suporte a partir de CMake 4.2; versão utilizada foi 4.4.3.
3. **Qt6WebSockets ausente:** pacote Qt do OBS não incluía o módulo. Clonado código oficial de `https://github.com/qt/qtwebsockets.git`, tag `v6.11.1`; configurado com `qt-cmake.bat` do Qt baixado e instalado por `cmake --install ... --prefix` dentro do pacote Qt. Qt Online Installer não foi usado porque o e-mail de confirmação não chegou.
4. **LOG_WARNING / LOG_INFO / blog não declarados:** adicionado `#include "obs.h"` em `streamhub-launcher.cpp` e `streamhub-chat-dock.cpp`.
5. **Erro 126 / LoadLibrary failed:** faltava `Qt6WebSockets_relwithdebinfo.dll`; copiada para a pasta da DLL do plugin. O nome com sufixo corresponde a esse build.
6. **Caminho duplicado ao iniciar Node:** corrigida resolução relativa/absoluta do diretório de dados. Depois disso o servidor iniciou e o dock recebeu mensagens.

## Avanços posteriores ao diário inicial

O diário inicial ainda dizia que Node e dock não tinham sido confirmados. O histórico posterior substitui essas pendências:

- [x] Plugin compilado e carregando sem erro.
- [x] Dados e `config.json` instalados no OBS portátil.
- [x] Dock StreamHub Chat apareceu e conectou ao WebSocket local.
- [x] Servidor Node iniciou pelo plugin.
- [x] Chat da Twitch validado de ponta a ponta: “opa” e “oi” apareceram no dock, acompanhando o chat nativo.
- [x] Chat do Kick testado e funcionando, conforme esclarecimento mais recente do usuário.
- [x] Chaves Twitch/Kick trocadas conforme confirmação no histórico posterior. Não tratar rotação como ainda pendente.
- [x] Escolhido multistream nativo; painel Múltiplas saídas já utilizado no histórico.

A aplicação das chaves novas em todos os destinos nativos ainda precisa ser conferida, sem exibi-las. Não é necessário copiá-las para o chat.

## Pendências reais

**A transmissão não foi testada, inclusive na Twitch. Somente os chats da Twitch e do Kick foram testados.**

- [x] Impedir inicialização do relay pelo plugin, inclusive com configuração antiga contendo `rtmp`.
- [x] Criar formulário Qt de canais com Salvar e aplicar, sem edição manual de JSON.
- [x] Reiniciar apenas o serviço de chat de forma assíncrona para aplicar mudanças, sem parar a transmissão.
- [ ] Mostrar status de cada plataforma separado da conexão com Node.
- [ ] Após as alterações, repetir os testes dos chats Twitch/Kick para confirmar que continuam funcionando.
- [ ] Conferir destinos nativos, chaves atuais e compartilhamento dos encoders no perfil ativo.
- [ ] Testar multistream nativo Twitch/Kick com imagem/áudio reais e medir estabilidade.
- [ ] Reabrir OBS para confirmar uso de cache sem download/instalação repetidos.
- [ ] Distinguir teste de cache de primeira execução sem Node: download portátil e instalação do zero não estão comprovados pelo funcionamento do chat da Twitch.
- [ ] Recompilar em diretório novo: cache atual contém caminhos da pasta antiga em C:.
- [ ] YouTube/TikTok somente se forem utilizados, em etapa posterior.

## Informações para continuidade

Chat salva em `E:\obs-studio\data\obs-plugins\obs-multi-rtmp\streamhub-server\config.json`. As saídas nativas salvam em `obs-multi-rtmp.json` no **perfil ativo** retornado pelo OBS. O caminho AppData citado em sessão anterior não foi reconfirmado; não é correto dizer que todas as configurações estão só no config.json.

O config histórico também tinha chaves/destinos do relay; isso não comprova transmissão funcionando. O código atual ignora essa seção antiga e não inicia relay/FFmpeg.

O servidor lê configuração ao iniciar. O botão **Salvar e aplicar** agora grava o arquivo e reinicia somente o processo Node de forma assíncrona.

Se `.deps` for removida/recriada ou Qt atualizado, o build atual baixa as dependências Qt do OBS; não é necessário reconstruir QtWebSockets. Um configure comum não significa apagar sempre as dependências.

As orientações antigas de usar VS2022, instalar por padrão em ProgramData, testar `rtmp.destinations` como fluxo principal e trocar novamente as chaves estão superadas neste trabalho. Detalhes operacionais: [BUILD_STREAMHUB.md](BUILD_STREAMHUB.md).

