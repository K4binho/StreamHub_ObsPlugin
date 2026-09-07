function createPlatformActions(accounts, getConfig) {
  let syncBusy = false;
  let lastSync = '';
  let syncStatus = { message: 'Sincronização automática desativada.' };
  return {
    status: () => syncStatus,
    async send(target, message) {
      if (!['all', 'twitch', 'kick', 'youtube', 'tiktok'].includes(target)) throw new Error('Destino inválido.');
      if (typeof message !== 'string' || !message.trim() || message.length > 500) throw new Error('Digite uma mensagem com até 500 caracteres.');
      const config = getConfig();
      const destinations = target === 'all' ? ['twitch', 'kick', 'youtube', 'tiktok'].filter(p => config[p]?.enabled) : [target];
      if (!destinations.length) throw new Error('Nenhum chat está habilitado.');
      return Promise.all(destinations.map(async platform => {
        try {
          if (!config[platform]?.enabled) throw new Error('Chat desativado.');
          if (!['twitch', 'kick'].includes(platform)) throw new Error('Envio ainda não disponível nesta plataforma.');
          const userId = await accounts.userId(platform);
          if (platform === 'twitch') {
            const users = await accounts.request('twitch', `/users?login=${encodeURIComponent(config.twitch.channel)}`);
            if (!users.data?.[0]) throw new Error('Canal não encontrado.');
            const result = await accounts.request('twitch', '/chat/messages', { method: 'POST', body: JSON.stringify({ broadcaster_id: users.data[0].id, sender_id: userId, message }) });
            if (!result.data?.[0]?.is_sent) throw new Error('Mensagem recusada pela moderação ou pelos limites da Twitch.');
          } else {
            const channels = await accounts.request('kick', `/channels?slug=${encodeURIComponent(config.kick.channel)}`);
            if (!channels.data?.[0]) throw new Error('Canal não encontrado.');
            await accounts.request('kick', '/chat', { method: 'POST', body: JSON.stringify({ broadcaster_user_id: channels.data[0].broadcaster_user_id, content: message, type: 'user' }) });
          }
          return { platform, ok: true };
        } catch (error) { return { platform, ok: false, error: error.message }; }
      }));
    },
    async sync(force = false) {
      if (syncBusy) throw new Error('Sincronização já em andamento.');
      syncBusy = true;
      try {
        const config = getConfig();
        const source = await accounts.request('twitch', `/users?login=${encodeURIComponent(config.twitch?.channel || '')}`);
        if (!source.data?.[0]) throw new Error('Configure o canal da Twitch.');
        const result = await accounts.request('twitch', `/channels?broadcaster_id=${source.data[0].id}`);
        const channel = result.data?.[0];
        if (!channel?.title) throw new Error('A Twitch não retornou título.');
        const ownKickId = await accounts.userId('kick');
        const kick = await accounts.request('kick', `/channels?slug=${encodeURIComponent(config.kick?.channel || '')}`);
        if (String(kick.data?.[0]?.broadcaster_user_id) !== ownKickId) throw new Error('Conecte a conta dona do canal Kick configurado.');
        const fingerprint = JSON.stringify([channel.title, channel.game_id, ownKickId]);
        if (!force && fingerprint === lastSync) return syncStatus;
        const body = { stream_title: channel.title };
        let categoryWarning = '';
        if (channel.game_name) {
          const categories = await accounts.request('kick', `/categories?q=${encodeURIComponent(channel.game_name)}`);
          const exact = (categories.data || []).filter(c => c.name.toLowerCase() === channel.game_name.toLowerCase());
          if (exact.length === 1) body.category_id = exact[0].id;
          else categoryWarning = ' Categoria sem correspondência exata; a categoria da Kick foi preservada.';
        }
        await accounts.request('kick', '/channels', { method: 'PATCH', body: JSON.stringify(body) });
        lastSync = fingerprint;
        syncStatus = { ok: true, message: `Título sincronizado com a Kick.${categoryWarning}`, updatedAt: Date.now() };
        return syncStatus;
      } catch (error) {
        syncStatus = { ok: false, message: error.message, updatedAt: Date.now() };
        throw error;
      } finally { syncBusy = false; }
    },
  };
}
module.exports = { createPlatformActions };
