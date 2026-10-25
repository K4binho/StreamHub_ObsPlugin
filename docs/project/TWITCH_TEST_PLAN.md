# Preparação modular e testes Twitch

Data: 2026-10-24

## Problem Recap
O README não descreve o editor vertical, os testes reais do YouTube nem a proteção de abertura. O código do tema acumula proteção de prévias e restauração de layout. A Twitch ainda precisa de uma rodada registrada de validação.

## Proposed Solution
Atualizar a documentação com evidências, separar o ciclo de abertura do tema e iniciar testes locais e consultas de leitura da Twitch. Preservar a configuração e a sessão do OBS.

## Key Features
1. README com editor vertical, abertura e estado de validação.
2. Módulo de abertura separado do tema nativo.
3. Regressões de consulta de metadados e chave Twitch com respostas simuladas.
4. Consulta real de conta, escopos, canal e estado ao vivo sem divulgar tokens.

## Success Metrics
Compilação sem erro, testes Node e CTest aprovados; conta Twitch identificada e permissões verificadas. Envio real de vídeo e mensagens só contam como validados após evidência própria.

## Trade-offs Considered
Reorganização incremental: o editor vertical já está distribuído em módulos. Não reescrever os coordenadores inteiros nesta rodada. Não iniciar uma live pública, enviar chat ou modificar metadados implicitamente.

## Risks & Mitigations
Separação pode perder eventos ou registro na compilação: preservar lógica e ponto de instalação; compilar a DLL. Consultas podem falhar por token expirado: registrar pré-requisito sem revelar credenciais. Não substituir DLL carregada.

## Next Steps
1. Separar o módulo de abertura e atualizar o mapa de responsabilidades.
2. Executar regressões de Twitch e a suíte existente.
3. Conferir a conta real por consultas de leitura.
4. Registrar resultados e preparar a etapa explícita de transmissão.

## Ajuste de interface solicitado
Remover a faixa de botões de transmissão/gravação horizontal do preview vertical, preservando o canvas 9:16, seu estado e os controles principais do OBS. Critério: nenhuma faixa horizontal no painel; atualização do estado da prévia continua funcionando. Recompilar antes de instalar; não substituir DLL carregada.

## Roteiro de validação

| Etapa | Critério |
| --- | --- |
| Conta | Token válido, usuário e aplicativo correspondentes |
| Permissões | Conferir escopos efetivamente concedidos |
| Metadados | Ler título, categoria, idioma e tags do canal |
| Chave | Consultar chave autorizada sem exibi-la nem substituir a salva |
| Chat | Observar conexão e leitura; envio exige mensagem de teste definida |
| Vídeo | Início explícito na Twitch; conferir player e integridade no painel |
| Parada | Encerrar apenas a saída escolhida; demais destinos permanecem no estado anterior |

## Resultados desta rodada

Atualização após configuração Dual Format: o usuário confirmou funcionamento em teste real. A DLL com isolamento da saída principal e nome StreamHub foi instalada com backup; Enhanced Broadcasting e canvas adicional foram conferidos na interface. Os itens históricos abaixo descrevem a preparação anterior e não substituem os testes restantes de chat, moderação e estabilidade.

### Regressão encontrada no teste real
Ao iniciar a Twitch pelo cartão individual, a vertical YouTube acompanhou o evento nativo STREAMING_STARTED: vertical habilitada e sync-start ligado, mas horizontal excluída. Corrigir o cartão principal para usar o caminho isolado existente e exigir pai habilitado/sincronizado no início automático de uma saída vinculada. Critérios: Twitch individual não inicia nem para auxiliares; início conjunto e sincronização explícita continuam disponíveis; vertical com pai excluído não acompanha início nativo. Testar política com pai ausente, excluído e habilitado. Não interromper a live nem reescrever configurações durante diagnóstico.

Correção implementada: cartão principal reutiliza StartHorizontalOnly/StopHorizontalOnly; o caminho de parada também aceita a fase de conexão. Início automático de filha exige pai existente, habilitado e sincronizado, além de startWithParent. Regressões cobrem pai excluído, sem sincronização, ausente e vínculo desligado. DLL e teste de ciclo compilados. Validação real da correção e instalação pendentes enquanto o OBS estiver aberto.

- Servidor local: 18/18 testes aprovados, incluindo quatro regressões novas da Twitch (consulta de chave e metadados, chave ausente, permissão recusada e canal ausente).
- Plugin: compilação RelWithDebInfo concluída; CTest 7/7 aprovado; git diff --check sem erros.
- API oficial: token válido para k4binho, usuário correspondente à conta salva, chave autorizada obtida sem exibição e canal offline no momento da consulta.
- Metadados lidos: título Live Teste Stream, categoria Minecraft, idioma pt.
- Escopos concedidos incluem channel:read:stream_key, channel:manage:broadcast, user:read:chat, user:write:chat e permissões de moderação. Permissão presente não comprova execução dessas ações.
- Servidor em execução: histórico de estados registra Twitch conectado. Nenhuma mensagem Twitch presente no histórico consultado; leitura de mensagem de ponta a ponta ainda não validada.
- Fontes oficiais: https://dev.twitch.tv/docs/authentication/validate-tokens/ e https://dev.twitch.tv/docs/authentication/scopes/.
- A DLL reorganizada foi gerada no build, sem substituir a DLL carregada pelo OBS. Abertura visual dessa nova compilação permanece pendente.
- Não foi iniciada live, enviado chat, alterado título/categoria nem executada moderação. O próximo teste de vídeo precisa definir se será diagnóstico sem publicação ou live real no canal.
