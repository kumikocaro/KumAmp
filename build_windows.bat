@echo off
rem ============================================================================
rem  KumAmp - compilacion + tests + instalador para Windows (x64)
rem
rem  Requisitos (una sola vez):
rem    - Visual Studio 2022 (Community sirve) con "Desarrollo para el escritorio con C++"
rem    - CMake 3.24 o superior (viene con Visual Studio o desde cmake.org)
rem    - Git
rem    - Inno Setup 6 (jrsoftware.org) para generar el instalador
rem
rem  Uso: doble clic, o desde una consola en la carpeta del proyecto:  build_windows.bat
rem  Resultado: dist\KumAmp-<version>-Windows-x64-Setup.exe
rem ============================================================================
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul || (echo [ERROR] No se encontro CMake. Instala Visual Studio 2022 o CMake. & goto :fail)
where git   >nul 2>nul || (echo [ERROR] No se encontro Git. & goto :fail)

echo.
echo === 1/4  Configurando (descarga JUCE y NeuralAmpModelerCore la primera vez) ===
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 || goto :fail

echo.
echo === 2/4  Compilando Release ===
cmake --build build-win --config Release --parallel || goto :fail

echo.
echo === 3/4  Tests ===
ctest --test-dir build-win -C Release --output-on-failure || goto :fail

echo.
echo === 4/4  Instalador ===
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo [AVISO] No se encontro Inno Setup 6: el plugin esta compilado en
    echo         build-win\KumAmp_artefacts\Release\  pero no se genero el instalador.
    goto :done
)
"%ISCC%" /DBuildDir="%CD%\build-win\KumAmp_artefacts\Release" installer\KumAmp.iss || goto :fail

echo.
echo Listo. Instalador en la carpeta dist\
:done
pause
exit /b 0

:fail
echo.
echo La compilacion fallo. Revisa los mensajes de arriba.
pause
exit /b 1
