# Plano de execução — StreamHub nativo + chats

Data: 2026-09-05. Status atualizado em 07/09/2026: DLL autocontida, painéis nativos, Twitch OAuth, envio no chat, moderação, recompensas e editor de transmissão implementados. OAuth YouTube validado com conta de teste; carregamento/edição da live ainda precisa corrigir consulta incompatível da API. Permanecem pendentes sincronização automática de RTMP/stream key e criação de destinos em Múltiplas saídas, além de OAuth e escrita para Kick e TikTok e teste real de transmissão.

## Atualização do usuário — 07/09/2026

- OAuth YouTube concluiu no navegador externo e retornou à página local **YouTube conectado ao StreamHub**.
- O painel **Informações de transmissão K4** exibiu o erro: `YouTube: Parâmetros incompatíveis especificados na solicitação: mine, broadcastStatus`.
- O painel exibiu também: `Twitch: Atualizada; notificação não existem na API da Twitch.`. O campo de notificação deve deixar de ser tratado como erro/resultado enganoso.
- Requisito novo confirmado: ao conectar uma plataforma, **Servidor RTMP** e **Stream key** devem sincronizar automaticamente; destinos correspondentes devem ser adicionados em **Múltiplas saídas** mesmo quando nenhuma live ou saída estiver pré-configurada.
- A sincronização deve ocorrer após conexão da plataforma, não depender de live previamente ativa e preservar configurações avançadas, ordem, nomes personalizados e demais destinos já existentes. Segredos precisam permanecer protegidos, sem logs, screenshots ou arquivos versionados.

Pendência técnica: validar quais APIs autorizadas de cada plataforma fornecem servidor e stream key. OAuth YouTube não deve inventar ou expor stream key; quando a API não fornecer valor reutilizável, fluxo deve orientar configuração segura em vez de gravar valor falso.
- Kick deverá usar OAuth por navegador externo, callback local, PKCE, refresh token, `streamkey:read`, envio de chat e ações de moderação conforme escopos oficiais liberados.
- A sincronização Node/C++ deverá usar ponte local autenticada e temporária. C++ atualizará `GlobalMultiOutputConfig()` de forma idempotente, sem duplicar destinos ou sobrescrever nomes, ordem, encoders e configurações avançadas.
- A implementação aguarda aprovação deste plano; esta etapa altera somente documentação e contexto seguro.

Contexto consolidado: [contexto.md](contexto.md).

## Plano de ação para próxima etapa

1. Corrigir descoberta de transmissão YouTube com parâmetros aceitos e transformar notificação Twitch em campo ignorado, com testes mockados.
2. Integrar Kick OAuth, callback, renovação, status, chat autenticado e moderação, mantendo tokens em armazenamento privado.
3. Implementar leitura oficial de servidor RTMP/stream key Kick com `streamkey:read`; definir tratamento explícito para chave YouTube indisponível.
4. Criar ponte Node/C++ loopback autenticada em memória, sem chave em logs, URL, eventos comuns ou configuração pública.
5. Criar sincronização nativa idempotente: localizar por plataforma, criar destino ausente, atualizar somente valores oficiais e preservar todos os demais campos.
6. Atualizar UI/status para sincronização concluída, pendente, indisponível ou falha, sempre mascarando segredos.
7. Testar regressão, segurança de credenciais, compatibilidade de configurações antigas, criação sem live/destino prévio e transmissão real.

Nenhum código deve ser alterado antes da confirmação do usuário após o checkpoint documental e deste plano.

Contexto detalhado: [contexto.md](contexto.md).



Atualização de execução (06/09/2026): DLL autocontida instalada; extração, reparação do npm, caminho absoluto e chats Twitch/Kick validados. O frontend nativo inicial de chat/configuração e o novo visual de Múltiplas saídas foram compilados e instalados; o log confirma nova conexão com o Node. Falta validação visual/regressão dos chats e a transmissão continua sem teste. Detalhes em [STATUS.md](STATUS.md).

