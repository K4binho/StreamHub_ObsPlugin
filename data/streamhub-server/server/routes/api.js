const express = require('express');
const { readConfig, writeConfig } = require('../config-store');

const router = express.Router();

// Retorna o config.json inteiro pro dashboard preencher os campos.
router.get('/config', (req, res) => {
  try {
    res.json(readConfig());
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Sobrescreve o config.json com o que veio do dashboard.
// Validação é propositalmente simples: isso roda só localmente, pra você mesmo.
router.post('/config', (req, res) => {
  const incoming = req.body;

  if (!incoming || typeof incoming !== 'object') {
    return res.status(400).json({ error: 'Corpo inválido' });
  }

  try {
    writeConfig(incoming);
    res.json({ ok: true });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

module.exports = router;
