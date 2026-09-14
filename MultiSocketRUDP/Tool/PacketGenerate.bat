@echo off
setlocal
python "%~dp0PacketGenerator\PacketGenerator.py" %*
set "GENERATOR_EXIT_CODE=%ERRORLEVEL%"

if "%1"=="" (
    pause
)
exit /b %GENERATOR_EXIT_CODE%