Atualização visual: Múltiplas saídas agora possui modelos por plataforma, cartões com ícones e interruptores, configuração básica expansível e janela avançada recolorida. Fundo e coroa usam os materiais enviados pelo usuário com opacidade reduzida. A transmissão real permanece pendente.

Histórico consolidado em [STATUS.md](STATUS.md) e instruções corrigidas em [BUILD_STREAMHUB.md](BUILD_STREAMHUB.md). OBS portátil confirmado pelo usuário em `E:\obs-studio`; VS Community 2026, CMake 4.4.3 e QtWebSockets 6.11.1 compilado manualmente. Os chats da Twitch e do Kick já foram testados; a transmissão ainda não foi testada, inclusive na Twitch e a rotação das chaves já foi relatada como concluída. Falta verificar as chaves atuais nos destinos nativos, sem duplicá-las no chat.

## Resultado esperado

Transmitir para Twitch e Kick pelas saídas nativas do OBS, compartilhando o encoder quando compatível. Configurar os chats pelo dock StreamHub, sem editar JSON e sem cadastrar chaves RTMP no servidor de chat. Salvar e aplicar canais sem fechar o OBS ou interromper a transmissão.

YouTube, Kick e TikTok completos exigem autenticação própria. Twitch e YouTube já enviam pelo fluxo Todos sem duplicar a linha exibida no chat. Kick e TikTok aguardam OAuth oficial. Preservar as configurações existentes.

## Interface vigente

- **Múltiplas saídas · K4:** primeiro cartão reservado à transmissão principal do OBS; cartões adicionais abaixo.
- **Informações de transmissão K4:** somente contas e edição da live, com busca visual de categoria e prévia.
- **StreamHub Chat · K4:** leitura e envio no mesmo dock; a engrenagem configura conectores e overlay; **ADM** concentra moderação e recompensas.
- A aba Todos exibe uma única mensagem local identificada como **Todos** e ignora ecos correspondentes durante a janela de deduplicação.

## Fase -1 — DLL autocontida

Pedido do usuário: só precisar copiar `obs-multi-rtmp.dll` após formatar o PC.

- [x] `qrc/streamhub-data.qrc` embutindo `data/streamhub-server/**` e locale mínimo (en-US, pt-BR) na DLL via Qt Resources.
- [x] `src/streamhub-bundle.{h,cpp}`: `StreamHub_EnsureBundledData()` recria `data/` a partir do embutido, controlado por um marcador de versão (`.streamhub-bundle-version`), sem tocar em `config.json`/`node_modules`.
- [x] Chamada inserida no início de `obs_module_load()`, antes do primeiro `obs_module_text()`.
- [x] Dock de chat trocado de `QWebSocket` para long-polling HTTP (`QNetworkAccessManager` + novo endpoint `GET /api/chat/poll` no `index.js`), eliminando a dependência do módulo Qt6 WebSockets.
- [x] `CMakeLists.txt` atualizado: sem `Qt6::WebSockets`, com os novos arquivos fonte.
- [x] Compilar com VS Community 2026 e Qt do pacote do OBS.
- [ ] Testar em instalação nova do OBS, copiando só a DLL.
- [x] Repetir testes de chat Twitch/Kick com o long-poll.

Detalhes: [STATUS.md](STATUS.md) e [BUILD_STREAMHUB.md](BUILD_STREAMHUB.md).

## Fase 0 — Descoberta e base confirmada

