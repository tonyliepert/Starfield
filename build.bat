@echo off
rem Builds Starfield.scr with the MSYS2 UCRT64 toolchain.
setlocal
set PATH=C:\msys64\ucrt64\bin;%PATH%
cd /d "%~dp0"
windres resources.rc -O coff -o resources.o || exit /b 1
g++ -O2 -std=c++17 -static -mwindows -Wall src\main.cpp resources.o -o Starfield.scr ^
    -lopengl32 -lgdi32 -lwinmm -lole32 -loleaut32 -lshell32 -luuid -lwindowscodecs -lmfplat -lmfreadwrite -lmfuuid || exit /b 1
rem Same program as an .exe, which opens in a window when double-clicked.
copy /y Starfield.scr Starfield.exe >nul || exit /b 1
echo Built Starfield.scr and Starfield.exe
