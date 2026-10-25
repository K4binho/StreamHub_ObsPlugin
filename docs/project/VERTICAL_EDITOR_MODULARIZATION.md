---
artifact: acceptance-criteria
version: "1.0"
created: 2026-10-24
status: automated-validation-passed-runtime-pending
---

# Acceptance Criteria: modularização do editor vertical

## Story Context

Separar responsabilidades do editor sem mudar seus controles, cenas, persistência ou renderização.

## Happy Path

### AC-1: edição preservada
**Given** uma coleção existente com fontes verticais.
**When** o usuário seleciona, move ou redimensiona uma fonte.
**Then** os comandos mantêm o comportamento anterior à reorganização.

## Edge Cases

### AC-2: sincronização
**Given** cenas vinculadas e sobreposições independentes.
**When** a cena principal muda.
**Then** o vínculo e as sobreposições mantêm seu comportamento anterior.

## Error States

### AC-3: estado ausente
**Given** nenhuma cena vertical disponível.
**When** o editor é atualizado.
**Then** mantém os mesmos tratamentos de ausência de cena, sem encerrar o OBS.

## Non-Functional Criteria

### AC-4: compilação e regressões
**Given** o código separado por responsabilidade.
**When** o projeto é compilado e a suíte existente executada.
**Then** não há símbolos ausentes ou duplicados e os seis testes passam.

### AC-5: integridade da extração
**Given** os métodos existentes antes da reorganização.
**When** são distribuídos entre os módulos.
**Then** todos os corpos são preservados, exceto a remoção do especificador inline.

## Notes

- Escopo: editor vertical. Outros arquivos extensos serão tratados separadamente, sem ampliar esta refatoração.
- Não altera configurações nem instala DLL em uma sessão aberta.
- Testes automatizados não substituem a validação visual no OBS.

## Organização

- `vertical-workspace.h/.cpp`: API pública, coordenação, configuração e ações horizontais.
- `vertical-preview.h/.cpp`: superfície de exibição, ciclo de vida do display, renderização e indicadores.
- `vertical-interaction.cpp`: seleção, cursor, mouse, teclado, encaixe e transformações interativas.
- `vertical-panels.cpp`: criação dos painéis, barras e conexões de controles.
- `vertical-scenes.cpp`: sincronização, ordenação, seleção e comandos de cenas.
- `vertical-sources.cpp`: fontes, visibilidade, bloqueio, menus e comandos de edição.
- `vertical-editor-widgets.h`: componentes visuais reutilizados pelas listas e área de prévia.
- `vertical-editor-core.h`: operações compartilhadas, estado e undo/redo já separados anteriormente.

## Limites

- A coordenação continua na mesma classe; não foram criados serviços ou interfaces artificiais.
- Os métodos foram extraídos para unidades de compilação reais, não cabeçalhos incluídos em cadeia.
- Os 62 corpos de métodos foram comparados integralmente com o original durante a extração, sem mudança de lógica.
- O teste estrutural impede recolocar implementações na API e verifica a inclusão dos módulos na compilação.

## Validação executada

- Compilação RelWithDebInfo da DLL com as seis unidades novas: passou.
- CTest: 7/7 passaram, incluindo estrutura dos módulos, editor com captura ativa, encaixe, ciclo de saída, configuração e áudio.
- `git diff --check`: passou, apenas avisos de conversão de fim de linha em arquivos existentes.
- DLL instalada preservada. A versão reorganizada foi gerada no diretório de build, sem reiniciar o OBS.
- Validação visual e transmissão real da DLL reorganizada: não executadas.
