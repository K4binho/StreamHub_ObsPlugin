---
artifact: acceptance-criteria
version: "1.0"
created: 2026-10-22
status: draft
---

# Organização vertical e encerramento

## Story Context

Reunir horizontal e vertical no mesmo cartão YouTube, oferecer cópia das configurações de imagem/transmissão e dois modos de organização vertical.

## Happy Path

### AC-1: Configuração conjunta

**Given** um destino YouTube com saída vertical vinculada.
**When** o usuário abre a engrenagem do cartão YouTube.
**Then** encontra as configurações horizontal e vertical juntas, com servidores e chaves próprios.

### AC-2: Copiar configuração principal

**Given** uma saída parada e configurações válidas na transmissão principal.
**When** o usuário clica em Copiar configurações da transmissão principal abaixo da chave e salva.
**Then** a saída utiliza uma cópia das configurações de vídeo, codificação e áudio; mantém suas credenciais e, no vertical, o formato 9:16.

### AC-3: Seguir cenas principais

**Given** o modo Seguir cenas principais.
**When** a cena principal muda.
**Then** o vertical acompanha a cena correspondente e permite organizar suas fontes em layout próprio sem modificar o enquadramento horizontal.

### AC-4: Cenas independentes

**Given** o modo Cenas independentes.
**When** o usuário cria uma cena vertical ou copia uma principal.
**Then** pode organizar e selecionar essa cena independentemente da cena horizontal.

## Edge Cases

### AC-5: Alternar modos

**Given** layouts salvos nos dois modos.
**When** o usuário alterna o modo e reabre o OBS.
**Then** os layouts existentes são preservados.

## Error States

### AC-6: Falha ao salvar

**Given** falha na gravação da configuração.
**When** o usuário salva alterações.
**Then** recebe uma mensagem de erro e a configuração anterior permanece válida.

## Non-Functional Criteria

### AC-7: Encerramento

**Given** uma sessão que utilizou ambas as saídas.
**When** o usuário para as saídas e fecha o OBS.
**Then** o processo encerra sem novo relatório de crash.

## Notes

- O crash de 22/10 às 19:59:32 ocorre em obs_source_release chamado por obs_canvas_destroy durante obs_shutdown. A pilha identifica o ponto da falha, mas ainda não determina quem deixou a referência inválida.
- Estes critérios descrevem trabalho solicitado; não representam funcionalidades já implementadas ou validadas.
