@echo off
cd /d "%~dp0"
python PacketGenerator/PacketGenerator.py

if "%1"=="" (
    pause
)
