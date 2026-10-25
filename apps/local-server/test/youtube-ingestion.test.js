const test = require('node:test');
const assert = require('node:assert/strict');
const { selectYoutubeRtmpIngestion } = require('../server/accounts');

test('prefers the YouTube RTMPS ingestion endpoint when it is valid', () => {
  assert.equal(selectYoutubeRtmpIngestion({
    rtmpsIngestionAddress: 'rtmps://ingest.example/live2',
    ingestionAddress: 'rtmp://ingest.example/live2',
  }), 'rtmps://ingest.example/live2');
});

test('falls back to the RTMP endpoint when RTMPS is unavailable or incompatible', () => {
  assert.equal(selectYoutubeRtmpIngestion({
    rtmpsIngestionAddress: 'https://upload.youtube.com/live_webrtc_ingest_handshake',
    ingestionAddress: 'rtmp://x.rtmp.youtube.com/live2',
  }), 'rtmp://x.rtmp.youtube.com/live2');
});

test('rejects WebRTC/HTTPS instead of guessing another ingestion server', () => {
  assert.equal(selectYoutubeRtmpIngestion({
    rtmpsIngestionAddress: 'https://upload.youtube.com/live_webrtc_ingest_handshake',
    ingestionAddress: 'https://upload.youtube.com/live_webrtc_ingest_handshake',
  }), '');
});
