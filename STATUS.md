# StreamHub — estado e diário de bordo

Atualizado em 05/09/2026 a partir do diário fornecido pelo usuário, histórico posterior e leitura do código. Testes em OBS aqui registrados foram relatados no histórico; não foram repetidos nesta atualização documental.

## DLL autocontida (implementado nesta sessão, ainda não compilado/testado)

Implementado o que você pediu: só copiar `obs-multi-rtmp.dll` depois de formatar, sem passos manuais.

- **Servidor Node, painel/overlay e locale mínimo (en-US + pt-BR) agora são embutidos na própria DLL** via Qt Resources (`qrc/streamhub-data.qrc`), compilados dentro do binário pelo AUTORCC do Qt. Não é mais preciso copiar a pasta `data/` à parte.
- Nova função `StreamHub_EnsureBundledData()` (`src/streamhub-bundle.cpp`) roda logo no início de `obs_module_load()`, antes de qualquer leitura de locale: recria a pasta `data/obs-plugins/obs-multi-rtmp/` inteira a partir do que está embutido, se ainda não existir ou se a versão embutida mudou. **Nunca sobrescreve** `streamhub-server/config.json` nem `node_modules/` do usuário.
- **Removida a dependência do módulo Qt6 WebSockets** (o que exigia clonar/compilar `qtwebsockets` manualmente). O dock "StreamHub Chat" trocou de WebSocket para **long-polling HTTP** (`GET /api/chat/poll?since=N`, usando `QNetworkAccessManager`/Qt6::Network — módulo que já vem no pacote Qt do OBS). Latência parecida (servidor segura a resposta até ~25s ou até ter mensagem nova).
- `CMakeLists.txt` não pede mais `Qt6::WebSockets`; passa a linkar só `Core Widgets Network`.

Consequência prática: **não é mais preciso** compilar QtWebSockets nem copiar `Qt6WebSockets_relwithdebinfo.dll` para o OBS. Isso elimina os problemas #3 e #5 da lista abaixo para builds futuros.

**Pendente de verificação (ainda não compilado neste ambiente, que não tem Windows/Visual Studio/Qt):**
- [ ] Confirmar que `Qt6::Network` está de fato incluído no pacote `obs-deps-qt6-2026-07-15-x64` (é um módulo bem mais "core" que WebSockets, mas precisa compilar pra ter certeza).
- [ ] Compilar com o CMakeLists.txt atualizado e checar se o AUTORCC gera o `.rcc` a partir de `qrc/streamhub-data.qrc` sem erro.
- [ ] Testar em uma instalação de OBS **nova** (sem a pasta `data/obs-plugins/obs-multi-rtmp` pré-existente) copiando só a DLL, e confirmar que a pasta é recriada sozinha.
- [ ] Repetir os testes de chat Twitch/Kick com o novo mecanismo de long-poll.
- [ ] Confirmar que reabrir o OBS não perde `config.json` (o marcador de versão do bundle, `.streamhub-bundle-version`, deve impedir reextração desnecessária).

## Decisão vigente

Transmissão pelo painel **Múltiplas saídas nativo**, compartilhando encoder quando compatível; **Node para chats/overlay**. Usuário confirmou essa direção. Desativação do relay e formulário ainda pendentes: [PLANO_STREAMHUB.md](PLANO_STREAMHUB.md).

Não duplicar chaves RTMP no chat. A explicação anterior de que relay local economizaria upload estava incorreta: ambos enviam uma cópia pela internet por destino.

## Ambiente

- OBS portátil: `E:\obs-studio`.
- Projeto do build bem-sucedido: `C:\Users\heinr\Downloads\Streaming\Config OBS + PLugins\streamhub-obs-plugin`.
- Projeto atual: `E:\Streaming\Config OBS + PLugins\streamhub-obs-plugin`.
- Visual Studio Community 2026; CMake 4.4.3; VS Code com C/C++ e CMake Tools (Microsoft).
- Qt 6.11.1 do pacote `obs-deps-qt6-2026-07-15-x64`; QtWebSockets 6.11.1 compilado manualmente dentro desse pacote.
- DLLs em `E:\obs-studio\obs-plugins\64bit\`: `obs-multi-rtmp.dll` e `Qt6WebSockets_relwithdebinfo.dll`.
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

- [ ] Impedir inicialização do relay pelo plugin, inclusive com configuração antiga contendo `rtmp`.
- [ ] Criar formulário Qt de canais com Salvar e aplicar, sem edição manual de JSON.
- [ ] Reiniciar apenas o serviço de chat de forma assíncrona para aplicar mudanças, sem parar a transmissão.
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

O config histórico também tinha chaves/destinos do relay; isso não comprova transmissão funcionando. O código atual inicia relay quando existe `config.rtmp`; a separação escolhida ainda será implementada.

Até implementar Salvar e aplicar, o servidor lê configuração apenas ao iniciar; o fluxo atual exige fechar/abrir OBS para mudanças de chat. Não apresentar o botão como existente.

Se `.deps` for removida/recriada ou Qt atualizado, reconstruir QtWebSockets compatível e instalar a DLL exigida. Um configure comum não significa apagar sempre as dependências. Preservar o necessário antes de limpar builds.

As orientações antigas de usar VS2022, instalar por padrão em ProgramData, testar `rtmp.destinations` como fluxo principal e trocar novamente as chaves estão superadas neste trabalho. Detalhes operacionais: [BUILD_STREAMHUB.md](BUILD_STREAMHUB.md).

