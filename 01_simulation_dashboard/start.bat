@echo off
REM Quick Start Script for Distributed Sync Dashboard
REM Windows Batch File

echo.
echo ╔═════════════════════════════════════════════════════════╗
echo ║  Distributed Wireless Clock Synchronization Dashboard   ║
echo ║                     Quick Start                          ║
echo ╚═════════════════════════════════════════════════════════╝
echo.

REM Check if Python is installed
python --version >nul 2>&1
if errorlevel 1 (
    echo ❌ Python not found! Please install Python 3.8+ from python.org
    pause
    exit /b 1
)

echo ✅ Python detected
python --version

REM Create virtual environment if it doesn't exist
if not exist venv (
    echo.
    echo 📦 Creating virtual environment...
    python -m venv venv
    if errorlevel 1 (
        echo ❌ Failed to create virtual environment
        pause
        exit /b 1
    )
)

REM Activate virtual environment
echo.
echo 🔄 Activating virtual environment...
call venv\Scripts\activate.bat

REM Install/upgrade pip
echo.
echo 📥 Upgrading pip...
python -m pip install --upgrade pip -q

REM Install dependencies
echo.
echo 📚 Installing dependencies...
cd backend
pip install -r requirements.txt -q
cd ..

if errorlevel 1 (
    echo ❌ Failed to install dependencies
    pause
    exit /b 1
)

echo.
echo ✅ Dependencies installed successfully!

REM Run the server
echo.
echo 🚀 Starting server...
echo.
cd backend
python main.py

pause
