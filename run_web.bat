@echo off
cd /d "%~dp0"
call activate_venv.bat
python -m uvicorn api.main:app --host 127.0.0.1 --port 8000
