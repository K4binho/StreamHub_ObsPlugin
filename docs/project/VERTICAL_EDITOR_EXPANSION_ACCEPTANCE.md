---
artifact: acceptance-criteria
version: "1.0"
created: 2026-10-24
status: in-validation
---

# Editor vertical: expansão de paridade

## Story Context

Editar o layout vertical com operações familiares do OBS, preservando o horizontal. Sincronização mantém o bloco principal inteiro; sobreposições verticais continuam separadas. Não implica implementar toda a aplicação OBS ou reinstalar Aitum.

## Happy Path

### AC-1: Seleção e transformação conjunta

**Given** duas fontes visíveis e desbloqueadas selecionadas com Ctrl ou por área.

**When** mover, redimensionar ou girar uma das selecionadas.

**Then** as selecionadas acompanham a transformação, com contornos e sem alterar o layout horizontal.

### AC-2: Grupos

**Given** fontes do mesmo grupo pai selecionadas.

**When** agrupar, expandir/recolher, selecionar um filho ou desagrupar.

**Then** a lista representa a hierarquia e permite editar o grupo inteiro ou o filho explicitamente selecionado.

### AC-3: Histórico

**Given** uma edição de transformação, adição, remoção, ordem, nome, visibilidade ou bloqueio.

**When** desfazer e refazer.

**Then** os estados anteriores e posteriores são recuperados, incluindo fontes removidas, grupos e cenas exclusivas.

### AC-4: Comandos e atalhos

**Given** uma seleção no canvas ou na lista.

**When** usar copiar/colar, duplicar, renomear, ajustar, esticar, centralizar, inverter, girar ou redefinir.

**Then** a operação é aplicada à seleção por um caminho comum de comandos.

### AC-5: Criação e independência

**Given** uma cena vertical.

**When** adicionar uma fonte nova, referência ou cópia independente.

**Then** a fonte aparece na lista e no canvas; somente a referência compartilha propriedades com a original.

### AC-6: Navegação e organização

**Given** canvas e listas abertos.

**When** usar roda, botão do meio, tecla 0 ou arrastar uma linha.

**Then** zoom, deslocamento, retorno ao enquadramento e ordenação funcionam sem mover fontes acidentalmente.

## Edge Cases

**Given** um filho com pai bloqueado ou pai e filho simultaneamente selecionados.

**When** manipular a seleção.

**Then** o bloqueio do pai é respeitado e a transformação não é aplicada duas vezes ao filho.

**Given** uma fonte espelhada, girada, recortada ou com caixa delimitadora.

**When** copiar, colar e desfazer/refazer.

**Then** posição, escala, rotação, recorte, alinhamento e limites são preservados.

**Given** modo sincronizado.

**When** tentar reordenar ou renomear cenas vinculadas.

**Then** a ordem e o nome seguem o principal; cenas exclusivas mantêm organização própria.

## Error States

**Given** uma fonte que o OBS não permite duplicar.

**When** pedir uma cópia independente.

**Then** não é apresentada silenciosamente uma referência como se fosse independente.

**Given** nomes já usados ou cancelamento de diálogo.

**When** adicionar ou renomear.

**Then** a operação é rejeitada ou cancelada sem alterar o estado existente.

## Non-Functional Criteria

**Given** alterações salvas no vertical.

**When** reiniciar o OBS.

**Then** composição, ordem, bloqueios, visibilidade e sincronização são preservados.

**Given** uma transmissão horizontal ou vertical ativa.

**When** navegar e editar a outra composição.

**Then** o editor não inicia, encerra ou troca destinos de transmissão por conta própria.

## Evidência e limites

- Ajuste de preferência confirmado pelo usuário: contorno, alças e medidas permanecem enquanto a fonte está selecionada, inclusive depois de soltar o botão esquerdo. Setas movem 1 px; Shift + setas, 10 px, tanto no canvas quanto na lista de fontes.
- Regressão de seleção corrigida: reconstruir a lista com sinais bloqueados agora atualiza também a seleção do item libobs; o contorno aceita o identificador ativo da seleção como salvaguarda.
- Novo teste gráfico: um consumidor de saída recebe quadros NV12 de um canvas real; uma sobreposição é adicionada e movida após iniciar a captura. A validação compara pixels, não apenas a presença da fonte. Este teste não substitui recepção H.264/RTMP no YouTube.
- Prévia passou a desenhar a textura do mix do canvas, como a prévia nativa, em vez de renderizar a árvore de fontes novamente por um caminho separado. Contornos continuam exclusivos do display, fora da transmissão.
- Teste gráfico e movimentação de 1/10 px também executados com as DLLs do OBS portátil 33.0.0; sobreposição adicionada/movida alcançou os quadros de saída. A ausência relatada no player do YouTube ainda não foi reproduzida neste teste local.

- Teste `streamhub-vertical-editor`: usa libobs real, sem rede e sem credenciais, com coordenadas absolutas; cobre histórico de transformações/bloqueio/remoção, grupos, trava herdada, nome, estado oculto, clipboard e identidade independente.
- Não substitui validação visual no OBS 33 nem comprova transmissão, sensibilidade de encaixe, navegação ou persistência após reinício.
- Canvas suportado nesta etapa: fixo em 1080 × 1920.
- Arraste de fontes ordena itens no mesmo pai; mover entre grupos por arraste não foi implementado nesta etapa.