- `BUILD_STREAMHUB.md`: arquitetura, build e proposta original de formulário Qt.
- `src/obs-multi-rtmp.cpp:474`: integração do launcher/dock e resolução do diretório absoluto. Preservar a correção de caminhos.
- `src/streamhub-chat-dock.cpp:29`: widgets nativos existentes; `ConnectTo(int)` e `SetStatus(const QString&)` são pontos atuais de integração.
- `src/streamhub-launcher.cpp`: `Start(...)`, `Restart()` e `Stop()` controlam o processo Node. O reinício usado pelo formulário é assíncrono.
- `data/streamhub-server/server/index.js`: inicia somente os conectores de chat e o servidor HTTP; o relay não faz mais parte da inicialização.
- `data/streamhub-server/server/chat/twitch.js`: `startTwitch(cfg, onMessage)` usa canal público, sem chave RTMP.
- `data/streamhub-server/server/chat/kick.js`: `startKick(cfg, onMessage)` usa canal e `chatroomId` opcional, com integração Pusher. O usuário confirmou o teste do chat Kick; repetir como teste de regressão após as alterações. Os comentários antigos não são documentação atual da plataforma.
- `data/streamhub-server/server/config-store.js:6`: `readConfig()` / `writeConfig(config)`; gravação atual substitui o arquivo sem validação suficiente ou troca atômica.
- `data/streamhub-server/server/routes/api.js:7`: GET/POST `/api/config` expõem/substituem configuração inteira. Não reutilizar esse comportamento como contrato do novo formulário.
- `data/streamhub-server/public/dashboard.js`: painel antigo ainda edita destinos do relay; precisa deixar de competir com os painéis nativos.
- `src/output-config.cpp`: persistência das saídas nativas existente; manter como autoridade para essas saídas.
- `src/edit-widget.cpp:732` e `:751`: menus de compartilhamento de vídeo/áudio; `src/push-widget.cpp:274` e `:316`: reutilização dos encoders do OBS. A UI nativa necessária já existe.
- `build_x64/CMakeCache.txt:439`: cache aponta para a antiga pasta em C:. Gerar uma nova pasta de build em vez de reutilizar esse cache.

Antes de adicionar APIs Qt/OBS, conferir os headers do SDK e a documentação correspondente. Os métodos novos de reinício/configuração/status serão implementados; não presumir que já existem.

## Fase 1 — Preparar instalação e separar transmissão de chat

1. Identificar a instalação de OBS em uso, perfil ativo e plugin carregado. Fazer backup dos arquivos que forem substituídos, sem imprimir credenciais.
2. Desativar o caminho de inicialização do relay nesta distribuição do plugin, inclusive para configurações antigas que contenham `rtmp`. Não depender apenas de alterar o exemplo.
3. Atualizar configuração de exemplo e documentação para iniciar apenas os chats configurados. Não apagar configurações antigas silenciosamente nem migrar chaves automaticamente para saídas existentes.
4. Retirar a configuração de relay do fluxo do dashboard antigo e orientar o uso dos painéis nativos. Manter a overlay funcional.

Verificar: um arquivo antigo contendo `rtmp` não abre a porta 1935 nem inicia FFmpeg; os chats Twitch/Kick continuam recebendo mensagens; perfil e destinos nativos permanecem intactos. A remoção de dependências exclusivas do relay só ocorre após confirmar ausência de consumidores.

## Fase 2 — Formulário nativo de canais

1. Acrescentar botão “Configurar canais” no dock, seguindo os widgets Qt existentes.
2. Exibir ativação e canal da Twitch; ativação e canal do Kick; `chatroomId` opcional em campo avançado. Não pedir chaves de transmissão.
3. Oferecer “Salvar e aplicar” e “Cancelar”, validação de campos e mensagens de erro legíveis.
4. Ler a mesma configuração efetiva utilizada pelo servidor. Permitir configurar mesmo se o servidor não conseguir iniciar ou o arquivo ainda não existir.
5. Salvar por substituição atômica, preservando campos não editados. Arquivo inválido deve produzir erro recuperável, sem sobrescrever os dados silenciosamente. Tratar ausência de permissão de escrita.
6. Fazer do formulário nativo o editor de configuração nesta distribuição. Desabilitar a gravação pelo dashboard legado para evitar substituições concorrentes; não expor o JSON completo com credenciais pelo endpoint antigo. Preservar endpoints necessários à overlay.

Verificar: salvar/reabrir preserva canais; cancelar não altera arquivo; dados inválidos e falha de escrita não destroem configuração; configurações de outras plataformas são preservadas; nenhuma chave RTMP é exigida ou exibida.

## Fase 3 — Aplicar mudanças e mostrar estado real

