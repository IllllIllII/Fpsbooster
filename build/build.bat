@echo off
chcp 65001 >nul
rem Windows: MinGW(g++, windres)가 PATH에 있을 때 빌드 -> ..\FrameBooster.exe
cd /d "%~dp0..\src"
windres app.rc -O coff -o app.res.o || goto :err
g++ -std=c++17 -Os -ffunction-sections -fdata-sections -Wl,--gc-sections -municode -mwindows main.cpp app.res.o -o ..\FrameBooster.exe -static -s -ld2d1 -ldwrite -ldxgi -lpowrprof -lwinhttp -lbcrypt -ldwmapi -lpsapi -lversion -lole32 -lshell32 -ladvapi32 -lgdi32 -luser32 || goto :err
del app.res.o
echo 빌드 완료
pause
exit /b 0
:err
echo 빌드 실패
pause
