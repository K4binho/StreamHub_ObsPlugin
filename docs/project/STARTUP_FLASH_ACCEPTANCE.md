---
artifact: acceptance-criteria
version: "1.0"
created: 2026-10-24
status: pending-runtime-validation
---

# Acceptance Criteria: abertura sem clarão branco

## Story Context

Evitar o clarão de inicialização sem alterar cenas, fontes ou disposição dos painéis.

## Happy Path

### AC-1: abertura
**Given** OBS fechado com tema escuro e painéis salvos.
**When** o usuário abre o OBS.
**Then** a área da janela permanece escura até as prévias e painéis aparecerem.

## Edge Cases

### AC-2: painéis destacados
**Given** painéis verticais destacados.
**When** o usuário reabre o OBS.
**Then** a disposição salva é preservada.

## Error States

### AC-3: carregamento lento
**Given** fontes externas indisponíveis.
**When** o OBS aguarda o carregamento.
**Then** não exibe uma superfície branca cobrindo a interface.

## Non-Functional Criteria

### AC-4: preservação
**Given** uma coleção de cenas existente.
**When** a correção é instalada.
**Then** nenhuma configuração de cena ou transmissão é reescrita pela instalação.

## Notes

- Hipótese: criação prematura de HWND pelo hook de título, agravada pela promoção de ancestrais nativos dos indicadores.
- Correção usa somente HWND já existente; não muda o tema global nem esconde a janela.
- Compilação e testes não comprovam ausência do clarão; abertura visual ainda precisa ser validada.

## Atualização: fundo preto nativo (2026-10-24)

- A hipótese anterior não resolveu o sintoma informado pelo usuário.
- A prévia OBSQTDisplay usa WA_PaintOnScreen e WA_NoSystemBackground; o tema Qt não preenche essa superfície antes do primeiro quadro.
- O hook agora preenche de preto as prévias na criação/exibição e responde a WM_ERASEBKGND com preto, sem criar HWNDs nem interceptar a pintura normal.
- DLL compilada em build_x64_modular e instalada em E:/Streaming/obs-studio/obs-plugins/64bit, com hash idêntico ao artefato compilado.
- Backup: obs-multi-rtmp.dll.before-black-preview-20261024 na mesma pasta.
- OBS 33.0.0 abriu com as prévias horizontal/vertical e painéis funcionando. A captura ocorreu após a abertura: os primeiros quadros ainda não foram registrados, portanto a ausência total de clarão permanece pendente de confirmação.

## Atualização: restauração antecipada do layout

- OBSBasic::OBSInit na fonte local mostra a janela antes de restaurar BasicWindow/DockState.
- O filtro da primeira exibição agora aplica o layout salvo e ativa seu cálculo antes da pintura. A restauração posterior feita pelo OBS permanece disponível para docks tardios.
- Compilação concluída e DLL instalada com backup before-startup-layout-20261024.
- Na primeira captura disponível após localizar a janela, os painéis e ambas as prévias já estavam nas posições normais. Não houve captura quadro a quadro da abertura; o comportamento durante todo o intervalo inicial ainda requer confirmação do usuário.
