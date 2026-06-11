# 🔧 Debugging Guide: "START SYNC" Not Responding

If the "START SYNC" button is not working, follow these steps to diagnose the issue.

## 🎯 Quick Diagnostic Checklist

- [ ] Backend is running (terminal should show output)
- [ ] Browser shows dashboard (you can see nodes)
- [ ] Network Status shows "🟢 ONLINE"
- [ ] Browser console shows ✅ WebSocket connected
- [ ] Button sends command to server
- [ ] Server receives command
- [ ] Server broadcasts state back to client

## 📋 Step-by-Step Debugging

### Step 1: Check Backend Console

**Look for these messages when you click buttons:**

```
✅ Command: START SYNC - Sync active: True
```

If you DON'T see this, the backend isn't receiving commands.

**Fix**: Check backend is running and WebSocket is connected.

### Step 2: Check Browser Console

Open browser DevTools: **F12** (Windows/Linux) or **Cmd+Option+I** (Mac)

Go to **Console** tab.

**Click "START SYNC" and look for:**

```
📤 Sending command: start_sync {}
```

If you see ✅ in green = command is being sent

If you see ❌ in red = something is wrong with WebSocket

### Step 3: Test WebSocket Connection

In browser console, type:

```javascript
console.log('WebSocket state:', ws.readyState);
console.log('WebSocket URL:', ws.url);
```

Expected output:
```
WebSocket state: 1        // 1 = OPEN
WebSocket URL: ws://localhost:8000/ws
```

If state is not 1, WebSocket is disconnected.

### Step 4: Send Test Command Manually

In browser console, type:

```javascript
sendCommand('start_sync');
```

Watch:
1. Browser console for "📤 Sending command"
2. Backend terminal for "✅ Command:"
3. Dashboard should update

### Step 5: Check Network Tab

In DevTools, go to **Network** tab:

1. Filter by "WS" (WebSocket)
2. Look for `/ws` connection
3. Click it and go to **Messages** tab
4. Click "START SYNC" button
5. You should see outgoing message with `type: "start_sync"`

If no WebSocket in Network tab = connection never established.

## 🐛 Common Issues & Solutions

### Issue #1: WebSocket Shows State 3 or 2

**State codes:**
- 0 = CONNECTING
- 1 = OPEN ✅
- 2 = CLOSING
- 3 = CLOSED ❌

**Solution:**
```bash
# Restart backend
# In backend terminal, press Ctrl+C
# Then run: python main.py
```

### Issue #2: "WebSocket not open" Error

**Cause:** Connection is disconnected or never established

**Solution:**
```javascript
// In browser console:
console.log('Connected?', ws && ws.readyState === 1);

// Force reconnect:
connectWebSocket();
```

### Issue #3: Command Sent But Backend Doesn't Receive

**Check backend logs** - should show:
```
✅ Command: START SYNC - Sync active: True
```

If not appearing:
1. Check backend is actually running
2. Try restarting backend
3. Check for errors in backend terminal

### Issue #4: Backend Receives Command But Nothing Happens

**Possible causes:**
1. Simulation not running
2. State not broadcasting
3. Callback not registered

**Check:**
```python
# Add this to backend terminal temporarily
print(simulation_controller.is_running)
print(simulation_controller.simulation.sync_active)
```

## 🧪 Test Procedure

### Full Debug Test

1. **Start backend:**
   ```bash
   cd backend
   python main.py
   ```
   
   Expected output:
   ```
   🚀 Distributed Sync Dashboard starting...
   📡 Simulating 8 nodes
   ✅ WebSocket ready for connections
   INFO:     Uvicorn running on http://0.0.0.0:8000
   ```

2. **Open browser:**
   - Navigate to `http://localhost:8000`
   - Open DevTools (F12)
   - Go to Console tab

3. **Check connection:**
   - Should see: `✅ WebSocket connected`
   - Header should show: `🟢 ONLINE`

4. **Send test command:**
   - Click "START SYNC" button
   - Backend terminal should show: `✅ Command: START SYNC - Sync active: True`
   - Browser console should show: `📤 Sending command: start_sync {}`

5. **Verify state change:**
   - Nodes should show "SYNCHRONIZING" (yellow)
   - Error graph should update
   - Convergence metric should increase

## 📊 Expected Behavior After FIX

### When you click "START SYNC":

**Backend terminal shows:**
```
✅ Command: START SYNC - Sync active: True
```

**Browser console shows:**
```
📤 Sending command: start_sync {}
```

**Dashboard shows:**
- Node status changes from "UNSYNCHRONIZED" to "SYNCHRONIZING" (yellow)
- Error graph drops
- Convergence metric increases
- Header status shows "🟢 SYNCING"

## 🔍 Advanced Debugging

### Log All Commands

Edit `backend/main.py`, find `handle_command`:

```python
async def handle_command(command: dict):
    cmd_type = command.get('type')
    print(f"📥 RECEIVED COMMAND: {command}")  # ← Add this line
    
    try:
        # ... rest of function ...
```

### Log State Broadcasts

Edit `backend/main.py`, find `broadcast_state`:

```python
def broadcast_state(state: dict):
    async def broadcast():
        num_clients = len(connected_clients)
        print(f"📡 Broadcasting to {num_clients} clients - Sync: {state['sync_active']}")
        # ... rest of function ...
```

### Test with curl

In another terminal, while backend is running:

```bash
# Check if server is responding
curl http://localhost:8000

# Should return the HTML dashboard
```

## 🆘 If Still Not Working

1. **Restart everything:**
   ```bash
   # Stop backend (Ctrl+C)
   # Close browser tab
   # Refresh page
   # Click button
   ```

2. **Check Python version:**
   ```bash
   python --version
   # Should be 3.8+
   ```

3. **Reinstall dependencies:**
   ```bash
   cd backend
   pip install --force-reinstall -r requirements.txt
   python main.py
   ```

4. **Check firewall:**
   - Ensure port 8000 is not blocked
   - Try different port in `config.py`

5. **Try different browser:**
   - Chrome (recommended)
   - Firefox
   - Edge

## 📞 Debug Output Template

When reporting issues, capture this information:

```
BACKEND TERMINAL OUTPUT:
─────────────────────────
[paste what you see when starting backend]

BROWSER CONSOLE OUTPUT:
──────────────────────
[paste console output from F12]

NETWORK TAB WEBSOCKET:
─────────────────────
[go to Network tab, filter by WS, describe what you see]

ACTIONS TAKEN:
──────────────
1. [describe what you did]
2. [what happened]
3. [what you expected]
```

---

## 🎉 Success Indicators

✅ You've fixed it when you see:

- Backend shows `✅ Command: START SYNC`
- Browser console shows `📤 Sending command`
- Dashboard nodes change to yellow
- Error graph starts decreasing
- Convergence metric goes up

**Happy debugging! 🔧**
