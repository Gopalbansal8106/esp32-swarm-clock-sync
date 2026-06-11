# 📋 Installation Cheat Sheet

## 🎯 TL;DR - Fastest Path

### Windows (1 command)
```bash
start.bat
```

### macOS/Linux (3 commands)
```bash
chmod +x start.sh
./start.sh
```

### Any Platform (5 commands)
```bash
python -m venv venv
# Windows: venv\Scripts\activate
# Mac/Linux: source venv/bin/activate
cd backend
pip install -r requirements.txt
python main.py
```

**Then visit**: `http://localhost:8000`

---

## 🔍 Prerequisites Check

### Windows
- [ ] Python 3.8+: Open PowerShell, run `python --version`
- [ ] pip: Should be included with Python

### macOS
- [ ] Python 3.8+: Run `python3 --version`
- [ ] pip: Run `python3 -m pip --version`

### Linux
- [ ] Python 3.8+: Run `python3 --version`
- [ ] pip: Run `pip3 --version`
- [ ] If missing: `sudo apt-get install python3 python3-pip python3-venv`

---

## ⚠️ Common Issues & Fixes

| Issue | Fix |
|-------|-----|
| `'python' is not recognized` | Install Python from [python.org](https://python.org), check "Add to PATH" |
| `Cannot find module 'venv'` | Update Python: `python -m pip install --upgrade pip` |
| `Connection refused` | Check server is running, port 8000 is free |
| `Module not found` | Activate venv first, then reinstall: `pip install --force-reinstall -r requirements.txt` |
| `Dashboard won't load` | Clear browser cache (Ctrl+Shift+Delete), try different port |

---

## 📝 Step-by-Step (Windows)

1. **Open PowerShell** in project directory
2. **Create venv**: `python -m venv venv`
3. **Activate venv**: `venv\Scripts\activate`
4. **Navigate**: `cd backend`
5. **Install deps**: `pip install -r requirements.txt`
6. **Run server**: `python main.py`
7. **Open browser**: `http://localhost:8000`

---

## 📝 Step-by-Step (macOS/Linux)

1. **Open Terminal** in project directory
2. **Make script executable**: `chmod +x start.sh`
3. **Run script**: `./start.sh`
   - Or manually:
4. **Create venv**: `python3 -m venv venv`
5. **Activate venv**: `source venv/bin/activate`
6. **Navigate**: `cd backend`
7. **Install deps**: `pip install -r requirements.txt`
8. **Run server**: `python main.py`
9. **Open browser**: `http://localhost:8000`

---

## 🔧 Port Already in Use?

```bash
# Windows - Find what's using port 8000
netstat -ano | findstr :8000

# Kill the process (PID from above)
taskkill /PID <PID> /F

# Or use different port:
# Edit backend/main.py:
# uvicorn.run(app, port=9000)
```

```bash
# macOS/Linux
lsof -i :8000
kill -9 <PID>

# Or use different port:
# Edit backend/main.py:
# uvicorn.run(app, port=9000)
```

---

## 🌐 Network Access

**Local machine only** (default):
- `http://localhost:8000`
- `http://127.0.0.1:8000`

**Other machines on network**:
1. Find your IP: 
   - Windows: `ipconfig` → look for IPv4
   - Mac/Linux: `ifconfig` → look for inet
2. Access from other PC: `http://YOUR_IP:8000`
3. Access from phone: `http://YOUR_IP:8000`

**Internet access** (advanced):
- Use ngrok or cloudflare tunnel
- Or deploy to cloud (AWS, Heroku, etc.)

---

## 🎯 Verify Installation

Once server is running, check:

```bash
# Open new terminal/PowerShell
# Windows
curl http://localhost:8000

# Mac/Linux
curl http://localhost:8000

# Should return HTML (dashboard)
```

---

## 🚀 Performance Tuning

If dashboard is slow:

### Browser Level
- Use Chrome (fastest)
- Close other tabs
- Disable extensions

### Dashboard Level
Edit `frontend/index.html`:
```javascript
// Reduce update frequency
timeout = 50;  // was 16ms (60 FPS), now ~20 FPS
```

### Simulation Level
Edit `backend/simulation.py`:
```python
# Reduce number of nodes
num_nodes = 4  # instead of 8
```

### Network Level
```bash
# Use localhost instead of 0.0.0.0
# Edit backend/main.py:
# host="127.0.0.1"
```

---

## 📦 Dependency Versions

Installed packages:
- **fastapi==0.104.1**
- **uvicorn==0.24.0** (with extras)
- **python-multipart==0.0.6**
- **websockets==12.0**

All compatible with Python 3.8-3.12

---

## 🔄 Update/Reinstall

```bash
# Clear everything and reinstall
rm -r venv  # or rmdir /s venv (Windows)

# Recreate venv
python -m venv venv

# Activate and reinstall
# [activate venv]
pip install --upgrade -r backend/requirements.txt
```

---

## ✅ Success Checklist

- [ ] Python 3.8+ installed
- [ ] `pip install -r requirements.txt` completed
- [ ] Server running: `python main.py`
- [ ] No errors in console
- [ ] Browser shows dark UI
- [ ] 8 nodes visible
- [ ] Buttons responsive
- [ ] Charts animating

✅ **All checked? You're ready!**

---

## 🎮 First Test Run

1. **Dashboard loaded**
2. **Click "▶️ Start Sync"**
   - Should see yellow "synchronizing" nodes
   - Charts should update
   - Error should decrease

3. **Click "➕ Add Node"**
   - Should see 9th node appear
   - Topology should update

4. **Click "📉 Packet Loss"**
   - Nodes may show "drifted" state
   - Convergence may slow

5. **Click "🔄 Reset"**
   - Should return to initial state
   - Clocks reset to 0

✅ **All working? You're good!**

---

## 💡 Tips

- Use **Firefox DevTools** for performance profiling
- Check **Console** (F12) for error messages
- Read **README.md** for detailed docs
- Edit **config.py** for easy customization
- Add **print()** statements for debugging

---

## 🆘 Still Stuck?

1. Check **README.md** - Troubleshooting section
2. Check **Python version**: `python --version`
3. Check **Dependencies**: `pip list | grep fastapi`
4. Check **Ports**: `netstat -an | grep 8000` (Windows)
5. Try **different browser**: Chrome recommended
6. Try **fresh venv**: Remove and recreate

---

## 📚 Documentation

- **README.md** - Full documentation
- **QUICKSTART.md** - Quick setup
- **PROJECT_SUMMARY.md** - Project overview
- **config.py** - Configuration guide
- **Code comments** - Inline documentation

---

**Good luck! Happy simulating! 🚀**
