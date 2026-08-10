@echo off
REM Launch TheXTech Dreamcast boot CDI in Flycast (edit FLYCAST if needed)
set CDI=I:\sw\TheXTech-main\build-dreamcast\thextech_dc_boot.cdi
set FLYCAST=

if exist "I:\sw\flycast-master\build\flycast.exe" set FLYCAST=I:\sw\flycast-master\build\flycast.exe
if exist "I:\sw\flycast-master\flycast.exe" set FLYCAST=I:\sw\flycast-master\flycast.exe
if exist "%ProgramFiles%\Flycast\flycast.exe" set FLYCAST=%ProgramFiles%\Flycast\flycast.exe

if "%FLYCAST%"=="" (
  echo No se encontro flycast.exe. Abre manualmente:
  echo   %CDI%
  explorer /select,"%CDI%"
  exit /b 1
)

start "" "%FLYCAST%" "%CDI%"
