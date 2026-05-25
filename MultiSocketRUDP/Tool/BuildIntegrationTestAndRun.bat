@echo off
setlocal
pushd "%~dp0\.."

set "TEST_CERT_PATH=%CD%\IntegrationTest\TestCert.pfx"

if not exist "%TEST_CERT_PATH%" (
    echo ==============================
    echo Generate IntegrationTest certificate
    echo ==============================

    call ".\Tool\ForTLS\CreateDevTLSPfx.bat"
    if %errorlevel% neq 0 (
        echo Certificate generation failed
        popd
        pause
        exit /b %errorlevel%
    )
)

echo ==============================
echo Build IntegrationTest
echo ==============================

msbuild .\MultiSocketRUDP.sln /t:IntegrationTest /p:Configuration=Debug /p:Platform=x64

if %errorlevel% neq 0 (
    echo Build Failed
    popd
    pause
    exit /b %errorlevel%
)

echo ==============================
echo Run IntegrationTest
echo ==============================

.\x64\Debug\IntegrationTest.exe
set "TEST_EXIT_CODE=%errorlevel%"

popd
pause
exit /b %TEST_EXIT_CODE%
