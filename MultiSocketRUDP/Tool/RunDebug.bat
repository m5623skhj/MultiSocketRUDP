@echo off

cd ../x64/Debug

start "TestServer" "ContentsServer.exe"
start "TestClient" "ContentsClient.exe"