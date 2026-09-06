#pragma once

#include <QString>

// Garante que os arquivos do StreamHub (servidor Node, painel/overlay e o
// locale mínimo) embutidos como Qt Resources dentro da própria DLL existam
// na pasta de dados do plugin (obs_get_module_data_path).
//
// Isso é o que permite distribuir só a .dll: na primeira vez que o OBS
// carrega essa versão do plugin, esta função (re)cria a pasta data/ inteira
// a partir do que está compilado dentro do binário. Chamadas seguintes são
// baratas (só conferem um marcador de versão) e NUNCA sobrescrevem
// streamhub-server/config.json nem streamhub-server/node_modules do
// usuário.
//
// Precisa ser chamada bem no início de obs_module_load(), antes de
// qualquer obs_module_text() (que lê locale/*.ini do disco) e antes de
// StreamHubLauncher::Start() (que espera server/index.js já no disco).
bool StreamHub_EnsureBundledData(const QString &dataPath);