1. Implementar reinício assíncrono somente do processo Node pertencente ao launcher, reaproveitando runtime e dependências. Não chamar o encerramento bloqueante atual na ação do formulário.
2. Tratar saída inesperada, erro ao iniciar, tentativas repetidas de salvar e OBS fechando durante a preparação. Evitar dois processos ou callbacks antigos iniciando outro servidor.
3. Centralizar a porta efetiva entre launcher e Node e detectar conflito de porta. Confirmar prontidão antes de anunciar “conectado”.
4. Iniciar o serviço local antes de aguardar plataformas; falha de um conector não deve impedir os outros nem o dock. Manter escuta local explícita para o uso no mesmo computador.
5. Acrescentar status por plataforma: desativado, conectando, conectado e erro, separado da conexão entre dock e servidor. Reaproveitar o WebSocket com mensagens tipadas e compatibilidade com mensagens de chat existentes.
6. Reconectar dock e overlay após reinício. Reiniciar Node limpa o histórico em memória; limpar/sincronizar a apresentação para não repetir mensagens. Aceitar breve pausa do chat, preservando a transmissão nativa.

Verificar: alterar canal aplica sem fechar OBS; salvar repetidamente mantém um Node; queda do servidor muda o status; falha do Kick não impede Twitch; reinício não interfere nas saídas RTMP; fechar OBS não deixa o processo filho rodando.

## Fase 4 — Compilar e instalar a versão certa

1. Conferir SDK, Qt e ferramentas disponíveis; gerar build novo apontando para a pasta atual em E:.
2. Usar as opções do preset `windows-x64` e compilação `RelWithDebInfo` documentadas em `BUILD_STREAMHUB.md`, com diretório novo para evitar o cache antigo.
   O gerador atual é `Visual Studio 18 2026`. O dock usa `Qt6::Network`, já incluído no pacote Qt do OBS; não reconstruir nem instalar QtWebSockets.
3. Copiar DLL e dados correspondentes para a instalação de OBS identificada, com OBS fechado e backups. Não sobrescrever a configuração do usuário ao copiar dados.
4. Reabrir e confirmar pelo caminho/versão carregada que o plugin testado é o recém-compilado.

Verificar: compilação concluída, plugin carrega, dois docks disponíveis, configuração preservada e runtime reutilizado na segunda abertura. Documentar os caminhos reais de build e instalação; não repetir como fato a antiga alegação de que este ambiente não compila.

## Fase 5 — Validar o uso real

1. Configurar Twitch e Kick no formulário e repetir os testes de mensagens em ambos os canais, já validados antes destas mudanças. Se Kick falhar, investigar a integração atual e sua documentação antes de escolher correção; não considerar o campo manual prova de funcionamento.
2. Conferir destinos nativos no perfil ativo, URLs e chaves atuais, sem copiá-las para o chat. Evitar duplicar uma plataforma que já seja a saída principal do OBS.
3. Selecionar compartilhamento dos encoders de vídeo/áudio onde suportado e compatível. Confirmar configuração e logs; não prometer uma única codificação se os destinos exigirem formatos distintos.
4. Em sessão de teste combinada com o usuário, transmitir para as duas plataformas e verificar imagem/áudio, bitrate e estabilidade por pelo menos dez minutos. Medir CPU, GPU, memória e quadros perdidos; considerar o upload somado dos destinos.
5. Durante o teste, aplicar mudança de chat e confirmar que a transmissão continua. Reabrir OBS depois e verificar persistência e inicialização com cache.

Testes automatizados focados: preservação/validação de configuração, relay desativado com arquivo legado, falha isolada de conector e ciclo de reinício sem processos duplicados. O projeto ainda não define script de testes; escolher o mecanismo compatível com o runtime verificado. Usar credenciais fictícias nos testes.

Concluído quando: os dois chats recebem mensagens; canais são configuráveis pelo dock; salvar aplica sem reiniciar OBS; há apenas um destino por transmissão pretendida; os encoders são compartilhados quando compatíveis; nenhum relay/FFmpeg é iniciado pelo StreamHub; multistream real e segunda abertura passam. Registrar resultados medidos e qualquer limitação restante.

