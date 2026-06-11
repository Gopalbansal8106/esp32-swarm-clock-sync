#!/bin/bash
# Quick Start Script for Distributed Sync Dashboard
# macOS/Linux Shell Script

echo ""
echo "╔═════════════════════════════════════════════════════════╗"
echo "║  Distributed Wireless Clock Synchronization Dashboard   ║"
echo "║                     Quick Start                          ║"
echo "╚═════════════════════════════════════════════════════════╝"
echo ""

# Check if Python is installed
if ! command -v python3 &> /dev/null; then
    echo "❌ Python3 not found! Please install Python 3.8+ from python.org"
    exit 1
fi

echo "✅ Python detected"
python3 --version

# Create virtual environment if it doesn't exist
if [ ! -d "venv" ]; then
    echo ""
    echo "📦 Creating virtual environment..."
    python3 -m venv venv
    
    if [ $? -ne 0 ]; then
        echo "❌ Failed to create virtual environment"
        exit 1
    fi
fi

# Activate virtual environment
echo ""
echo "🔄 Activating virtual environment..."
source venv/bin/activate

# Install/upgrade pip
echo ""
echo "📥 Upgrading pip..."
python -m pip install --upgrade pip -q

# Install dependencies
echo ""
echo "📚 Installing dependencies..."
cd backend
pip install -r requirements.txt -q
cd ..

if [ $? -ne 0 ]; then
    echo "❌ Failed to install dependencies"
    exit 1
fi

echo ""
echo "✅ Dependencies installed successfully!"

# Run the server
echo ""
echo "🚀 Starting server..."
echo ""
cd backend
python main.py
