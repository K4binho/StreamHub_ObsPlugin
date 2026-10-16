# StreamHub Noise Suppressor

Implementação reutilizável do supressor RNNoise usado pelo plugin principal do OBS.

O filtro é compilado dentro de `plugins/obs/obs-multi-rtmp.dll`. Esta pasta não gera uma segunda DLL: ela mantém o código DSP e o teste automatizado, evitando duplicidade e conflitos de registro no OBS.

## Testar

A partir desta pasta:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
ctest --test-dir build/windows-x64 -C RelWithDebInfo --output-on-failure
```

O teste valida o processamento de quadros, gate, ganho, limitador e bypass. A DLL final do filtro é gerada somente pelo projeto em `plugins/obs`.

## Limitações

RNNoise trabalha com áudio a 48 kHz e quadros de 480 amostras. Outras taxas do OBS passam pelo resamplador e podem adicionar latência. Não empilhe uma segunda implementação RNNoise no mesmo microfone.

O aviso de licença do RNNoise está em [LICENSE-RNNOISE.txt](LICENSE-RNNOISE.txt).
