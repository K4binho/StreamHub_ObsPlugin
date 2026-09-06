# StreamHub OBS Plugin (fork do obs-multi-rtmp)

## Atualização de 06/09/2026 — caminho corrigido

O build atual foi compilado com sucesso no Windows, VS Community 2026 e CMake 4.4.3. A DLL está em `build_x64/RelWithDebInfo/obs-multi-rtmp.dll` e foi instalada no OBS portátil em `E:\obs-studio`. A extração inicial de 18 arquivos foi validada no log de 00:00:08. Depois da remoção do relay do bundle, a versão 2 extrai 17 arquivos. O log de 00:24:35 confirma a DLL corrigida, reparação automática dos pacotes, Node iniciado e dock conectado à porta 3000. O usuário confirmou em seguida mensagens `oi` da Twitch e da Kick aparecendo no dock. Transmissão não foi testada.

O caminho retornado pelo OBS é resolvido com `QDir::absolutePath()` no diretório de trabalho do OBS, antes de qualquer mudança de diretório do Node. Tanto o diretório do processo quanto o script precisam ser absolutos. Não voltar a concatenar `../../data/...` ao diretório do servidor nem usar a pasta da DLL como base. Código compartilhado em `src/streamhub-paths.h`; teste de regressão em `tests/paths`.

Teste isolado (Qt disponível no pacote `.deps`):

```powershell
cmake -S tests/paths -B build_path_tests -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="$PWD/.deps/obs-deps-qt6-2026-07-15-x64"
cmake --build build_path_tests --config Release
$env:PATH = "$PWD/.deps/obs-deps-qt6-2026-07-15-x64/bin;$env:PATH"
ctest --test-dir build_path_tests -C Release --output-on-failure
```

O teste passou nesta sessão. As referências históricas abaixo a VS2022, WebSocket e impossibilidade de compilar neste ambiente estão superadas: o dock atual usa long-poll HTTP e a compilação funciona. Veja [STATUS.md](STATUS.md) para o estado consolidado.

## Interface nativa e configuração — 06/09/2026

O dock de chat possui filtros por plataforma, mensagens estruturadas e botão de configuração. Esse formulário edita Twitch/Kick, salva `config.json` de forma atômica e reinicia somente o servidor Node. O dock Múltiplas saídas continua sendo a autoridade das transmissões e agora apresenta os controles existentes em cabeçalho e cartões com o tema StreamHub.

A build com essa interface foi instalada no OBS portátil e o log `2026-09-06 00-35-36.txt` confirmou o carregamento e a conexão local. O envio pelo campo do chat não foi implementado: os conectores atuais apenas leem mensagens públicas, e enviar exige autenticação OAuth por plataforma. A transmissão real ainda não foi testada.

Se o OBS for fechado durante a primeira instalação dos pacotes Node, a pasta `node_modules` pode existir incompleta. O launcher atual só reutiliza a pasta quando também encontra `.dependencies-sha256` com o hash do `package.json`. Sem esse marcador, ele roda `npm install` novamente e se repara sozinho. Deixe o OBS aberto até o dock conectar na primeira execução; se for interrompido, basta abrir novamente.

