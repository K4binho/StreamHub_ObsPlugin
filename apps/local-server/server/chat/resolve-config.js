function normalizedChannel(value) {
  return String(value || '').trim().replace(/^[@#]+/, '');
}

function resolveChatConfigurations(saved, identities) {
  const twitchIdentity = identities.twitch || {};
  const kickIdentity = identities.kick || {};
  const youtubeIdentity = identities.youtube || {};

  const twitch = {
    ...(saved.twitch || {}),
    enabled: Boolean(twitchIdentity.connected || saved.twitch?.enabled),
    channel: normalizedChannel(
      twitchIdentity.connected ? twitchIdentity.name : saved.twitch?.channel,
    ),
  };
  const kick = {
    ...(saved.kick || {}),
    enabled: Boolean(kickIdentity.connected || saved.kick?.enabled),
    channel: normalizedChannel(
      kickIdentity.connected ? kickIdentity.name : saved.kick?.channel,
    ),
  };
  const youtube = {
    ...(saved.youtube || {}),
    enabled: Boolean(youtubeIdentity.connected || saved.youtube?.enabled),
  };

  return { twitch, kick, youtube, tiktok: saved.tiktok || {} };
}

module.exports = { resolveChatConfigurations };
