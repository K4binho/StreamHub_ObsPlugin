# StreamHub OBS Plugin — Instruções para desenvolvimento

Leia antes de alterar código:

- [Arquitetura, contexto, regras estritas, segurança e roadmap](PLANOS/ARQUITETURA_E_CONTEXTO.md)
- [Manual, build e operação](PLANOS/MANUAL_E_BUILD.md)

## Regras rápidas

- Preserve alterações pré-existentes no worktree.
- Não faça commit ou push sem pedido explícito.
- Não hardcode caminhos de máquina. Use `StreamHubWritableDataPath()`, `StreamHubServerPath()` e `StreamHubModuleConfigPath()`.
- Nunca registre, commite ou exponha Client Secret, tokens ou stream keys.
- Node gerencia APIs, OAuth e credenciais. C++ gerencia saídas RTMP e `GlobalMultiOutputConfig()`.
- Ao alterar arquivos embutidos em `qrc/streamhub-data.qrc`, aumente `kBundleVersion` em `src/streamhub-bundle.cpp`.
- Preserve `config.json`, `accounts-private.json`, `node_modules`, campos JSON desconhecidos, ordem de destinos, encoders e configurações avançadas durante migrações ou extração.
- Mantenha compatibilidade com configurações antigas e defaults seguros para campos ausentes.
- Antes de concluir, execute `node --check` nos arquivos Node alterados, configure/compile com CMake quando código for alterado e rode `git diff --check`.
- Não declarar Watchdog, Job Objects, IPC Node/C++ ou sincronização automática como implementados antes de código e validação.
