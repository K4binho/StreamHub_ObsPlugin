# Solution Brief: Twitch Dual Format com canvas StreamHub

Data: 2026-10-24

## Problem Recap
O envio RTMP horizontal tradicional não publica o canvas vertical como versão mobile da mesma live. O nome atual do canvas também inclui um identificador técnico desnecessário na lista do OBS.

## Proposed Solution
Usar Enhanced Broadcasting e a seleção nativa de canvas adicional do OBS. Renomear o canvas gerenciado para StreamHub preservando UUID, cenas e vínculo existente.

## Key Features
1. Reutilizar o canvas 1080x1920 já renderizado pelo editor.
2. Identificar a composição como StreamHub nas configurações do OBS.
3. Preservar separação entre a saída principal Twitch e os destinos RTMP auxiliares.

## Success Metrics
Compilação e regressões aprovadas; nome StreamHub exibido; canvas correto selecionado em MultitrackExtraCanvas. Envio aceito pela Twitch e versão vertical visível no celular são critérios de execução separados.

## Trade-offs Considered
O OBS já implementa a negociação multifaixa com a Twitch: não duplicar esse protocolo nem criar uma segunda conexão RTMP com a mesma chave. A seleção adicional deve apontar para o UUID existente, não um canvas substituto.

## Risks & Mitigations
Canvas antigo pode continuar referenciado: normalizar apenas o nome do canvas gerenciado, sem alterar UUID. Enhanced Broadcasting pode requisitar mais encoders e upload: verificar logs e aceitação da configuração antes de afirmar sucesso no celular. Não modificar transmissão ativa.

## Next Steps
1. Aplicar nome StreamHub a canvases novos e antigos.
2. Testar que a mudança preserva UUID e não renomeia canvas externo.
3. Compilar, instalar com OBS fechado e conferir seleção nativa.
4. Testar Twitch e conferir negociação dos dois formatos e recepção mobile.

## Evidência técnica
- OBSBasicSettings_Stream.cpp enumera canvases persistentes na lista adicional.
- BasicOutputHandler.cpp lê Stream1/MultitrackExtraCanvas e passa o UUID ao MultitrackVideoOutput.
- Perfil local já contém EnableMultitrackVideo=true; a captura fornecida pelo usuário mostra a seleção do canvas StreamHub Vertical.
- Fontes: https://help.twitch.tv/s/article/dual-format-vertical-video e https://blog.twitch.tv/en/2026/06/17/introducing-dual-format-and-2k-streaming-on-twitch/.

## Validação após instalação
- DLL instalada com backup e hash idêntico ao build; 7/7 testes aprovados.
- OBS reaberto com prévias horizontal e vertical funcionando.
- Conferência visual em Configurações > Transmissão: Twitch conectado, Enhanced Broadcasting marcado, área adicional StreamHub selecionada.
- Configuração persistida aponta para o UUID 1783cb3c-a988-4354-abc6-83867103a421, o mesmo canvas existente antes da troca de nome.
- Nenhuma saída ativa durante a conferência inicial.
- Após a preparação, o usuário confirmou o funcionamento do Dual Format no teste real: “Boa! funcionou bem”. O resultado é evidência fornecida pelo usuário; não foram medidos tempo de reconexão, estabilidade prolongada, chat ou moderação.
- Log do teste também confirma seis encoders multifaixa, sendo três horizontais e três verticais. Encerramento registrou 5,6% de perda por conexão/banda; o envio dos formatos funciona, mas estabilidade de upload segue como prioridade. Veja [polimento e regressões](POLISH_VALIDATION.md).
