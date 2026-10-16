# Plugin OBS do StreamHub

Este diretório contém a DLL principal do StreamHub. O servidor local, o broker OAuth, o RNNoise, os testes, as dependências e os recursos ficam na raiz organizada do projeto.

## Compilar

Na raiz de `plugins/obs`:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

Saídas principais:

```text
build_x64/RelWithDebInfo/obs-multi-rtmp.dll
build_x64/RelWithDebInfo/streamhub-updater-helper.exe
```

O CMake usa caminhos fixos da estrutura organizada:

- `../../apps/local-server` para os arquivos embutidos do servidor;
- `../../libraries/noise-suppressor` para o filtro integrado;
- `../../third_party/rnnoise` e `../../third_party/nlohmann-json` para dependências;
- `../../tests` para os testes;
- `../../tools` para o helper de atualização.

O QRC embute servidor, overlay, ícones, tema e locales na DLL. Não é necessário copiar `apps/` manualmente para a instalação do OBS.