Este é o fork do [obs-multi-rtmp](https://github.com/sorayuki/obs-multi-rtmp)
com duas coisas a mais:

1. `src/streamhub-launcher.*` — sobe o servidor Node.js (o mesmo projeto
   StreamHub que já tínhamos) como processo filho, assim que o OBS carrega o
   plugin. Instala as dependências (`npm install`) sozinho na primeira vez.
2. `src/streamhub-chat-dock.*` — um dock nativo em Qt (não é navegador/CEF)
   que consulta o servidor por long-poll HTTP e mostra o chat unificado
   dentro do próprio OBS.

O resto (todas as saídas RTMP, a UI de "Adicionar Saída" etc.) é o
obs-multi-rtmp original, sem modificação.

## Por que não incorporamos a UI de configuração (dashboard.html) direto no OBS?

Dava pra fazer isso embutindo um painel CEF (o mesmo motor de navegador que
o OBS usa pras Fontes de Navegador), só que essa é uma API interna do
obs-browser, não documentada nem recomendada pra plugins de terceiros — os
próprios mantenedores do obs-browser dizem isso literalmente no repositório.
Ela pode quebrar a qualquer atualização do OBS sem aviso. Preferi manter o
dashboard como página web separada (acessada por fora, ou via Custom Browser
Dock manual, como já configuramos antes) e deixar o plugin C++ só com a
parte que realmente precisa ser nativa: as saídas RTMP e o chat ao vivo
dentro do OBS.

## Como o Node.js é resolvido (v2 — corrigido)

As duas limitações da v1 foram resolvidas com `src/streamhub-node-provision.*`:

- **Não trava mais o OBS.** Todo o fluxo (resolver/baixar Node, `npm
  install`) usa `QProcess`/`QNetworkAccessManager` assíncronos — `Start()`
  retorna na hora. A dock "StreamHub Chat" mostra uma label de status
  ("Baixando runtime... 42%", "Instalando dependências...", etc.) enquanto
  isso roda em segundo plano; o resto do OBS fica 100% liberado.
- **Não precisa mais de Node.js instalado na máquina.** Ordem de resolução:
  1. Já resolvido antes (`data/node-runtime/resolved-node-path.txt`) → usa
     na hora, sem rede.
  2. Node.js do sistema (PATH) → usa esse, se existir.
  3. Nenhum dos dois → baixa automaticamente o runtime portátil oficial de
     nodejs.org (~30 MB) pra `data/node-runtime/`, extrai com a ferramenta
     nativa do SO (`Expand-Archive` no Windows, `tar` no mac/Linux — nenhuma
     dependência nova) e usa esse dali em diante.
- A versão do Node baixada é fixada em `kNodeVersion` dentro de
  `streamhub-node-provision.h` (hoje `20.11.1`, LTS "Iron"). Vale revisar essa
  constante de tempos em tempos.

O fluxo foi compilado e executado nesta máquina. Ainda falta o teste em um
Windows limpo, sem Node instalado, copiando somente a DLL. Nesse cenário,
acompanhar as linhas `[streamhub]` no log até o dock conectar.

Nome do dock/arquivo de config ainda estão em inglês/genérico
(`obs-multi-rtmp`) por baixo dos panos — não precisa mudar isso pra
funcionar, é só cosmético.

## Build

Isso usa o **obs-plugintemplate** (o mesmo sistema de build do
obs-multi-rtmp original), que baixa e configura o SDK do OBS, Qt6 e
dependências automaticamente via CMake Presets. Não dá pra compilar isso
aqui no sandbox (não tem OBS instalado, nem Qt, nem interface gráfica pra
testar) — mas o fluxo abaixo funciona numa máquina Windows normal.

### Pré-requisitos (Windows)

1. Visual Studio Community 2026 com a carga de
   trabalho "Desenvolvimento para desktop com C++"
2. [CMake](https://cmake.org/download/) 3.28+
3. [Git](https://git-scm.com/)

Node.js **não é mais pré-requisito** — se não estiver instalado, o próprio
plugin baixa um runtime portátil sozinho na primeira abertura do OBS (ver
seção acima). Se você já tem Node.js instalado, ele é reaproveitado direto,
sem download nenhum.

### Passos

```powershell
cd streamhub-obs-plugin

# Baixa OBS Studio + Qt6 + dependências pré-compiladas automaticamente
# (config vem do buildspec.json, já ajustado pra OBS 32.2.1)
cmake --preset windows-x64

# Compila
cmake --build --preset windows-x64 --config RelWithDebInfo
```

Isso gera o plugin em algo como
`build_x64\RelWithDebInfo\streamhub-obs-plugin.dll` (o nome exato do
`.dll`/pasta de dados segue o que está no `buildspec.json`, campo `name`).

### Instalando pra testar

1. Copie o `.dll` gerado para
   `%ProgramData%\obs-studio\plugins\<nome-do-plugin>\bin\64bit\`
2. Copie a pasta `data/` (que já inclui nosso `data/streamhub-server/`) para
   `%ProgramData%\obs-studio\plugins\<nome-do-plugin>\data\`
3. Abra o OBS. Nos logs (Help → Log Files → Ver Log Atual), procure por
   linhas com `[streamhub]` pra confirmar que o servidor Node subiu.
4. Dois docks novos devem aparecer em **View → Docks**: "Outputs" (do
   obs-multi-rtmp original) e "StreamHub Chat" (o nosso).

### Configurando o chat e as saídas

Use a engrenagem no cabeçalho do dock **StreamHub Chat** para ativar e
informar os canais Twitch/Kick; **Salvar e aplicar** reinicia somente o
serviço de chat. Use **Múltiplas saídas → Adicionar destino** para URL,
stream key e encoder das transmissões.

## Próximos passos possíveis

- Trocar a edição manual do `config.json` por uma aba de configuração
  dentro do próprio dock nativo (mais Qt widgets, reaproveitando os campos
  que já existem no dashboard.html)
- Assinar/instalador (.exe via NSIS, o `installer.nsi` original já dá a
  base) incluindo tudo isso num único instalador clicável
