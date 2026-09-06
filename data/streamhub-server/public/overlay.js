const socket = io();
const chatBox = document.getElementById('chat-box');

const MAX_ON_SCREEN = 25;
const AUTO_REMOVE_MS = 20000; // some mensagem sozinha depois de 20s

function renderMessage(msg) {
  const el = document.createElement('div');
  el.className = 'msg';

  el.innerHTML = `
    <span class="platform-tag ${msg.platform}">${msg.platform}</span>
    <span class="user" style="color:${msg.color || '#fff'}">${escapeHtml(msg.user)}:</span>
    <span class="text">${escapeHtml(msg.message)}</span>
  `;

  chatBox.appendChild(el);

  while (chatBox.children.length > MAX_ON_SCREEN) {
    chatBox.removeChild(chatBox.firstChild);
  }

  chatBox.scrollTop = chatBox.scrollHeight;

  setTimeout(() => {
    el.remove();
  }, AUTO_REMOVE_MS);
}

function escapeHtml(str) {
  const div = document.createElement('div');
  div.textContent = str ?? '';
  return div.innerHTML;
}

socket.on('chat-history', (messages) => {
  messages.forEach(renderMessage);
});

socket.on('chat-message', renderMessage);
