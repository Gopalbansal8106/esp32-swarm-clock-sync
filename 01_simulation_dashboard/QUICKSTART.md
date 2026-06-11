# ⚡ Quick Start Guide

Get the dashboard running in **2 minutes**.

## 🪟 Windows

**Option 1: Automatic (Recommended)**
```bash
double-click start.bat
```

**Option 2: Manual**
```bash
python -m venv venv
venv\Scripts\activate
cd backend
pip install -r requirements.txt
python main.py
```

## 🍎 macOS / 🐧 Linux

**Option 1: Automatic (Recommended)**
```bash
chmod +x start.sh
./start.sh
```

**Option 2: Manual**
```bash
python3 -m venv venv
source venv/bin/activate
cd backend
pip install -r requirements.txt
python main.py
```

## 🌐 Open Dashboard

Once server is running, open browser:

```
http://localhost:8000
```

You should see a dark-themed network visualization with 8 nodes arranged in a circle.

## 🎮 First Steps

1. **Click "▶️ Start Sync"** - Nodes begin synchronizing
2. **Watch the graphs** - Clock values should converge
3. **Click "📉 Packet Loss"** - Introduce network problems
4. **Click "⚡ Increase Drift"** - Make sync harder
5. **Click "➕ Add Node"** - Add more nodes dynamically

## ❌ Troubleshooting

### "Python not found"
- Install Python 3.8+ from [python.org](https://python.org)
- Restart terminal after installation

### "Connection refused"
- Ensure `python main.py` is running
- Check port 8000 is free: `netstat -an | find ":8000"`
- Try different port in `backend/main.py`

### "Module not found"
- Activate virtual environment first
- Reinstall: `pip install --force-reinstall -r requirements.txt`

### "Dashboard is laggy"
- Close other browser tabs
- Reduce number of nodes (remove some)
- Switch to Chrome if using Firefox

## 📝 Project Files

```
distributed-sync-dashboard/
├── start.bat              # Windows quick start
├── start.sh               # macOS/Linux quick start
├── config.py              # Customizable parameters
├── README.md              # Full documentation
├── QUICKSTART.md          # This file
│
├── backend/
│   ├── main.py            # FastAPI server
│   ├── simulation.py       # Simulation engine
│   └── requirements.txt    # Dependencies
│
└── frontend/
    └── index.html         # Dashboard UI (single file)
```

## 🔧 Customize Parameters

Edit `config.py`:
- Change number of nodes
- Adjust clock drift rates
- Modify network latency
- Customize colors & animations

Then restart server for changes to take effect.

## 🚀 Next Steps

- Read [README.md](README.md) for complete documentation
- Try different control panel buttons
- Experiment with configuration parameters
- Share dashboard screenshots in presentations/demos

## 🎯 What You're Looking At

- **Left Panel**: Network topology with animated packets
- **Top-Right**: Real-time clock synchronization graph
- **Bottom-Right**: Synchronization error convergence
- **Right Side**: Node status cards and metrics
- **Bottom**: Interactive control buttons

Enjoy! 🎉
