# StreamHub

Chat unificado (Twitch + YouTube + Kick + TikTok) e multistream, pra usar com o OBS.
É um ponto de partida — funcional, mas simples — não uma cópia do Aitum.

## O que isso faz

1. **Overlay de chat unificado**: junta o chat das 4 plataformas e mostra tudo
   numa única fonte de navegador, que você adiciona no OBS.
2. **Multistream**: você transmite uma vez pro StreamHub (rodando na sua
   máquina), e ele reenvia sem recodificar pras 4 plataformas ao mesmo tempo.

## Pré-requisitos

- [Node.js](https://nodejs.org) 18 ou mais recente
- [ffmpeg](https://ffmpeg.org/download.html) instalado e no PATH do sistema
  (necessário só se for usar o multistream)
- OBS Studio

## Instalação

```bash
npm install
cp config.example.json config.json
```

Edite o `config.json` com seus dados (veja abaixo, plataforma por plataforma).

```bash
npm start
```

Isso vai:
- subir o painel/overlay em `http://localhost:3000`
- (se configurado) subir o servidor RTMP local na porta 1935

## Configurando cada plataforma no config.json

### Twitch
Só precisa do nome do canal. Lê o chat público, sem precisar de login/token.
```json
"twitch": { "enabled": true, "channel": "seucanal" }
```

### YouTube
Precisa de uma API Key do Google Cloud (ative a "YouTube Data API v3" num
projeto no [Google Cloud Console](https://console.cloud.google.com/)) e do
`videoId` da live **enquanto ela estiver ao vivo** (o `videoId` muda a cada
live nova — é aquele código depois de `watch?v=` na URL).
```json
"youtube": { "enabled": true, "apiKey": "...", "videoId": "..." }
```

### Kick
Não-oficial (a Kick não publica API de chat). Só o nome do canal já costuma
bastar; se a busca automática falhar (a Kick às vezes bloqueia via
Cloudflare), pegue o `chatroom_id` manualmente em
`https://kick.com/api/v2/channels/SEUCANAL` e cole em `chatroomId`.
```json
"kick": { "enabled": true, "channel": "seucanal" }
```

### TikTok
Não-oficial. Só funciona com a live já no ar. Só precisa do @ do usuário
(sem o @).
```json
"tiktok": { "enabled": true, "username": "seuusuario" }
```

## Usando a overlay no OBS

1. Rode `npm start`
2. No OBS: **Fontes → + → Fonte de Navegador**
3. URL: `http://localhost:3000/overlay.html`
4. Largura/altura: o que couber na sua cena (a overlay já tem fundo
   transparente)

## Dashboard de configuração (em vez de editar o config.json na mão)

Acesse `http://localhost:3000/dashboard.html` — dá pra ativar/desativar cada
plataforma de chat, preencher canal/API key/videoId, e gerenciar as saídas do
multistream (adicionar, editar, remover) numa interface, parecida com a do
Aitum. Ao salvar, ele reescreve o `config.json` — só precisa reiniciar o
`npm start` pra aplicar (as conexões de chat e o relay só são abertas uma vez,
quando o servidor sobe).

**Dica**: no OBS, vá em **View → Docks → Custom Browser Docks**, dê um nome
(ex: "StreamHub") e cole essa mesma URL. Isso encaixa o dashboard como um
painel dentro do próprio OBS, do lado das outras abas — sem precisar deixar
uma aba do navegador aberta por fora.

## Sobre o multistream nativo (sem relay)

O que fizemos aqui (`server/relay.js`) é um servidor RTMP local que reenvia
via ffmpeg — funciona, mas depende desse servidor estar de pé o tempo todo.
Se você quiser o mesmo esquema que o Aitum usa (saída nativa dentro do
próprio OBS, compartilhando o encoder, sem servidor no meio), isso exige um
plugin C++ contra o SDK do OBS — não dá pra fazer em JS/Python, é uma
limitação do próprio OBS. A boa notícia é que já existe um plugin gratuito
e open-source que faz exatamente isso: **obs-multi-rtmp**
(https://github.com/sorayuki/obs-multi-rtmp). Instala, adiciona os destinos
lá, e você tem a mesma robustez do Aitum de graça — sem precisar do nosso
relay.js.

## Usando o multistream no OBS

1. Preencha `config.rtmp` no `config.json` com a stream key de cada
   plataforma (Twitch, YouTube e TikTok você pega no painel de criador de
   cada uma; a da Kick fica em Configurações → Stream).
2. No OBS: **Configurações → Stream → Serviço: Personalizado**
   - Servidor: `rtmp://localhost:1935/live`
   - Chave de Stream: o mesmo valor de `rtmp.streamKey` no config.json
     (por padrão, `obs`)
3. Comece a transmitir no OBS normalmente. O StreamHub detecta o início e
   dispara o reenvio pras 4 plataformas.

**Atenção**: como não há recodificação (`-c copy`), suas configurações de
bitrate/resolução no OBS precisam já estar dentro do que cada plataforma
aceita. Isso mantém a qualidade e usa pouca CPU, mas significa que se você
configurar algo fora do padrão de uma plataforma, pode dar erro só naquele
destino.

## O que ainda não tem (próximos passos possíveis)

- Interface de configuração (hoje é só editar o JSON na mão)
- Enviar mensagens de volta pro chat de cada plataforma (hoje é só leitura)
- Detecção automática de quando uma live termina numa plataforma específica
- Fila/anti-flood pra chats muito rápidos
- Suporte a emotes (BTTV/7TV na Twitch, por exemplo)

## Aviso sobre Kick e TikTok

Essas duas integrações usam engenharia reversa de protocolos internos, não
APIs públicas oficiais. Elas podem parar de funcionar se as plataformas
mudarem algo, sem aviso prévio. Isso é uma limitação de qualquer ferramenta
desse tipo (incluindo as pagas), não só deste projeto.
