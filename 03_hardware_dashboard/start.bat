@echo off
title ESP32 Clock Sync Hardware Dashboard Launcher
cd /d "%~dp0"

echo =========================================================
echo   Distributed Wireless Clock Synchronization Dashboard
echo             [ REAL HARDWARE EDITION ]
echo =========================================================
echo.

:: Check for Python
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo ❌ Error: Python is not installed or not in PATH!
    pause
    exit /b 1
)

:: Create virtual environment if it doesn't exist
if not exist venv (
    echo 🔄 Creating Python virtual environment...
    python -m venv venv
)

:: Activate virtual environment
call venv\Scripts\activate.bat

:: Install dependencies
echo 📚 Installing dependencies...
python -m pip install --upgrade pip >nul
pip install fastapi uvicorn websockets pyserial >nul

echo.
echo ✅ Environment ready. starting hardware bridge server...
echo.
python main.py

pause
