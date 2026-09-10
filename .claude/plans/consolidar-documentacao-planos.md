# Consolidar documentação em PLANOS

## Objetivo

Criar dois documentos mestres em `PLANOS/`, consolidando contexto técnico, arquitetura, segurança, roadmap, build e uso do StreamHub. Substituir `README.md` por ponte curta para o manual.

## Arquivos novos

1. `PLANOS/ARQUITETURA_E_CONTEXTO.md`
   - Regras estritas de desenvolvimento e distribuição.
   - Arquitetura C++/Qt, Node.js, bundle Qt Resource System, launcher, docks e persistência.
   - Estado real validado e pendências, sem declarar IPC, Watchdog, Kick OAuth ou sincronização como implementados antes do código existir.
   - Roadmap: correções YouTube/Twitch, Watchdog, Job Objects, IPC local autenticado, Kick OAuth/PKCE, sincronização idempotente Node/C++ e validação.
   - Segurança de tokens, Client Secrets, stream keys, caminhos dinâmicos, processos e bundle.
   - Critérios de testes e regra de preservar alterações pré-existentes.

2. `PLANOS/MANUAL_E_BUILD.md`
   - Visão geral e features disponíveis.
   - Pré-requisitos e comandos CMake/Windows.
   - Instalação da DLL, primeira execução, Node provisionado, configuração dos docks, contas, overlay e tema.
   - Configuração YouTube e limitações atuais de Twitch, Kick, TikTok e Facebook.
   - Procedimento Flatpak, mantendo comandos e fatos de `FLATPAK.md` em seção própria.
   - Troubleshooting e segurança de credenciais para usuário final.

## Arquivos alterados/removidos

- Reescrever `README.md` com título curto e link relativo para `PLANOS/MANUAL_E_BUILD.md`.
- Remover `BUILD_STREAMHUB.md`, `CLAUDE.md`, `contexto.md` e `STATUS.md` após migração.
- Manter `FLATPAK.md`, `PLANO_STREAMHUB.md`, `data/streamhub-server/README.md` e `themes/INSTALACAO.md` como guias especializados; conteúdo relevante será duplicado/consolidado nos mestres, sem apagar documentação operacional específica.

## Regras de conteúdo

- Usar data de atualização `08/09/2026`.
- Preservar nomes de símbolos, caminhos, comandos, URLs e mensagens de erro exatamente.
- Marcar como pendente qualquer capacidade apenas planejada: Watchdog, Job Objects/IPC novos, Kick OAuth oficial e sincronização automática de destinos.
- Não incluir tokens, Client Secrets, stream keys, URLs OAuth com `code=`, caminhos pessoais de instalação como requisito ou dados privados.
- Corrigir links que apontavam para documentos removidos, apontando para os dois mestres.
- Não alterar código, configuração, arquivos staged existentes ou histórico Git.

## Validação

1. Confirmar existência dos dois mestres e link do README.
2. Buscar referências aos documentos removidos e corrigir referências quebradas nos arquivos consolidados.
3. Conferir `git diff --stat` e `git diff --check`.
4. Confirmar que nenhuma credencial ou stream key apareceu nos novos arquivos.
5. Não executar build: mudança documental não afeta código compilável.
