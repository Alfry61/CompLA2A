@echo off
rem ============================================================================
rem  ALAP Opto Leveler - script di compilazione per Windows
rem
rem  Uso:
rem     build.bat                 compila in Release
rem     build.bat debug           compila in Debug
rem     build.bat clean           cancella la cartella build e ricompila
rem     build.bat install         compila e copia il VST3 nella cartella di
rem                               sistema (avviare il prompt come amministratore)
rem     build.bat test            compila ed esegue il programma di prova
rem  Le opzioni si possono combinare:  build.bat clean release install
rem
rem  Per un'altra versione di Visual Studio:
rem     set GENERATOR=Visual Studio 18 2026
rem     build.bat
rem ============================================================================
setlocal

set "CONFIG=Release"
set "DO_CLEAN=0"
set "DO_INSTALL=0"
set "DO_TEST=0"
set "TEST_FAILED=0"
set "BUILD_DIR=build"
set "PLUGIN_NAME=ALAP Opto Leveler"
set "TARGET=ALAPOptoLeveler"
if not defined GENERATOR set "GENERATOR=Visual Studio 17 2022"

rem --- Lettura degli argomenti ------------------------------------------------
:parse
if "%~1"=="" goto parsed
if /I "%~1"=="debug"   set "CONFIG=Debug"
if /I "%~1"=="release" set "CONFIG=Release"
if /I "%~1"=="clean"   set "DO_CLEAN=1"
if /I "%~1"=="install" set "DO_INSTALL=1"
if /I "%~1"=="test"    set "DO_TEST=1"
shift
goto parse
:parsed

rem Lavora sempre nella cartella dello script
cd /d "%~dp0"

rem --- Verifiche preliminari ---------------------------------------------------
where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERRORE] CMake non trovato nel PATH. Installarlo da https://cmake.org
    goto fail
)

if not exist "Source\PluginProcessor.cpp" (
    echo [ERRORE] Cartella Source non trovata accanto a build.bat
    goto fail
)

rem --- Pulizia -----------------------------------------------------------------
if "%DO_CLEAN%"=="1" (
    if exist "%BUILD_DIR%" (
        echo Pulizia della cartella %BUILD_DIR%...
        rmdir /s /q "%BUILD_DIR%"
    )
)

rem --- Configurazione ----------------------------------------------------------
echo.
echo === Configurazione [%GENERATOR%, x64] ===
cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A x64
if errorlevel 1 goto fail

rem --- Compilazione ------------------------------------------------------------
echo.
echo === Compilazione [%CONFIG%] ===
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 goto fail

set "ARTEFACTS=%BUILD_DIR%\%TARGET%_artefacts\%CONFIG%"
set "VST3_PATH=%ARTEFACTS%\VST3\%PLUGIN_NAME%.vst3"

echo.
echo Compilazione completata.
echo   VST3       : %VST3_PATH%
echo   Standalone : %ARTEFACTS%\Standalone\%PLUGIN_NAME%.exe

rem --- Programma di prova --------------------------------------------------------
if "%DO_TEST%"=="0" goto aftertest

set "TEST_EXE=%BUILD_DIR%\tests\%CONFIG%\ALAPOptoTests.exe"

rem Ricerca di riserva nell'intera cartella build
if not exist "%TEST_EXE%" (
    for /f "delims=" %%F in ('dir /s /b "%BUILD_DIR%\ALAPOptoTests.exe" "%BUILD_DIR%\ALAP Opto Tests.exe" 2^>nul') do set "TEST_EXE=%%F"
)

if not exist "%TEST_EXE%" (
    echo [ERRORE] Programma di prova non trovato.
    echo          Verificare che la cartella Tests con TestMain.cpp sia accanto a Source
    echo          e che CMakeLists.txt sia la versione aggiornata, poi usare: build.bat clean test
    goto fail
)

echo.
echo === Esecuzione delle prove ===
"%TEST_EXE%"
if errorlevel 1 set "TEST_FAILED=1"
if "%TEST_FAILED%"=="1" echo [ATTENZIONE] Alcune prove non sono state superate.

:aftertest

rem --- Installazione -----------------------------------------------------------
if "%DO_INSTALL%"=="0" goto done

net session >nul 2>&1
if errorlevel 1 (
    echo [ERRORE] Per installare servono i privilegi di amministratore.
    goto fail
)

set "VST3_DEST=%CommonProgramFiles%\VST3\%PLUGIN_NAME%.vst3"
echo.
echo Copia in "%VST3_DEST%"...
if exist "%VST3_DEST%" rmdir /s /q "%VST3_DEST%"
xcopy "%VST3_PATH%" "%VST3_DEST%\" /E /I /Y /Q >nul
if errorlevel 1 goto fail
echo Plugin installato. Riesegui la scansione dei plugin in Cubase / Ableton.

:done
if "%TEST_FAILED%"=="1" (
    endlocal
    exit /b 2
)
endlocal
exit /b 0

:fail
echo.
echo *** Compilazione interrotta ***
endlocal
exit /b 1
