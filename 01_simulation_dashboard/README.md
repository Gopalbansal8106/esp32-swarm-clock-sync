# 🌐 Distributed Wireless Clock Synchronization Dashboard

A **professional-grade real-time web dashboard** simulating a distributed embedded systems network where wireless nodes synchronize their clocks using a distributed algorithm.

## 🎯 Project Overview

This simulation system models a real-world distributed clock synchronization network (similar to aerospace/industrial IoT systems). It features:

- **8+ Wireless Nodes** with independent clock drift
- **Master-Slave Synchronization Algorithm** 
- **Network Latency & Jitter Simulation**
- **Packet Loss & Network Instability**
- **Real-Time Animated Visualizations**
- **Professional Dark Futuristic UI**
- **Interactive Control Panel**

### Key Simulation Features

✅ Independent oscillator drift for each node  
✅ Network packet transmission with animated visualization  
✅ Synchronization error convergence tracking  
✅ Dynamic node addition/removal (2-20 nodes)  
✅ Faulty node behavior simulation  
✅ Network stability controls  
✅ Real-time metrics & statistics  

## 🏗️ Project Structure

```
distributed-sync-dashboard/
├── backend/
│   ├── main.py              # FastAPI server & WebSocket handler
│   ├── simulation.py         # Core simulation engine
│   └── requirements.txt      # Python dependencies
├── frontend/
│   └── index.html            # Single-file web dashboard
└── README.md                 # This file
```

## 🚀 Quick Start

### Prerequisites

- **Python 3.8+**
- **pip** (Python package manager)
- Modern web browser (Chrome, Firefox, Edge)

### Installation

1. **Clone/Extract the project**
   ```bash
   cd distributed-sync-dashboard
   ```

2. **Create virtual environment** (recommended)
   ```bash
   # Windows
   python -m venv venv
   venv\Scripts\activate

   # macOS/Linux
   python3 -m venv venv
   source venv/bin/activate
   ```

3. **Install dependencies**
   ```bash
   cd backend
   pip install -r requirements.txt
   ```

4. **Run the server**
   ```bash
   python main.py
   ```

   Expected output:
   ```
   ╔═════════════════════════════════════════════════════════╗
   ║  Distributed Wireless Clock Synchronization Dashboard   ║
   ║                    FastAPI Backend                       ║
   ╚═════════════════════════════════════════════════════════╝
   
   INFO:     Uvicorn running on http://0.0.0.0:8000
   ```

5. **Open in browser**
   - Navigate to: `http://localhost:8000`
   - Dashboard should load with 8 nodes in a network topology

## 📊 Dashboard Components

### 1. **Network Topology Panel** (Left)
- Visual representation of master and slave nodes
- Circular layout showing network connectivity
- Animated packet transfers between nodes
- Node color indicates synchronization state:
  - 🟢 **Green**: Synchronized
  - 🟡 **Yellow**: Synchronizing
  - 🔴 **Red**: Drifted
  - 🔵 **Cyan**: Master Node

### 2. **Clock Graph** (Top-Right)
- Real-time plot of each node's local clock value
- Shows clock drift before synchronization
- Visualizes convergence after sync activation
- Master clock (thicker line) vs. slave clocks

### 3. **Synchronization Error Graph** (Bottom-Right)
- Average synchronization error over time
- Should decrease as nodes synchronize
- Visual indicator of convergence speed

### 4. **Global Metrics Panel** (Right-Side Cards)
- **Avg Latency**: Network round-trip time
- **Packet Loss**: Network reliability percentage
- **Sync Status**: Active or stopped
- **Convergence**: Synchronization progress

### 5. **Node Status Panel** (Lower-Right)
- Individual card for each node
- Displays:
  - Local clock time
  - Drift rate (PPM - parts per million)
  - Synchronization error
  - Network latency

### 6. **Control Panel** (Bottom)
- **▶️ Start Sync**: Begin synchronization algorithm
- **⏹️ Stop Sync**: Pause synchronization
- **➕ Add Node**: Dynamically add new node (up to 20)
- **➖ Remove Node**: Remove slave node
- **🔄 Reset**: Return to initial state
- **📉 Packet Loss**: Introduce random packet loss (5%)
- **⚡ Increase Drift**: Amplify clock drift rates
- **🔴 Faulty Node**: Create node with extreme drift

## 🎮 Usage Guide

### Basic Simulation Flow

