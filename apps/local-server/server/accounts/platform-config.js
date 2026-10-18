const TWITCH_CLIENT_ID = 'qujbqnms1y167wc7dpt4t7v8k0dwtj';
const TWITCH_SCOPES = [
  'user:read:chat', 'user:write:chat', 'channel:manage:broadcast',
  'channel:read:stream_key', 'moderator:manage:banned_users',
  'moderator:manage:chat_messages', 'moderator:manage:chat_settings',
  'moderator:manage:automod',
].join(' ');
const TWITCH_DEVICE_URL = 'https://id.twitch.tv/oauth2/device';
const TWITCH_TOKEN_URL = 'https://id.twitch.tv/oauth2/token';
const TWITCH_USERS_URL = 'https://api.twitch.tv/helix/users';

const KICK_AUTHORIZE_URL = 'https://id.kick.com/oauth/authorize';
const KICK_TOKEN_URL = 'https://id.kick.com/oauth/token';
const KICK_API_BASE = 'https://api.kick.com/public/v1';
const KICK_REDIRECT_URI = 'http://localhost:605/api/accounts/kick/callback';
const KICK_SCOPES = [
  'user:read', 'channel:read', 'channel:write', 'streamkey:read', 'chat:write', 'moderation:ban',
].join(' ');

const YOUTUBE_AUTHORIZE_URL = 'https://accounts.google.com/o/oauth2/v2/auth';
const YOUTUBE_TOKEN_URL = 'https://oauth2.googleapis.com/token';
const YOUTUBE_API_BASE = 'https://www.googleapis.com/youtube/v3';
const YOUTUBE_REDIRECT_URI = 'http://localhost:605/api/accounts/youtube/callback';
const YOUTUBE_SCOPE = 'https://www.googleapis.com/auth/youtube.force-ssl';

const DEFAULT_OAUTH_BROKER_URL = 'https://streamhub-oauth-broker.onrender.com';

const SHARED_OAUTH_BROKER_URL = String(
  process.env.STREAMHUB_OAUTH_BROKER_URL || DEFAULT_OAUTH_BROKER_URL,
).trim().replace(/\/+$/, '');

module.exports = {
  TWITCH_CLIENT_ID,
  TWITCH_SCOPES,
  TWITCH_DEVICE_URL,
  TWITCH_TOKEN_URL,
  TWITCH_USERS_URL,
  KICK_AUTHORIZE_URL,
  KICK_TOKEN_URL,
  KICK_API_BASE,
  KICK_REDIRECT_URI,
  KICK_SCOPES,
  YOUTUBE_AUTHORIZE_URL,
  YOUTUBE_TOKEN_URL,
  YOUTUBE_API_BASE,
  YOUTUBE_REDIRECT_URI,
  YOUTUBE_SCOPE,
  SHARED_OAUTH_BROKER_URL,
};
