function createHttpClient(request) {
  const postForm = async (url, fields, platform) => {
    const response = await request(url, {
      method: 'POST',
      body: new URLSearchParams(fields),
      timeout: 15000,
    });
    const result = await response.json();
    if (!response.ok) {
      const error = new Error(
        result.message || result.error_description || `${platform} recusou a operação (HTTP ${response.status}).`,
      );
      error.code = result.message || result.error;
      throw error;
    }
    return result;
  };

  const requestJson = async (url, options = {}, platform = 'Twitch') => {
    const response = await request(url, { ...options, timeout: 15000 });
    if (!response.ok) {
      let detail = '';
      try {
        const result = await response.json();
        detail = result.message || result.error?.message || result.error?.errors?.[0]?.reason || result.error || '';
      } catch (_) {
        // Mantém a mensagem segura quando o provedor não retorna JSON.
      }
      throw new Error(`${platform} recusou a operação (HTTP ${response.status})${detail ? `: ${detail}` : ''}.`);
    }
    return response.status === 204 ? {} : response.json();
  };

  return { postForm, requestJson };
}

module.exports = { createHttpClient };