1. **Dashboard starts with 8 nodes**
   - All nodes have independent random drift rates
   - Master (Node #0) is marked with cyan highlight

2. **Click "Start Sync"**
   - Master begins sending timestamp packets to slaves
   - Slaves gradually synchronize their clocks
   - Watch the clock graph converge
   - Error graph shows decreasing error

3. **Monitor Convergence**
   - Error graph should show exponential decrease
   - Node cards will show "SYNCHRONIZED" status
   - Convergence metric increases toward 100%

4. **Test Network Instability**
   - Click "Packet Loss" to introduce random packet drops
   - Click "Increase Drift" to make synchronization harder
   - Click "Faulty Node" to create extreme drift node
   - Observe recovery behavior

5. **Dynamic Node Management**
   - Add/remove nodes while simulation runs
   - Newly added nodes automatically join sync
   - Topology updates in real-time

## 🔧 Advanced Configuration

### Modify Simulation Parameters

Edit `backend/simulation.py`:

```python
# In DistributedSyncSimulation.__init__():

# Convergence speed (0.0 = slow, 1.0 = fast)
self.convergence_speed = 0.05

# Maximum number of nodes
# Change this in add_node() check:
if len(self.nodes) >= 20:  # Modify this

# Initial drift range (PPM)
drift_rate = random.uniform(-100, 100)  # Change range

# Network latency (ms)
random.gauss(20, 5)  # Change mean and std dev
```

### Change Server Port

Edit `backend/main.py`:

```python
uvicorn.run(
    app,
    host="0.0.0.0",
    port=8000,  # Change this port
    ...
)
```

Then access via: `http://localhost:YOUR_PORT`

### Adjust Visual Theme

Edit `frontend/index.html` CSS variables in `<style>`:

```css
:root {
    --primary: #0a0e27;           /* Dark background */
    --accent: #00ff88;             /* Green accent */
    --error: #ff0055;              /* Red error color */
    --warning: #ffaa00;            /* Yellow warning */
    /* ... modify other colors ... */
}
```

## 📡 WebSocket API Reference

### Commands (Client → Server)

The frontend sends these commands via WebSocket:

```javascript
// Start synchronization
{ type: 'start_sync' }

// Stop synchronization
{ type: 'stop_sync' }

// Add new node
{ type: 'add_node' }

// Remove node
{ type: 'remove_node', node_id: 1 }

// Reset simulation
{ type: 'reset' }

// Inject packet loss (0-1)
{ type: 'inject_packet_loss', rate: 0.1 }

// Increase drift (multiply by factor)
{ type: 'increase_drift', factor: 1.5 }

// Make node faulty
{ type: 'simulate_faulty_node', node_id: 1, is_faulty: true }

// Set convergence speed
{ type: 'set_convergence_speed', speed: 0.05 }
```

### State Broadcast (Server → Client)

Server broadcasts state ~60 times per second:

```javascript
{
    type: 'state',
    data: {
        global_time: 125.432,
        sync_active: true,
        nodes: [
            {
                node_id: 0,
                local_time: 125.435,
                drift_rate: -12.5,  // PPM
                offset: 0.0,
                sync_state: 'synchronized',
                latency_estimate: 24.5,  // ms
                packet_loss_rate: 0.01,
                packets_sent: 1254,
                packets_received: 1243,
                sync_error: 0.0002,  // seconds
                is_master: true,
                is_faulty: false
            },
            // ... more nodes ...
        ],
        master_id: 0,
        total_packets_sent: 12540,
        total_packets_lost: 45,
        avg_sync_error: 0.00015,
        sync_history: [
            [0.0, 2.345],      // [time, error]
            [0.016, 2.340],
            // ... more points ...
        ],
        num_nodes: 8
    }
}
```

## 🔍 Understanding the Simulation

### Clock Drift Model

Each node's local clock advances according to:

```
local_time += dt * (1 + drift_rate/1,000,000) * (1 + network_jitter)
```

Where:
- **drift_rate**: PPM (parts per million) deviation
- **dt**: Time delta since last update
- **network_jitter**: Simulates real oscillator instability

### Synchronization Algorithm

Master-slave synchronization:

1. **Master sends** its current time to all slaves periodically
2. **Each slave** calculates time offset from master
3. **Slaves gradually converge** (not instant sync)
4. **Convergence amount** = (master_time - local_time) × convergence_speed × dt

This realistic gradual convergence models real hardware behavior.

### Network Latency

Each packet experiences:
- Base latency: Gaussian(20ms, 5ms std)
- Random jitter adds realism
- Packet loss: Random drop with configured rate

## 🎨 Visual Theme

### Color Scheme
- **Primary Background**: Deep blue (#0a0e27)
- **Accent Color**: Neon green (#00ff88)
- **Master Node**: Cyan (#00ffaa)
- **Error State**: Red (#ff0055)
- **Warning State**: Orange (#ffaa00)

### Effects
- Glowing borders and node effects
- Animated packet transfer visualization
- Smooth transitions and animations
- Grid background pattern
- Gradient fills and shadows

## 🐛 Troubleshooting

### Browser Connection Issues

**Problem**: Dashboard won't connect
```
Solution:
1. Ensure backend server is running
2. Check firewall allows port 8000
3. Try different port: Edit main.py port = 9000
4. Clear browser cache (Ctrl+Shift+Delete)
5. Check browser console for errors (F12)
```

### Slow Performance

**Problem**: Dashboard feels laggy
```
Solution:
1. Reduce number of nodes
2. Close other browser tabs
3. Update GPU drivers
4. Try different browser
5. Reduce animation complexity in CSS
```

### Python Dependency Issues

**Problem**: ModuleNotFoundError
```
Solution:
1. Verify virtual environment is activated
2. Reinstall dependencies:
   pip install --upgrade -r requirements.txt
3. Check Python version (3.8+):
   python --version
4. Try with pip3:
   pip3 install -r requirements.txt
```

### WebSocket Connection Fails

**Problem**: WebSocket error in console
```
Solution:
1. Check firewall/proxy settings
2. Verify server is running
3. Check port is not blocked
4. Try localhost vs 127.0.0.1
5. Check browser WebSocket support (most modern browsers OK)
```

## 📈 Performance Notes

- **Network Topology**: Canvas-based, smooth 60 FPS animation
- **Charts**: Plotly.js with optimized rendering (100 data points)
- **WebSocket**: ~60 Hz state broadcast (~50-100 KB/s)
- **Memory**: ~100-200 MB for typical setup (8 nodes)
- **CPU**: Low usage, <5% typical

## 🎓 Educational Use

### Concepts Demonstrated

1. **Distributed Systems**: Master-slave architecture
2. **Clock Synchronization**: Timestamp exchange & convergence
3. **Network Simulation**: Latency, jitter, packet loss
4. **Real-time Systems**: 60 Hz simulation & visualization loop
5. **Web Technologies**: WebSocket, Canvas, Plotly, FastAPI
6. **UI/UX**: Professional dark theme, responsive design

### Experiments to Try

1. **Effect of Packet Loss**: Inject loss, observe recovery time
2. **Drift Sensitivity**: Increase drift, measure convergence difficulty
3. **Scalability**: Add nodes incrementally, observe latency increase
4. **Failure Recovery**: Make node faulty, observe re-synchronization
5. **Convergence Speed**: Adjust convergence_speed in code

## 🚀 Deployment

### Local Network

1. Find your machine IP: `ipconfig` (Windows) or `ifconfig` (Linux/Mac)
2. Update `main.py`: `host="0.0.0.0"` (allows external connections)
3. Access from other machine: `http://YOUR_IP:8000`

### Cloud Deployment

For deployment on services like Heroku, AWS, or Digital Ocean:

1. Create `Procfile`:
   ```
   web: cd backend && uvicorn main:app --host 0.0.0.0 --port $PORT
   ```

2. Set environment variables as needed

3. Deploy with cloud CLI

## 📝 Code Architecture

### Backend Structure

```python
# main.py
- FastAPI app initialization
- WebSocket endpoint (/ws)
- Command handling
- State broadcasting
- HTTP routes (/api/*)

# simulation.py
- DistributedSyncSimulation: Core simulation engine
- SimulationController: Threading & callback management
- NodeStats: Individual node state
- SyncState: Enum for sync states
```

### Frontend Structure

```javascript
// Sections in index.html

// Styling (CSS)
- Dark theme with accent colors
- Grid layout (3-column responsive)
- Glow & shadow effects
- Smooth animations

// Connectivity
- WebSocket connection & reconnection
- State management system
- Command sending

// Visualization
- Canvas-based topology drawing
- Plotly.js charts (clock & error graphs)
- Node status cards
- Real-time metrics

// Animation Loop
- 60 FPS canvas rendering
- Smooth transitions
- Particle effects (packets)
```

## 📄 License

This project is for educational and demonstration purposes.

## 🤝 Contributing

Suggestions for improvements:
- Additional synchronization algorithms (NTP, PTP)
- 3D network visualization
- Export simulation data
- Custom drift profiles
- Master failover simulation

## 🎯 Future Enhancements

- [ ] Gossip-based synchronization algorithm
- [ ] Byzantine fault tolerance simulation
- [ ] Data export (CSV, JSON)
- [ ] Configurable UI theme selector
- [ ] Performance profiling tools
- [ ] Recorded playback mode
- [ ] Multi-region simulation
- [ ] Custom network topologies

## 📞 Support

For issues or questions:
1. Check troubleshooting section
2. Review code comments
3. Verify dependencies installed
4. Test with Python REPL directly

## ✨ Credits

Built with:
- **FastAPI** - Modern Python web framework
- **Plotly.js** - Interactive charts
- **Canvas API** - Network visualization
- **WebSockets** - Real-time communication
- **GSAP** - Animation library

---

**Enjoy your distributed sync dashboard! 🚀**
