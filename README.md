# StreamHub Clean

Estrutura unificada do StreamHub e do supressor de ruído, organizada por responsabilidade. Os caminhos do CMake já estão preparados para esta árvore; não é necessário editar referências ao mover o projeto dentro do workspace.

## Estrutura

- `plugins/obs`: plugin nativo do OBS, docks, streaming, chat, contas e integração visual.
- `apps/local-server`: servidor local usado pelo plugin e pelo overlay.
- `apps/oauth-broker`: broker OAuth externo para Kick e YouTube.
- `libraries/noise-suppressor`: filtro RNNoise independente e seus testes DSP.
- `tests`: testes de regressão do plugin.
- `third_party`: dependências de terceiros mantidas no projeto.
- `docs`: arquitetura, operação e decisões técnicas.
- `tools`: utilitários de atualização e empacotamento.
- `deployment`: arquivos de hospedagem e implantação.

## Princípios de organização

Cada pasta tem uma responsabilidade única. O código de domínio fica separado da infraestrutura, da interface do OBS, dos servidores Node e das ferramentas de build. Arquivos de build gerados não fazem parte desta cópia organizada.

## Compilação e validação

Na raiz do repositório, entre em `plugins/obs` e compile:

```powershell
cd plugins/obs
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

O teste independente do DSP pode ser executado em `libraries/noise-suppressor` com os mesmos comandos do preset local. O RNNoise continua incorporado à DLL principal; a pasta da biblioteca não produz uma segunda DLL.

Depois da compilação, a DLL fica em `plugins/obs/build_x64/RelWithDebInfo/obs-multi-rtmp.dll`.

## Fonte original

Esta é a estrutura principal do projeto. Os caminhos relativos do CMake e do empacotamento foram ajustados para ela; não é necessário manter ou editar uma cópia paralela.
