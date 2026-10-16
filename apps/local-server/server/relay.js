const NodeMediaServer = require('node-media-server');
const { spawn } = require('child_process');

/**
 * Sobe um servidor RTMP local. O OBS transmite PRA CÁ (não direto pra Twitch/
 * YouTube/etc). Quando o OBS começa a publicar, este servidor abre um
 * processo ffmpeg por destino, copiando o stream sem recodificar (-c copy)
 * e empurrando pra cada plataforma. É o mesmo princípio usado por
 * ferramentas como restream.io ou o Aitum Multistream, só que rodando
 * localmente na sua máquina.
 *
 * No OBS, configure:
 *   Servidor: rtmp://localhost:PORT/live
 *   Chave de stream: o valor de rtmp.streamKey do config.json
 *
 * @param {object} cfg - config.rtmp do config.json
 */
function startRelay(cfg) {
  const nms = new NodeMediaServer({
    rtmp: {
      port: cfg.port,
      chunk_size: 60000,
      gop_cache: true,
      ping: 30,
      ping_timeout: 60,
    },
    logType: 1,
  });

  const activeProcesses = new Map();

  nms.on('postPublish', (id, StreamPath, args) => {
    const key = StreamPath.split('/').pop();

    if (key !== cfg.streamKey) {
      console.warn(`[relay] chave de stream "${key}" não confere com a configurada, ignorando`);
      return;
    }

    console.log('[relay] OBS começou a transmitir — iniciando envio para as plataformas');

    const sourceUrl = `rtmp://127.0.0.1:${cfg.port}${StreamPath}`;
    const procs = [];

    for (const dest of cfg.destinations) {
      if (!dest.enabled) continue;
      if (!dest.key || dest.key.startsWith('SUA_STREAM_KEY')) {
        console.warn(`[relay] pulando ${dest.name}: chave de stream não configurada`);
        continue;
      }

      const targetUrl = `${dest.url}/${dest.key}`;

      const ff = spawn('ffmpeg', [
        '-i', sourceUrl,
        '-c', 'copy',
        '-f', 'flv',
        targetUrl,
      ]);

      ff.stderr.on('data', (chunk) => {
        // ffmpeg escreve o progresso no stderr; deixe comentado a menos que
        // esteja debugando, senão o console fica poluído.
        // console.log(`[relay:${dest.name}]`, chunk.toString());
      });

      ff.on('close', (code) => {
        console.log(`[relay:${dest.name}] processo encerrado (código ${code})`);
      });

      console.log(`[relay] enviando para ${dest.name}`);
      procs.push(ff);
    }

    activeProcesses.set(id, procs);
  });

  nms.on('donePublish', (id) => {
    const procs = activeProcesses.get(id) || [];
    for (const p of procs) p.kill('SIGINT');
    activeProcesses.delete(id);
    console.log('[relay] OBS parou de transmitir — encerrando envio para as plataformas');
  });

  nms.run();
  console.log(`[relay] servidor RTMP rodando em rtmp://localhost:${cfg.port}/live`);

  return nms;
}

module.exports = { startRelay };
