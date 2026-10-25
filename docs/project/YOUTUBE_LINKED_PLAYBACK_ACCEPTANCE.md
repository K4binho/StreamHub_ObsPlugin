---
artifact: acceptance-criteria
version: "1.0"
created: 2026-10-24
status: implemented-runtime-pending
---

# Controle conjunto do YouTube

## Story Context

O cartão único esconde a saída vertical vinculada. Seu botão deve controlar ambas, sem exigir descobrir um controle na engrenagem. Não modificar servidor, chave, evento ou transmissão em execução durante a instalação.

## Happy Path

**Given** horizontal parada e vertical vinculada habilitada.

**When** clicar no play do cartão YouTube.

**Then** ambas recebem solicitação de início e seus estados aparecem separadamente no cartão.

**Given** uma ou ambas as saídas conectando, ao vivo ou reconectando.

**When** clicar em parar no cartão.

**Then** todas as saídas ocupadas do grupo recebem solicitação de parada, sem iniciar uma saída parada.

## Edge Cases

**Given** o card “Vínculo de início — horizontal e vertical” nas configurações.

**When** desmarcar “Iniciar a vertical junto com a horizontal”, salvar e reabrir.

**Then** a escolha permanece desmarcada; o play horizontal não inicia a vertical, mas o controle individual e Iniciar tudo continuam disponíveis.

**Given** um perfil anterior sem preferência explícita de vínculo.

**When** carregar suas configurações.

**Then** o início conjunto permanece ligado por compatibilidade com o comportamento anterior.

**Given** vertical desativada para início conjunto.

**When** iniciar pelo cartão.

**Then** somente a horizontal é iniciada e o cartão informa a desativação da vertical.

**Given** vertical em execução, mas posteriormente desativada para início conjunto.

**When** parar pelo cartão.

**Then** a vertical também é encerrada.

**Given** início por Iniciar tudo ou sincronização nativa do OBS.

**When** o fluxo já percorre cada saída.

**Then** nenhuma solicitação adicional é produzida pelo controle do cartão.

## Error States

**Given** uma saída inicia e a outra falha.

**When** o painel atualiza.

**Then** o cartão apresenta o estado de cada orientação e o botão permanece em parar enquanto houver saída ocupada.

**Given** saída continua encerrando após o prazo de parada normal.

**When** o usuário solicita nova parada com a opção de forçar disponível.

**Then** o encerramento forçado continua acessível.

## Non-Functional Criteria

**Given** diagnóstico posterior.

**When** examinar o log.

**Then** solicitações de início registram identificador e orientação, sem registrar servidor com chave ou credenciais.

## Evidência

- Regressão automatizada no teste de ciclo de saída: iniciar/parar ambas, vertical desabilitada, falha parcial e parada de vertical ocupada/desabilitada.
- Validação de conexão real de ambas no YouTube permanece pendente; testes não iniciam transmissão.
