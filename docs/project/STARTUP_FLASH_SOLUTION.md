# Solution Brief: fundo preto na abertura

Data: 2026-10-24

## Problem Recap
A prévia aparece branca durante alguns segundos na abertura, mesmo com tema escuro. A tentativa anterior tratava a criação de janelas pela barra de título, mas o sintoma persiste.

## Proposed Solution
Preencher de preto as superfícies de prévia na criação/exibição e responder à limpeza de fundo do Windows com preto. A renderização normal continua sob controle do OBS.

## Key Features
1. Fundo preto antes do primeiro quadro.
2. Tratamento compartilhado das prévias que desenham diretamente na janela.
3. Uso de janelas já existentes, sem promover ancestrais nativos.

## Success Metrics
Meta: zero quadro branco na abertura; confirmar visualmente em execução. Compilação valida compatibilidade, não comprova ausência do clarão.

## Trade-offs Considered
Alterar apenas o tema não cobre superfícies que desativam a pintura do Qt. Não alterar cenas, fontes ou disposição dos painéis.

## Risks & Mitigations
Uma limpeza tardia pode apagar um quadro temporariamente: executar preenchimento apenas na criação/exibição ou quando o Windows solicita limpeza, sem interceptar a pintura do OBS. Restringir a widgets com pintura direta na tela.

## Next Steps
1. Aplicar proteção nativa.
2. Compilar a DLL.
3. Instalar com OBS fechado e verificar abertura visual quando disponível.

## Refinamento: painéis nas posições salvas
O usuário confirmou melhora do fundo, mas a prévia ainda ocupa a janela antes dos painéis aparecerem. OBSBasic::OBSInit chama show() antes de restoreState(DockState). Antecipar a restauração do DockState na primeira exibição da janela principal, depois do registro dos docks; manter a restauração normal do OBS. Meta: primeira exibição com os painéis salvos, sem alterar o arquivo de configuração. Risco: docks adicionais criados pelo OBS depois de show() ainda dependem da restauração posterior. Validar abertura, posições e prévias.
