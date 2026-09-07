# StreamHub OBS Plugin

## Projeto

Plugin C++/Qt para OBS Studio, baseado em `obs-multi-rtmp`, com:

- painel nativo de múltiplas saídas RTMP;
- dock nativo `StreamHub Chat`;
- painel nativo `Informações de transmissão K4`;
- Twitch OAuth para chat, dados da live, moderação e recompensas;
- servidor Node.js embutido em `data/streamhub-server`;
- tema K4binho e ícones Qt compilados em `qrc/streamhub-data.qrc`.

## Arquitetura importante

- `src/obs-multi-rtmp.cpp`: dock de múltiplas saídas, cartão da transmissão principal e entrada `obs_module_load()`.
- `src/push-widget.cpp`: cartões e estado de cada saída.
- `src/streamhub-chat-dock.cpp/.h`: polling HTTP, mensagens, filtros, envio autenticado, deduplicação e renderização nativa.
- `src/streamhub-chat-settings.cpp`: formulário de configuração do chat e gravação atômica de `config.json`.
- `src/streamhub-chat-admin.cpp/.h`: moderação, configurações administrativas, recompensas e resgates.
- `src/streamhub-control-dock.cpp/.h`: cartões de contas e editor de dados da live com prévia e busca de categoria.
- `src/streamhub-launcher.cpp`: provisionamento do Node, `npm install` e processo do servidor.
- `src/streamhub-bundle.cpp`: extração versionada dos recursos embutidos para a pasta de dados do plugin.
- `qrc/streamhub-data.qrc`: locale, servidor Node, ícones, branding e tema compilados dentro da DLL.
- `data/streamhub-server/server/index.js`: servidor HTTP, histórico, long-poll e eventos de status.
- `data/streamhub-server/server/accounts.js`: OAuth e armazenamento privado de tokens por plataforma.
- `data/streamhub-server/server/routes/api.js`: endpoints locais de contas, chat, transmissão, moderação e recompensas.
- `data/streamhub-server/server/chat/*.js`: conectores Twitch, Kick, YouTube e TikTok.

## Regras de distribuição

- DLL copiada sozinha precisa funcionar: recursos devem estar em Qt Resource System ou ser extraídos por `StreamHub_EnsureBundledData()`.
- Nunca sobrescrever `data/streamhub-server/config.json`, credenciais ou `node_modules` durante extração.
- Ao mudar arquivos embutidos no QRC, aumentar `kBundleVersion` em `src/streamhub-bundle.cpp`.
- Locale usado por `obs_module_text()` precisa existir em `data/<plugin>/locale`; a extração deve ocorrer antes do primeiro `obs_module_text()`.
- Ícones nativos usam caminhos `:/streamhub-ui/icons/*.svg`.

## Configuração do chat

- `data/streamhub-server/config.example.json` define defaults.
- Configuração de usuário fica em `data/streamhub-server/config.json` e não deve ser commitada.
- Opções novas precisam manter compatibilidade com configs antigos, usando default seguro quando campo ausente.
- Alterações no formulário Qt devem preservar campos JSON desconhecidos.

## Estado atual

- Múltiplas saídas mostra a transmissão principal do OBS como primeiro cartão fixo.
- Informações de transmissão contém somente Contas e Transmissão.
- StreamHub Chat contém envio; o botão ADM concentra moderação e recompensas.
- Na visualização Todos, mensagens enviadas aparecem uma vez com o selo Todos.
- Alertas online/offline não aparecem; somente reinicialização real do chat pode ser informada.
- Twitch está implementada. OAuth e escrita de YouTube, Kick e TikTok permanecem pendentes.

## Verificação

1. Compilar com Qt `AUTOMOC`, `AUTOUIC` e `AUTORCC`.
2. Testar DLL em diretório limpo, sem pasta `data` manual.
3. Verificar labels traduzidos e ícones de adicionar/iniciar/parar/plataformas.
4. Alternar opção de horário e verificar imediatamente após salvar.
5. Enviar pela Twitch em Todos e confirmar uma única linha local.
6. Abrir ADM e verificar moderação/recompensas com uma conta autorizada.
7. Conferir `git diff` e preservar alterações pré-existentes do usuário.

## Segurança e versionamento

- Não incluir tokens, API keys, stream keys, `accounts-private.json` ou `config.json` em commits.
- Não fazer commit ou push sem pedido explícito.
- Preferir mudanças pequenas e manter idioma/nomenclatura existentes.
