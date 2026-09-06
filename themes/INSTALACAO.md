# K4binho — Má Fase para OBS Studio

Tema visual escuro, urbano e profissional criado para o StreamHub. A base de compatibilidade dos componentes é o tema Yami, de Warchamp7, incluído no OBS Studio; cores, estados, hierarquia e identidade visual foram adaptados para a marca K4binho — Má Fase.

## Instalação automática pela DLL do StreamHub

1. Copie `obs-multi-rtmp.dll` para `obs-plugins\64bit` na instalação do OBS.
2. Abra o OBS uma vez. O plugin extrairá o tema e seus recursos para `data\obs-studio\themes`.
3. Feche e abra o OBS novamente, pois a lista de temas é carregada antes dos plugins.
4. Acesse **Configurações → Aparência → Tema** e selecione **K4binho — Má Fase**.

Atualizações futuras do tema são aplicadas automaticamente pela DLL.

No Windows, a DLL também aplica a paleta navy/ciano à barra de título das janelas nativas quando este tema está ativo, incluindo **Informações da transmissão**. O conteúdo do Chat e do Feed de atividade da Twitch é uma página web externa e não recebe estilos `.qss`; para uma interface uniforme, oculte esses dois painéis em **Painéis (D)** e use **StreamHub Chat · K4**.

## Instalação manual pelo ZIP

1. Feche o OBS.
2. Extraia o conteúdo da pasta `theme` do ZIP dentro de `data\obs-studio\themes`.
3. Confirme que estes caminhos existem:
   - `data\obs-studio\themes\K4binho_Ma_Fase.obt`
   - `data\obs-studio\themes\K4binho-Ma-Fase\crown-watermark.png`
4. Abra o OBS e selecione **K4binho — Má Fase** em **Configurações → Aparência → Tema**.

O arquivo `K4binho_Ma_Fase.qss` acompanha o pacote para instalações antigas e personalização manual. O OBS 32 usa preferencialmente o arquivo `.obt`.

## Fundo de cena 1920×1080

O arquivo `K4binho-Ma-Fase\backgrounds\ma-fase-background-1920x1080.png` é separado do estilo da interface para não prejudicar a leitura.

Para usá-lo:

1. Na cena desejada, clique em **Adicionar fonte → Imagem**.
2. Escolha `ma-fase-background-1920x1080.png`.
3. Mova a fonte para o final da lista de fontes.
4. Use **Transformar → Ajustar à tela** se necessário.

## Paleta

| Uso | Cor |
| --- | --- |
| Fundo principal | `#080D14` |
| Fundo dos painéis | `#101A26` |
| Painel elevado | `#162738` |
| Borda discreta | `#27465A` |
| Seleção | `#075579` |
| Azul principal | `#087EBA` |
| Ciano de destaque | `#00C8FF` |
| Texto principal | `#F1FAFF` |
| Texto secundário | `#91B3C7` |
| Verde “Ao Vivo” | `#25D99A` |
| Amarelo de atenção | `#F6BD3A` |
| Vermelho crítico | `#D94B59` |

## Remoção

Feche o OBS e remova `K4binho_Ma_Fase.obt`, `K4binho_Ma_Fase.qss` e a pasta `K4binho-Ma-Fase` de `data\obs-studio\themes`.
