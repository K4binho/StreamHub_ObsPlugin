# Polimento e regressões

Data: 2026-10-24

## Problem Recap
O usuário confirmou funcionamento do Twitch Dual Format. A próxima rodada prioriza controles previsíveis e evidência de testes, preservando a sessão validada.

## Proposed Solution
Consolidar o resultado real no README e documentação; impedir que Iniciar tudo inclua uma filha oculta quando seu cartão pai estiver excluído; testar grupo ligado/desligado, pai ausente e configurações incompletas.

## Key Features
1. Seletores de grupo respeitados também no início conjunto.
2. Regressões para casos de inclusão de saídas.
3. Registro explícito do sucesso real do Dual Format.

## Success Metrics
Compilação e suítes sem regressões; pai desligado implica filha excluída de Iniciar tudo. Teste de recepção no celular confirmado pelo usuário; chat, moderação e teste prolongado permanecem separados.

## Trade-offs Considered
Não redesenhar a interface nem trocar encoders nesta rodada. A opção startWithParent controla o play vinculado, enquanto Iniciar tudo considera os seletores de inclusão.

## Risks & Mitigations
Saída filha oculta indevidamente iniciada: verificar seleção do pai no mesmo caminho usado pelo botão global e cobrir com teste. Preservar parada de saídas ocupadas mesmo após desmarcar inclusão. Não instalar durante sessão aberta.

## Next Steps
1. Atualizar a evidência de Twitch.
2. Corrigir inclusão no início conjunto e testar estados limítrofes.
3. Compilar e executar suítes Node/CTest.
4. Validar posteriormente parada/reconexão, chat, moderação e estabilidade em uso real.

## Evidência do teste real
Log local 2026-10-24 18-55-30.txt: encoders multifaixa iniciados às 18:57:25 com resoluções 1920x1080, 1280x720, 640x360, 1080x1920, 720x1280 e 360x640. Encerramento às 19:01:14 registrou 63664 quadros enviados de 67455 tentativas e 3791 quadros perdidos por banda/conexão (5,6%). Codificação e renderização registraram aproximadamente 0,0% de perdas. Estes números são contadores da saída multifaixa desse teste, não duração por quadro ou previsão de capacidade da rede.

Próxima prioridade: avaliar orçamento agregado de banda no Enhanced Broadcasting e comparar um teste com limites menores usando Twitch Inspector. Não alterar o perfil durante uma sessão ativa nem afirmar que reduzir número de faixas sozinho resolverá o gargalo. Recepção funcional já confirmada; estabilidade ainda precisa melhorar.

## Validação automatizada

Atualização de 25/10/2026: o teste de configurações usava o ponteiro fictício 1 para canvas, mas não substituía obs_canvas_set_name. A migração do nome disparava a função real do libobs e causava acesso inválido. Completar o host double com estado de nome, simulação de criação/renomeação e verificações de nome novo e migração. Executar novamente todas as suítes antes de publicar o código; não tratar o crash do teste como falha confirmada no OBS real.

Correção validada em 25/10/2026: criação e renomeação do canvas agora pertencem ao host double. Teste de configurações aprovado incluindo migração do nome antigo, preservação de UUID, rejeição de chave duplicada e rollback após falha de gravação. CTest 7/7, servidor local 18/18 e broker OAuth 8/8 aprovados. Nenhum teste envia dados de transmissão nem executa moderação nas contas reais.
Compilação RelWithDebInfo aprovada; servidor local 18/18 aprovado. Regressões de inclusão agora cobrem pai desligado, vínculo ausente, saída independente, grupo totalmente excluído, grupo vazio e entradas nulas. A nova compilação permanece separada da DLL carregada no OBS.

CTest: 7/7 aprovado após a mudança de inclusão no início conjunto.

## Preparação da comparação de upload

- Teste anterior: bitrates de vídeo 7500+3500+500+5000+3000+450 = 19950 Kbps; duas faixas AAC de 160 Kbps. Total nominal de 20270 Kbps antes da sobrecarga de rede. Conexão por Ethernet, não Wi-Fi.
- Perfil salvo pela interface do OBS com teto agregado de 16000 Kbps e máximo de quatro faixas, ambos com Automático desmarcado. Enhanced Broadcasting e canvas StreamHub permanecem selecionados.
- Backup local de basic.ini realizado antes da alteração. Apenas a configuração do perfil ativo foi alterada; não tornar esses limites padrão global do plugin.
- Objetivo: comparar perdas com o teste anterior sob carga semelhante. O teto não é medição da velocidade da conexão e a Twitch ainda precisa aceitar/negociar a nova combinação de faixas e resoluções.
- Estado final: zero saídas ativas; não foi iniciada outra live. As prévias estavam sem conteúdo no momento da conferência, portanto confirmar cena e captura antes do próximo envio.
- No próximo teste, iniciar somente o cartão Twitch por pelo menos cinco minutos, conferir ambos os formatos e comparar contadores de queda e resoluções negociadas. Se houver redução de qualidade indesejada ou rejeição, restaurar Automático nos dois limites ou recuperar os valores do backup.
- Referências: https://help.twitch.tv/s/article/dual-format-vertical-video e https://obsproject.com/kb/stream-connection-troubleshooting/.
