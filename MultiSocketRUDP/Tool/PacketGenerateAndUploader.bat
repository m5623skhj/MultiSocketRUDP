@echo off
call PacketGenerate.bat nopause
if errorlevel 1 exit /b %errorlevel%
call PacketUploader.bat
