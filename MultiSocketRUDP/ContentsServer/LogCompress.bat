@echo off
setlocal EnableDelayedExpansion

set LOGROOT=%~dp0\Log Folder
set ARCHIVE=%LOGROOT%\Archive

if not exist "%ARCHIVE%" mkdir "%ARCHIVE%"

for /f %%D in ('powershell -NoProfile -Command "(Get-Date).ToString('yyyyMMdd')"') do set TODAY=%%D

if "!TODAY!"=="" (
    echo [ERROR] Failed to retrieve date.
    exit /b 1
)

for %%Z in ("%ARCHIVE%\*.tar.gz") do (
    set FNAME=%%~nxZ
    set FBASE=!FNAME:~0,-7!
    set FDATE=!FBASE:~-8!

    if not "!FDATE!"=="%TODAY%" (
        del /q "%%Z"
        echo [INFO] Deleted outdated archive: %%~nxZ
    )
)

dir /b "%LOGROOT%\*.txt" >nul 2>nul
if not errorlevel 1 (
    set ZIPFILE=%ARCHIVE%\LogFolder_%TODAY%.tar.gz

    if exist "!ZIPFILE!" (
        echo [SKIP] Archive for today already exists: LogFolder
    ) else (
        tar -czf "!ZIPFILE!" -C "%LOGROOT%" *.txt

        if !ERRORLEVEL! == 0 (
            tar -tzf "!ZIPFILE!" >nul 2>nul
            if !ERRORLEVEL! == 0 (
                del /q "%LOGROOT%\*.txt"
                echo [OK] LogFolder - Compressed and original logs deleted
            ) else (
                del /q "!ZIPFILE!"
                echo [ERROR] LogFolder - Integrity check failed, corrupted archive deleted, original logs preserved
            )
        ) else (
            if exist "!ZIPFILE!" del /q "!ZIPFILE!"
            echo [ERROR] LogFolder - Compression failed
        )
    )
)

for /D %%D in ("%LOGROOT%\*") do (
    if /I not "%%~nxD"=="Archive" (

        dir /b "%%D\*.txt" >nul 2>nul
        if not errorlevel 1 (
            set ZIPFILE=%ARCHIVE%\%%~nxD_%TODAY%.tar.gz

            if exist "!ZIPFILE!" (
                echo [SKIP] Archive for today already exists: %%~nxD
            ) else (
                tar -czf "!ZIPFILE!" -C "%%D" *.txt

                if !ERRORLEVEL! == 0 (
                    tar -tzf "!ZIPFILE!" >nul 2>nul
                    if !ERRORLEVEL! == 0 (
                        del /q "%%D\*.txt"
                        echo [OK] %%~nxD - Compressed and original logs deleted
                    ) else (
                        del /q "!ZIPFILE!"
                        echo [ERROR] %%~nxD - Integrity check failed, corrupted archive deleted, original logs preserved
                    )
                ) else (
                    if exist "!ZIPFILE!" del /q "!ZIPFILE!"
                    echo [ERROR] %%~nxD - Compression failed
                )
            )
        )
    )
)