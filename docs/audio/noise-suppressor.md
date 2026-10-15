# Supressor de ruído StreamHub

Este componente é compilado e registrado dentro de `obs-multi-rtmp.dll`. Não existe uma DLL de filtro separada para instalar. O código usa RNNoise do código-fonte do OBS e combina redução de ruído, gate adaptativo, filtro passa-altas e limitador.

## Compilar e testar

Na raiz do repositório:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

O filtro aparece no OBS como **StreamHub — Supressor de ruído (RNNoise)**, na categoria de filtros de áudio. Instale somente `obs-multi-rtmp.dll`.

## Uso recomendado

Adicione o filtro à fonte do microfone e comece pelo preset **Equilibrado**. Evite empilhar outro filtro RNNoise. Se precisar controlar o volume depois da redução de ruído, aplique um compressor após este filtro.

O RNNoise processa quadros de 480 amostras a 48 kHz; outras taxas são convertidas pelo resamplador de áudio do OBS, com latência adicional dependente da configuração. Este filtro não usa código proprietário do Discord e não promete equivalência de resultado.

Consulte `licenses/RNNOISE.txt` para os avisos de licença do RNNoise incorporado.
