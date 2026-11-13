@echo off
rem Builds ..\SR2Recomp.exe (the hub) with llvm-mingw clang, CMake and Ninja.
setlocal
rem llvm-mingw installed by winget (the toolkit's scripts\bootstrap.ps1 does that), any version; otherwise clang must be on PATH.
for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT*") do (
  for /d %%E in ("%%D\llvm-mingw-*") do if exist "%%E\bin\clang.exe" set "PATH=%%E\bin;%PATH%"
)
cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_RC_COMPILER=llvm-windres || exit /b 1
cmake --build build || exit /b 1
echo Built %~dp0..\SR2Recomp.exe
