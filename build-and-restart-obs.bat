@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "PROJECT_ROOT=%~dp0"
set "OBS_ROOT=E:\obs-studio"
set "OBS_EXE=%OBS_ROOT%\bin\64bit\obs64.exe"
set "DLL_SRC=%PROJECT_ROOT%build_x64\RelWithDebInfo\obs-multi-rtmp.dll"
set "DLL_DIR=%OBS_ROOT%\obs-plugins\64bit"
set "DLL_DST=%DLL_DIR%\obs-multi-rtmp.dll"
set "DLL_TMP=%DLL_DST%.deploying"
set "OBS_WAS_RUNNING=0"
set "OBS_CLOSED=0"

cd /d "%PROJECT_ROOT%"
if errorlevel 1 goto :fail

echo [1/5] Configurando projeto...
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERRO: cmake nao encontrado no PATH.
    goto :fail
)
cmake --preset windows-x64
if errorlevel 1 (
    echo ERRO: configuracao CMake falhou.
    goto :fail
)

echo [2/5] Compilando plugin...
cmake --build --preset windows-x64 --config RelWithDebInfo
if errorlevel 1 (
    echo ERRO: compilacao falhou.
    goto :fail
)

if not exist "%DLL_SRC%" (
    echo ERRO: DLL compilada nao encontrada: "%DLL_SRC%"
    goto :fail
)
for %%F in ("%DLL_SRC%") do if %%~zF LEQ 0 (
    echo ERRO: DLL compilada esta vazia.
    goto :fail
)
if not exist "%OBS_EXE%" (
    echo ERRO: OBS nao encontrado: "%OBS_EXE%"
    goto :fail
)
if not exist "%DLL_DIR%\" (
    echo ERRO: pasta de plugins nao encontrada: "%DLL_DIR%"
    goto :fail
)

echo [3/5] Fechando OBS...
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $obs = [IO.Path]::GetFullPath($env:OBS_EXE); $p = @(Get-CimInstance Win32_Process -Filter 'Name = ''obs64.exe''' -ErrorAction Stop | Where-Object { $_.ExecutablePath -eq $obs }); if ($p.Count -gt 0) { $p | ForEach-Object { $proc = Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue; if ($proc) { $null = $proc.CloseMainWindow() } }; exit 1 }; exit 0 } catch { exit 2 }" >nul 2>&1
if errorlevel 2 (
    echo ERRO: nao foi possivel verificar o processo do OBS.
    goto :fail
)
if errorlevel 1 (
    set "OBS_WAS_RUNNING=1"
) else (
    set "OBS_CLOSED=1"
    goto :deploy
)

if "%OBS_WAS_RUNNING%"=="1" (
    for /L %%N in (1,1,30) do (
        powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $obs = [IO.Path]::GetFullPath($env:OBS_EXE); $p = @(Get-CimInstance Win32_Process -Filter 'Name = ''obs64.exe''' -ErrorAction Stop | Where-Object { $_.ExecutablePath -eq $obs }); if ($p.Count -eq 0) { exit 0 }; exit 1 } catch { exit 2 }" >nul 2>&1
        if errorlevel 2 (
            echo ERRO: nao foi possivel verificar se OBS fechou.
            goto :fail
        )
        if not errorlevel 1 (
            set "OBS_CLOSED=1"
            goto :deploy
        )
        timeout /t 1 /nobreak >nul
    )
    echo ERRO: OBS nao fechou em 30 segundos. Implantacao cancelada.
    echo Feche OBS manualmente e execute este arquivo novamente.
    goto :fail
)
set "OBS_CLOSED=1"

goto :deploy

:deploy
echo [4/5] Instalando DLL...
del /q "%DLL_TMP%" >nul 2>&1
copy /y "%DLL_SRC%" "%DLL_TMP%" >nul
if errorlevel 1 (
    echo ERRO: copia temporaria da DLL falhou.
    goto :reopen_after_fail
)
for %%F in ("%DLL_TMP%") do if %%~zF LEQ 0 (
    echo ERRO: copia temporaria da DLL esta vazia.
    del /q "%DLL_TMP%" >nul 2>&1
    goto :reopen_after_fail
)
move /y "%DLL_TMP%" "%DLL_DST%" >nul
if errorlevel 1 (
    echo ERRO: substituicao da DLL instalada falhou.
    del /q "%DLL_TMP%" >nul 2>&1
    goto :reopen_after_fail
)

echo [5/5] Abrindo OBS...
start "" /d "%OBS_ROOT%" "%OBS_EXE%"
if errorlevel 1 (
    echo ERRO: nao foi possivel iniciar OBS.
    exit /b 1
)
echo Concluido: plugin compilado, DLL instalada e OBS iniciado.
exit /b 0

:reopen_after_fail
if "%OBS_CLOSED%"=="1" (
    echo Tentando reabrir OBS apos falha.
    start "" /d "%OBS_ROOT%" "%OBS_EXE%"
)
:fail
exit /b 1
