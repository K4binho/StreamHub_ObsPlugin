const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { createAccounts } = require('../server/accounts');

function fixture(t, endpoint = 'rtmps://ingest.example/live2') {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'streamhub-youtube-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  fs.writeFileSync(path.join(directory, 'accounts-private.json'), JSON.stringify({
    youtube: { access_token: 'fixture-token', expiresAt: Date.now() + 3600000, name: 'Creator' },
  }));
  const calls = [];
  const accounts = createAccounts(directory, async (url) => {
    const request = new URL(url);
    calls.push(request.pathname + request.search);
    let body;
    if (request.pathname.endsWith('/liveBroadcasts')) {
      body = { items: [
        { id: 'public-live', snippet: { title: 'Public event' }, status: { lifeCycleStatus: 'live', privacyStatus: 'public' }, contentDetails: { boundStreamId: 'public-stream' } },
        { id: 'private-test', snippet: { title: 'Private test' }, status: { lifeCycleStatus: 'ready', privacyStatus: 'private' }, contentDetails: { boundStreamId: 'private-stream' } },
        { id: 'completed', snippet: {}, status: { lifeCycleStatus: 'complete' }, contentDetails: { boundStreamId: 'old-stream' } },
      ] };
    } else if (request.pathname.endsWith('/videos')) {
      body = { items: [] };
    } else if (request.pathname.endsWith('/liveStreams')) {
      const ids=request.searchParams.get('id').split(',');
      assert.ok(ids.every(id=>['private-stream','public-stream'].includes(id)),'must never use another event stream');
      body = { items: ids.map(id=>({ id, cdn: {
        ingestionType:id==='private-stream'?'rtmp':'webrtc',
        ingestionInfo:{ ingestionAddress:id==='private-stream'?endpoint:'https://upload.youtube.com/live_webrtc_ingest_handshake',streamName:'private-fixture-key' },
      } })) };
    } else {
      throw new Error('Unexpected API request in fixture');
    }
    return { ok: true, json: async () => body };
  });
  return { accounts, calls };
}

test('sync uses the selected private event even when another public event is live', async (t) => {
  const { accounts } = fixture(t);
  const result = await accounts.youtubeTransmission('private-test');
  assert.equal(result.broadcastId, 'private-test');
  assert.equal(result.visibility, 'private');
  assert.equal(result.streamKey, 'private-fixture-key');
});

test('event list identifies and prioritizes RTMP without exposing endpoints or keys', async(t)=>{
  const {accounts,calls}=fixture(t);
  const events=await accounts.youtubeBroadcastEvents();
  assert.equal(events.length,2);
  assert.equal(events[0].id,'private-test');
  assert.equal(events[0].rtmpCompatible,true);
  assert.equal(events[0].ingestionType,'rtmp');
  assert.equal(events[1].rtmpCompatible,false);
  assert.equal(events[1].ingestionType,'webrtc');
  assert.equal(JSON.stringify(events).includes('private-fixture-key'),false);
  assert.equal(JSON.stringify(events).includes('ingestionAddress'),false);
  assert.equal(calls.filter(url=>url.includes('/liveStreams?')).length,1);
});

test('event list does not invent RTMP compatibility for WebRTC', async(t)=>{
  const {accounts}=fixture(t,'https://upload.youtube.com/live_webrtc_ingest_handshake');
  const events=await accounts.youtubeBroadcastEvents();
  assert.ok(events.every(event=>event.rtmpCompatible===false));
});

test('missing selection is rejected without querying or choosing an event', async (t) => {
  const { accounts, calls } = fixture(t);
  await assert.rejects(accounts.youtubeTransmission(), /Selecione o evento/);
  assert.equal(calls.length, 0);
});

test('deleted and completed selections cannot silently switch to a different event', async (t) => {
  const { accounts, calls } = fixture(t);
  await assert.rejects(accounts.youtubeTransmission('deleted'), /não está mais disponível/);
  await assert.rejects(accounts.youtubeTransmission('completed'), /encerrado/);
  assert.equal(calls.some(url => url.includes('/liveStreams')), false);
});

test('a selected WebRTC stream fails without inventing a compatible server', async (t) => {
  const { accounts } = fixture(t, 'https://upload.youtube.com/live_webrtc_ingest_handshake');
  await assert.rejects(accounts.youtubeTransmission('private-test'), /incompatível com RTMP/);
});
