@echo off

echo ==============================
echo Build CoreTest
echo ==============================

msbuild ..\MultiSocketRUDP.sln /t:CoreTest /p:Configuration=Debug /p:Platform=x64

if %errorlevel% neq 0 (
    echo Build Failed
    pause
    exit /b %errorlevel%
)

echo ==============================
echo Run CoreTest
echo ==============================

..\x64\Debug\CoreTest.exe

pause