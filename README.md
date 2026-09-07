# StreamHub OBS Plugin

Este projeto é uma modificação do [obs-multi-rtmp](https://github.com/sorayuki/obs-multi-rtmp), criado por **SoraYuki**. O trabalho original das múltiplas saídas foi preservado e recebeu melhorias de **K4binho** para formar o StreamHub:

- visual escuro integrado ao OBS para chat e múltiplas saídas;
- chat unificado com leitura de Twitch e Kick e envio autenticado pela Twitch diretamente no dock;
- configuração dos canais, moderação, ajustes administrativos e recompensas dentro do **StreamHub Chat · K4**;
- servidor de chat e recursos embutidos na DLL;
- instalação autorreparável, sem a antiga dependência manual do QtWebSockets;
- correções de inicialização, caminhos e dependências no Windows/OBS portátil.
- painel de saídas redesenhado com paleta StreamHub, cartões por plataforma, interruptores coloridos por serviço e configuração expansível;
- servidor RTMP e stream key protegidos por padrão, com controles para visualizar/copiar e validação antes de iniciar;
- modelos de destino para Twitch, Kick, YouTube, TikTok, Facebook e RTMP personalizado;
- **Iniciar tudo** aciona primeiro a transmissão principal do OBS e, depois que ela estiver ativa, inicia somente os destinos marcados;
- a transmissão principal do OBS aparece como o primeiro cartão fixo, acima das saídas adicionais;
- textura StreamHub no fundo e coroa K4binho usada somente como marca d'água discreta.
- identidade visual inspirada nas artes K4binho, com textura mais visível, logotipo K4 no chat e câmera no cabeçalho das saídas;
- tema completo **K4binho — Má Fase** embutido na DLL, com instalação automática, versão `.qss`, pacote `.obt` para OBS 32 e fundo de cena 1920×1080;
- overlay transparente em `http://127.0.0.1:3000/overlay.html`, pronto para Fonte de navegador, com cor e ícone por plataforma, badges, destaque de menções e remoção automática;
- aba **Overlay** nas configurações do chat com tempo das mensagens, nome do canal, filtros de comandos por plataforma, cópia da URL e prévia;
- alertas comuns de online/offline ficam ocultos; somente a reinicialização efetiva do chat é informada, e o auto-scroll permanece travado enquanto o streamer lê mensagens antigas;
- destinos RTMP personalizados agora podem escolher ícone e cor; o painel mostra a banda agregada das saídas adicionais ativas;
- entradas do plugin no menu **Painéis** recebem identificação K4 e o tema aplica a barra nativa escura às janelas abertas no Windows;
- painel **Informações de transmissão K4** com cartão da conta Twitch, autorização persistente, prévia da live, contadores e busca visual de jogo/categoria;

Este plugin é fornecido gratuitamente. Se quiser apoiar o trabalho:

- **StreamHub e melhorias de K4binho:** [LivePix](https://livepix.gg/k4binho)
- **Projeto original de SoraYuki:** [PayPal](https://paypal.me/sorayuki0)

Redes do K4binho: [Twitch](https://www.twitch.tv/k4binho) · [Kick](https://kick.com/k4binho) · [YouTube](https://www.youtube.com/@k4binho)

## Overlay do chat na transmissão

1. Abra a engrenagem do **StreamHub Chat** e selecione a aba **Overlay**.
2. Ajuste o tempo na tela, o nome usado para destacar menções e os filtros de comandos iniciados por `!`.
3. Clique em **Copiar URL**.
4. No OBS, adicione uma **Fonte de navegador**, cole a URL e use, por exemplo, 540×900.

A URL aceita ajustes temporários, úteis para fontes diferentes: `?duration=30&channel=k4binho&mentions=1`. O fundo continua transparente.

## Painéis nativos do OBS

Em **Painéis**, o plugin registra três entradas com a marca K4:

- **Múltiplas saídas · K4:** transmissão principal primeiro, seguida dos destinos adicionais.
- **StreamHub Chat · K4:** leitura, filtros e envio. O botão **ADM** abre moderação e recompensas.
- **Informações de transmissão K4:** conta Twitch e edição dos dados da live com prévia.

Na aba **Todos**, uma mensagem enviada pelo StreamHub aparece uma única vez com o selo **Todos**, mesmo quando as plataformas devolvem cópias da mesma mensagem. O conteúdo do Chat e do Feed de atividade nativos da Twitch é uma página web e não recebe o stylesheet Qt; o StreamHub Chat pode substituí-los na disposição do OBS.

TikTok permanece marcado como **experimental** até um teste ponta a ponta em uma live real. Facebook está disponível como destino RTMP, mas o conector de chat ainda está no roadmap e não é anunciado como suportado.

## [Homepage original / 主页](https://sorayuki.github.io/obs-multi-rtmp)

## 为什么首页是日语？ / Why is the homepage in Japanese?

因为最初是做给管人用的。

Because it's originally made for virtual Youtubers (VTubers).

# 声明 

近日发现百度贴吧有个叫 maggot 的用户在售卖此插件。咸鱼上也有，没得救了。 

本插件免费使用，作者不收取费用。 

举报之后百度贴吧找我要软件著作权证明，累不爱。 


# Announcement

This plugin is provided for free, without a fee. 

Recently a Baidu Tieba account 'maggot' is selling this plugin. Please, don't buy it.


# お知らせ

本プラグインは無償で提供されるものです。

最近、Baidu Tiebaに「maggot」というアカウント名のユーザーがこのプラグインを販売する行為をしています。

決して購入はしないでください。


# Donate

如果你觉得这个工具很有用想要捐赠，这里是链接。注意：这不是提需求的渠道。

このツールの開発に支援もとい投げ銭をしたいと思った方は以下のリンクからお願いします。(機能のリクエストは受け付けていません)

If you find this tool useful and want to doante, here is the link. (Please do not donate for feature requests.)

## [paypal / 贝宝](https://paypal.me/sorayuki0)

## alipay / 支付宝

![alipay](./docs/zhi.png) 

## wechat / 微信
![wechat](./docs/wechat.jpg)

## Build

This project uses obs-plugintemplate.   
Please refer to obs-plugintemplate to understand how it works.
# StreamHub_ObsPlugin
# StreamHub_ObsPlugin
