# Auditoria do editor vertical — 24/10/2026

> A tabela abaixo é o diagnóstico anterior à expansão. Implementação e critérios atualizados estão em `VERTICAL_EDITOR_EXPANSION_ACCEPTANCE.md`. A expansão adiciona comandos, histórico estrutural, hierarquia de grupos, contornos múltiplos, transformações conjuntas, ordenação por arraste no mesmo pai, duplicação independente e zoom/deslocamento. Esses itens não devem ser confundidos com equivalência visual integral já validada.

Escopo: interação do canvas, cenas e fontes do StreamHub comparada ao código nativo disponível em `.deps/obs-studio-32.2.1/frontend`. Ambiente do usuário: OBS portátil 33.0.0. Esta análise estática não prova equivalência em execução.

## Critério de conclusão

Uma alteração compilada é implementada, não validada visualmente. Os testes de configuração e áudio não cobrem o editor. Cada interação abaixo deve ser executada no OBS antes de ser declarada concluída.

| Ponto | Situação no código | Validação pendente |
|---|---|---|
| Cursor em corpo, oito alças, rotação, fundo e bloqueados | Tratamento unificado de alças para cursor e clique | Exercitar posições com fonte normal e girada |
| Seleção e desseleção | Contorno permanece selecionado; clique fora limpa seleção | Fonte/lista/painel/menu e janela flutuante |
| Distâncias e guias | Durante manipulação; dimensões 1080 × 1920 | Conferir valores após recorte/rotação |
| Retenção no centro | Captura e liberação com raios distintos; teste específico passa | Conferir sensação de arraste e Ctrl |
| Preferências de encaixe | Consulta SnappingEnabled, ScreenSnapping, CenterSnapping, SourceSnapping, SnapDistance | Alternar preferências no OBS |
| Oito alças | Implementadas | Exercitar cada alça, Shift e caixa delimitadora |
| Proporção | Preserva por padrão em fontes sem caixa; Shift inverte | Cantos, laterais e imagens espelhadas |
| Arraste fora do canvas | Conversão permite coordenadas externas durante interação | Todos os lados e retorno ao canvas |
| Rotação | Alça própria e modificadores | Pivô não central e fonte espelhada |
| Recorte | Alt com alças em fontes sem caixa delimitadora | Quatro lados, limites e cancelamento |
| Movimento de seleção múltipla | Seleção estendida e posições iniciais por item | Vários itens com bloqueios mistos |
| Contorno de múltipla seleção | Ainda desenha apenas o item principal | Divergência aberta |
| Seleção por área | Implementada por interseção de retângulos | Diverge em fontes giradas; revisar critério nativo de inclusão |
| Acerto em fonte girada | Usa inversa da matriz real do item | Confirmar clique em área vazia ao redor |
| Bloqueio | Movimento/resize recusam itens bloqueados | Grupos com pai bloqueado ainda requerem revisão |
| Histórico | Transformações de mouse e setas usam API de undo/redo | Verificar Ctrl+Z/Y, troca de cena e restauração de recorte |
| Histórico de operações de lista | Adição, remoção, ordem e visibilidade não estão integralmente no histórico | Divergência aberta |
| Ordem de camadas | Lista invertida para refletir ordem visual nativa | Subir/descer com fontes ocultas de outro modo |
| Fonte extra em modo sincronizado | Overlay identificado separadamente do bloco principal | Alternar modo e reabrir OBS |
| Fonte existente versus independente | Transformações são separadas; conteúdo da fonte existente é compartilhado | Falta escolha explícita entre referência e duplicação |
| Menu de transformação | Oferece diálogo nativo; não contém todos os atalhos nativos de ajustar/centralizar/inverter | Divergência aberta |
| Menu de cenas | Não reproduz todo o menu nativo: duplicar, renomear e copiar/colar filtros incompletos | Divergência aberta |
| Organização por arraste | Listas têm drag desativado; usam botões | Divergência aberta |
| Grupos e hierarquia | Enumeração plana, sem árvore de grupos | Divergência aberta |
| Zoom e deslocamento da prévia | Sem wheelEvent e sem navegação equivalente à nativa | Divergência aberta |
| Resolução variável | Várias regras assumem 1080 × 1920 | Aceitável apenas para o canvas fixo atual |
| Persistência | Transformações e configuração são salvas | Reabrir OBS após editar ambos os modos |

## Arquivos inspecionados

- `plugins/obs/src/streaming/vertical-workspace.h`
- `plugins/obs/src/streaming/vertical-output.h`
- `plugins/obs/src/streaming/center-snap.h`
- `plugins/obs/.deps/obs-studio-32.2.1/frontend/widgets/OBSBasicPreview.cpp`
- `plugins/obs/.deps/obs-studio-32.2.1/frontend/widgets/OBSBasic_Scenes.cpp`
- `plugins/obs/.deps/obs-studio-32.2.1/frontend/components/SourceTree.cpp`

## Ordem da próxima validação

1. Seleção, cursores, alças, proporção, arraste fora, recorte e rotação.
2. Encaixe por preferência, centro e desativação temporária com Ctrl.
3. Sobreposição vertical, camadas, bloqueio e propriedades.
4. Desfazer/refazer, troca de modos e reabertura.
5. Resolver as divergências abertas antes de chamar o editor de equivalente ao nativo.
