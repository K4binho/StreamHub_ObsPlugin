# Etapa 2 — Watchdog e encerramento seguro do Node.js

## Objetivo
Manter servidor Node ligado somente enquanto instância correta do OBS existir, impedir colisão com instância antiga e garantir encerramento gracioso antes de finalizar processo.

## Alterações

1. **Paths e runtime C++**
   - Expandir `src/streamhub-paths.h` com resolução dinâmica para `StreamHubWritableDataPath()` e `StreamHubServerPath()`.
   - Usar `obs_module_config_path(...)` para localizar `runtime.json`; não inserir caminhos absolutos.
   - Criar helpers para ler/gravar `runtime.json` atomicamente, preservando apenas metadados necessários: PID do Node, PID do OBS, porta e token da instância.
   - Restringir permissões do arquivo quando suportado.

2. **Servidor Node (`data/streamhub-server/server/index.js`)**
   - Ler `STREAMHUB_OBS_PID` e token de runtime fornecido pelo launcher.
   - Validar PID como inteiro positivo.
   - Adicionar watchdog em intervalo aproximado de 2 segundos; testar existência do processo OBS sem confiar apenas em texto externo.
   - Criar `shutdown()` idempotente: parar watchdog, cancelar long-polls, parar conectores retornados por `start*`, fechar Socket.IO/HTTP e sair apenas após fechamento.
   - Registrar `SIGTERM` e `SIGINT` em `shutdown()`.
   - Adicionar `/internal/status` e `/internal/shutdown`; exigir token em header `X-StreamHub-Token`, retornar `401` sem token válido e não expor token em respostas/logs.
   - Limitar endpoints internos a loopback quando possível.
   - Manter compatibilidade com conectores que ainda não retornam `stop()`.

3. **Launcher C++ (`src/streamhub-launcher.cpp/.h`)
   - Gerar token aleatório por processo usando API criptográfica Qt; manter token em memória e gravar runtime somente para descoberta/controle local.
   - Na inicialização, ler runtime anterior, validar PID e token/porta, confirmar que PID é processo Node/StreamHub esperado e pedir `/internal/shutdown` por loopback antes de encerrar instância antiga.
   - Nunca usar `taskkill /IM node.exe`; nunca matar PID sem validação.
   - Passar `STREAMHUB_OBS_PID` e token ao Node via `QProcessEnvironment`.
   - Gravar runtime depois de iniciar processo e remover/invalidar runtime ao terminar.
   - Implementar encerramento em fases: endpoint interno com token, `terminate()`, espera limitada, `kill()` como último recurso.
   - Evitar corrida entre restart, finished signal e shutdown.

4. **Windows process ownership**
   - Associar processo Node a Job Object com `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` após início.
   - Guardar handle no launcher; fechar handle no encerramento/destrutor.
   - Tratar falhas de criação/associação sem derrubar OBS, registrando log e mantendo fallback terminate/kill.
   - Compilar código Windows sob `_WIN32`; manter caminho POSIX compilável sem APIs Windows.

5. **Integração OBS**
   - Usar PID atual do OBS no launcher, sem caminho fixo.
   - Manter `obs_module_load()` e evento `OBS_FRONTEND_EVENT_EXIT` compatíveis com launcher estático atual.
   - Não alterar configuração de usuário, credenciais ou `node_modules` durante bundle/runtime.
   - Incrementar `kBundleVersion` porque `index.js` embutido muda.

6. **Validação**
   - Rodar `node --check data/streamhub-server/server/index.js`.
   - Testar endpoints internos com token correto/incorreto.
   - Testar watchdog com PID inexistente e shutdown repetido.
   - Rodar build CMake Qt com `AUTOMOC`, `AUTOUIC` e `AUTORCC`.
   - Verificar diff, caminhos dinâmicos e ausência de `taskkill /IM node.exe`.

## Ordem de implementação

1. Helpers de paths/runtime e contrato de ambiente.
2. Shutdown/endpoints/watchdog Node.
3. Launcher C++ e stale-instance cleanup.
4. Job Object Windows.
5. Bundle version.
6. Syntax check, testes e build.
