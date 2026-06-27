@echo off
cd /d "%~dp0"

if not exist "build\Release\ufc_server.exe" (
    echo Building C++ server...
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 exit /b 1
    cmake --build build --config Release
    if errorlevel 1 exit /b 1
)

if not exist "frontend\dist\index.html" (
    echo Frontend build not found. Run: cd frontend ^&^& npm install ^&^& npm run build
)

build\Release\ufc_server.exe --port 8000
