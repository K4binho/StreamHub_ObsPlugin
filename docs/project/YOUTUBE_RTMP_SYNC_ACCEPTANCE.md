---
artifact: acceptance-criteria
version: "1.0"
created: 2026-10-24
status: implemented-runtime-ui-pending
---

# Sincronização RTMP do YouTube

## Story Context

Sincronizar o evento escolhido sem confundir eventos de webcam/WebRTC com eventos de codificador e sem alterar os destinos ativos ou as credenciais verticais manuais.

## Happy Path

**Given** um evento ativo/editável vinculado a uma stream com endpoint RTMP/RTMPS e chave.

**When** listar eventos e sincronizar explicitamente esse evento.

**Then** a lista o identifica como RTMP/RTMPS disponível e a consulta retorna somente as credenciais desse evento.

## Edge Cases

**Given** eventos com títulos iguais, mas protocolos ou privacidades diferentes.

**When** carregar a lista.

**Then** protocolo, privacidade e ID distinguem cada opção; opções compatíveis aparecem primeiro, sem seleção automática de outro evento.

**Given** a seleção salva foi encerrada ou desapareceu.

**When** atualizar a lista.

**Then** a seleção fica vazia e explica que é necessário escolher outro evento, sem reaproveitar dados antigos como sucesso.

## Error States

**Given** um evento WebRTC/webcam ou sem credenciais RTMP.

**When** tentar sincronizar.

**Then** a operação explica a incompatibilidade e preserva os servidores e chaves atuais, sem inventar endpoint ou converter o evento.

**Given** falha na consulta oficial.

**When** listar ou sincronizar eventos.

**Then** o erro é comunicado e nenhuma credencial é sobrescrita.

## Non-Functional Criteria

**Given** um jogo aberto ou saída transmitindo.

**When** diagnosticar e instalar a correção.

**Then** não são enviados comandos ao jogo, nem iniciado/encerrado qualquer destino; DLL em uso não é substituída.

**Given** a resposta pública de listagem de eventos.

**When** inspecionar seus campos.

**Then** não contém chave nem endereço de ingestão.

## Evidência

- Consulta oficial autenticada: seleção anterior encerrada; três eventos editáveis, sendo um RTMP/RTMPS compatível e dois WebRTC incompatíveis.
- Consulta direta ao evento privado compatível retornou servidor RTMPS e chave não vazia; nenhuma transmissão foi iniciada nem vínculo remoto alterado.
- Testes Node: 14 aprovados, incluindo seleção exata, rejeição de evento encerrado, classificação de protocolo e ausência de chave na lista.
- Não confundir consulta de credenciais com teste de transmissão ou validação visual no OBS.
