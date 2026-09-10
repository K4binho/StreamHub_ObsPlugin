# Consolidar documentação StreamHub

## Objetivo

Criar única fonte documental em `PLANOS/`, separando referência técnica para desenvolvimento/IA de manual operacional para build e uso final.

## Arquivos criar

1. `PLANOS/ARQUITETURA_E_CONTEXTO.md`
   - Contexto e estado real em 08/09/2026.
   - Arquitetura C++/Qt, Node.js embutido, bundle Qt Resource System, launcher, docks e persistência.
   - Regras estritas: caminhos dinâmicos (`StreamHubWritableDataPath()`, `StreamHubServerPath()`, `obs_module_config_path()`), segredos, Job Object, encerramento de processos, IPC local autenticado, compatibilidade de config e versionamento `kBundleVersion`.
   - Status separado entre implementado, validado e pendente; não apresentar Watchdog, OAuth Kick oficial ou sincronização Node/C++ como concluídos.
   - Roadmap: correção YouTube/Twitch, Watchdog, Job Object, ponte IPC, Kick OAuth/PKCE, sincronização idempotente de destinos e validação.
   - Critérios de teste, segurança, preservação de alterações existentes e política de commit/push.

2. `PLANOS/MANUAL_E_BUILD.md`
   - Descrição das features atuais.
   - Requisitos Windows e comandos CMake reais derivados de `buildspec.json`.
   - Instalação da DLL, extração automática, Node portátil, configuração dos três docks, chat, ADM, overlay, contas e destinos.
   - Setup YouTube e limitações Kick/TikTok/Facebook.
   - Instalação do tema K4binho e fundo de cena.
   - Build Flatpak/WSL e comandos existentes em `FLATPAK.md`.
   - Troubleshooting de dock, primeira instalação, `node_modules` incompleto e logs.
   - Avisos de segurança sem incluir credenciais ou stream keys.

## Conteúdo consolidar

Usar como fontes `BUILD_STREAMHUB.md`, `CLAUDE.md`, `contexto.md`, `STATUS.md`, `PLANO_STREAMHUB.md`, `FLATPAK.md`, `data/streamhub-server/README.md`, `themes/INSTALACAO.md`, `docs/Readme.md` e README atual. Remover histórico repetido, instruções superadas e caminhos absolutos de máquina.

## Arquivos substituir/remover

- Substituir `README.md` por ponte curta para os dois mestres.
- Remover docs legados consolidados: `BUILD_STREAMHUB.md`, `CLAUDE.md`, `contexto.md`, `STATUS.md`, `PLANO_STREAMHUB.md`, `FLATPAK.md`, `data/streamhub-server/README.md`, `themes/INSTALACAO.md`, `docs/Readme.md`.
- Não tocar em documentação vendorizada dentro de `.deps/` nem em arquivos de código/configuração.

## Validação

1. Conferir existência e conteúdo dos dois arquivos em `PLANOS/`.
2. Buscar referências quebradas a docs removidos.
3. Confirmar ausência de tokens, Client Secrets, stream keys e caminhos pessoais.
4. Conferir `git diff --check`.
5. Conferir `git status --short` e preservar todas alterações pré-existentes fora do escopo.
