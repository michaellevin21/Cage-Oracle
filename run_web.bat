@echo off
cd /d "%~dp0"

echo Building C++ server...
cmake -B build -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 (
    echo.
    echo Build failed. If ufc_server.exe is running, stop it and run this script again.
    exit /b 1
)

if not exist "frontend\dist\index.html" (
    echo Frontend build not found. Run: cd frontend ^&^& npm install ^&^& npm run build
)

echo Fetching upcoming UFC matchups...
python upcoming_matchups.py --json --db ufc.db --output .upcoming_matchups_cache.json
if errorlevel 1 (
    echo Warning: failed to refresh upcoming matchups cache.
)

build\Release\ufc_server_app.exe --port 8000
