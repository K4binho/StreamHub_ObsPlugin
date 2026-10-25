---
artifact: acceptance-criteria
version: "1.0"
created: 2026-10-22
status: implemented-local-validation
---

# Saídas horizontal e vertical: implementação e validação

## Contexto

O usuário precisa transmitir para o YouTube sem depender da Twitch principal e enviar composições horizontal e vertical com chaves diferentes. A sincronização deve respeitar o evento selecionado, os dados manuais e os seletores salvos.

## Fluxo principal

### AC-1: Dois destinos

**Given** um destino YouTube horizontal e uma área vertical disponível.

**When** o usuário salva o servidor e a chave horizontal, o servidor e a chave vertical e a área vertical.

**Then** as configurações persistem e a saída vertical ganha um cartão com play/parar independente.

### AC-2: YouTube sem Twitch

**Given** a saída principal está excluída de Iniciar tudo.

**When** o usuário inicia o YouTube individualmente ou usa Iniciar tudo com apenas o YouTube incluído.

**Then** a saída principal permanece parada e o YouTube usa seu próprio encoder.

### AC-3: Seletores persistidos

**Given** destinos incluídos e excluídos de Iniciar tudo.

**When** o usuário salva, sincroniza a conta ou reabre o perfil.

**Then** os seletores mantêm suas preferências; uma sessão nova começa sem transmissão ativa.

### AC-4: Evento exato

**Given** um evento privado selecionado e outro público disponível.

**When** o usuário sincroniza o evento privado.

**Then** a chave retornada pertence ao evento selecionado e sua visibilidade permanece inalterada.

## Casos de borda

### AC-5: Parar durante conexão ou reconexão

**Given** uma saída conectando ou reconectando.

**When** o usuário solicita a parada.

**Then** o botão mostra a parada em andamento, bloqueia um novo início e ignora sinais atrasados de conexão; após oito segundos, a saída auxiliar permite forçar a parada.

### AC-6: Credenciais manuais

**Given** servidor e chave marcados para preservação manual.

**When** a conta sincroniza dados do evento.

**Then** os valores manuais permanecem e o seletor ligado/desligado não muda.

## Erros e recuperação

### AC-7: Configuração inválida

**Given** URL HTTPS/WebRTC, par servidor/chave incompleto, chave vertical igual à horizontal ou área vertical ausente.

**When** o usuário tenta salvar ou iniciar a configuração correspondente.

**Then** aparece uma orientação para corrigir a configuração, sem substituir valores válidos salvos.

### AC-8: Evento removido ou encerrado

**Given** o evento selecionado não está mais disponível para transmissão.

**When** o usuário sincroniza.

**Then** aparece um erro e nenhum outro evento é selecionado silenciosamente.

### AC-9: Falha de gravação

**Given** o perfil não permite gravar as alterações.

**When** o usuário tenta salvar.

**Then** aparece falha de gravação, a configuração em memória é restaurada e nenhum aviso de sucesso é mostrado.

## Critérios não funcionais

### AC-10: Separação de vídeo e estabilidade

**Given** uma saída vertical configurada.

**When** a saída inicia ou termina.

**Then** ela usa a área vertical selecionada, mantém as referências de vídeo enquanto necessária e não altera o encoder horizontal em uso.

### AC-11: Segredos

**Given** chaves configuradas.

**When** os testes e logs são consultados.

**Then** os testes usam chaves fictícias e o relatório não contém credenciais reais.

## Implementação

- `streamhub-output-settings.h`: formulário reutilizável, validação, saída vertical vinculada e rollback de falha ao salvar.
- `output-target-config.h`: serialização de cada saída, incluindo inclusão, formato, área, vínculo e preferência manual; migração de seletores antigos.
- `output-lifecycle.h`: estados de conexão e parada.
- `push-widget.cpp`: encoders independentes a partir do perfil avançado, vídeo da área vertical, sinais vinculados ao widget e à geração da saída, controle individual e encerramento.
- `obs-multi-rtmp.cpp`: inclusão e play/parar da principal; iniciar somente destinos incluídos; preservação de configurações na sincronização.
- Servidor, launcher e dock: ID do evento selecionado enviado e validado de ponta a ponta. Respostas antigas não podem aplicar dados depois de mudar a seleção.
- Bundle 65: distribuição automática do servidor corrigido na DLL.

## Evidência e limites

Validação local: 12 testes Node e 4 grupos CTest (ciclo de vida, persistência, formulário Qt real com host OBS simulado e DSP). O formulário verifica dois destinos, chaves distintas, URL inválida, bloqueio de alteração em uso, falha de gravação e recarga. Os testes usam dados fictícios, sem transmissão externa.

A consulta autenticada ao evento privado selecionado retornou servidor RTMPS válido e chave presente. A conta também possui chaves RTMP separadas com nomes de horizontal e vertical. Essa consulta confirma a seleção e o acesso aos dados, não a reprodução de vídeo.

A DLL foi instalada com backup e hash idêntico ao build. Na inicialização real do OBS, o módulo carregou e extraiu o bundle 65. A chamada autenticada ao servidor instalado confirmou o ID e a privacidade do evento, a igualdade entre a chave horizontal salva e a retornada pela API, a diferença entre as duas chaves e a área vertical configurada. O OBS foi iniciado minimizado, sem comando de transmissão. A saída vertical nova fica inicialmente fora de Iniciar tudo; seu play individual continua disponível.

Antes da preparação local, foram preservados backups da DLL, do arquivo de destinos e das preferências do perfil. Esses backups ficam na instalação local do usuário e não são incluídos no repositório.

Ainda requer validação em sessão real: ingestão simultânea das duas composições pelo YouTube, confirmação de dimensões/áudio no Studio e ciclos repetidos de parar/reiniciar com a GPU e rede reais. Build e conexão RTMP isolada não comprovam essas etapas. O perfil instalado usa modo avançado; a clonagem padrão de encoders neste fluxo depende desse modo.

## Como testar no OBS

No teste de 22/10, os logs confirmaram H.264 em 1920×1080 e 1080×1920, a 15 Mbps por saída, mas registraram 10,7% e 9,1% de perda de quadros por rede. O Studio mostrou sinal Excelente, porém ambas as prévias permaneceram preparando. Isso ainda não valida reprodução nem identifica sozinho a causa da prévia travada.

Foi adicionado bitrate independente por destino (0 herda do OBS), persistido e aplicado ao encoder próprio em CBR. Para o próximo teste controlado, usar 6000 kbps em cada saída e verificar primeiro a horizontal sozinha, depois as duas. O limite não altera o bitrate global do perfil OBS. A reprodução e a saúde do evento devem ser observadas enquanto os sinais estiverem ativos.

1. Na aba Transmissão, selecione explicitamente o evento privado desejado; nas configurações da conta, sincronize esse evento.
2. Abra a engrenagem do destino YouTube horizontal. Confira os campos horizontais e preencha o servidor e a chave **Vertical** do Studio na seção vertical.
3. Selecione a área vertical que contém sua composição 9:16. Salve; o cartão vertical será criado.
4. Para YouTube sozinho, exclua principal/Kick dos seletores de Iniciar tudo ou use os plays individuais dos dois cartões YouTube.
5. Confirme imagem e áudio nos dois painéis do Studio. Teste parar durante conexão, parar normalmente e reiniciar; o play deve voltar somente após o encerramento.
6. Reabra o OBS e confirme os seletores salvos. As transmissões devem continuar paradas até um comando explícito.
