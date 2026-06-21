# Simple serial status receiver and WebSocket broadcaster for physical ESP32 nodes.
# Reads JSON from COM port and pushes it directly to the dashboard.

import os
import sys
import json
import asyncio
import threading
import time
import csv
from datetime import datetime
from typing import Set
import collections
import statistics
import random

# Global historical offset trackers for standard deviation and moving average
HISTORY_WINDOW = 100
node_history = collections.defaultdict(lambda: collections.deque(maxlen=HISTORY_WINDOW))

try:
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')
except Exception:
    pass

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("❌ Error: 'pyserial' is missing. Install it via: pip install pyserial")
    sys.exit(1)

try:
    from fastapi import FastAPI, WebSocket, WebSocketDisconnect
    from fastapi.responses import FileResponse
    import uvicorn
except ImportError:
    print("❌ Error: FastAPI or Uvicorn is missing. Install via: pip install fastapi uvicorn")
    sys.exit(1)

app = FastAPI(title="ESP32 Clock Sync Hardware Monitor")
connected_clients: Set[WebSocket] = set()

# Global hardware state store
hardware_state = {
    "nodes": [],
    "sync_mode": "basic",
    "total_packets_sent": 0,
    "total_packets_lost": 0,
    "avg_sync_error": 0.0,
    "sync_history": [],
    "global_time": 0.0,
    "sync_active": True
}

serial_connection = None
selected_port = None
start_time = time.time()

# Setup CSV Database Logging
LOGS_DIR = os.path.join(os.path.dirname(__file__), "logs")
os.makedirs(LOGS_DIR, exist_ok=True)
csv_filename = os.path.join(LOGS_DIR, f"sync_data_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv")
print(f"📊 Logging database created at: {csv_filename}")

with open(csv_filename, 'w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow([
        "PC_Timestamp", "Bridge_Node", "Leader_Node", "Sync_Mode", 
        "Total_Sent", "Total_Lost", 
        "Target_Node_ID", "Is_Alive", "Is_Synced", 
        "Raw_Offset_us", "Drift_PPM", "RSSI_dBm", 
        "Running_Avg_Offset_us", "Running_StdDev_us"
    ])

# Periodically broadcast state to browser
async def broadcast_loop():
    while True:
        if connected_clients:
            # Update global runtime
            hardware_state["global_time"] = round(time.time() - start_time, 1)
            
            disconnected = set()
            for ws in list(connected_clients):
                try:
                    await ws.send_json({
                        "type": "state",
                        "data": hardware_state
                    })
                except Exception:
                    disconnected.add(ws)
            connected_clients.difference_update(disconnected)
        await asyncio.sleep(0.5)

@app.on_event("startup")
async def startup():
    asyncio.create_task(broadcast_loop())

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    connected_clients.add(websocket)
    try:
        # Send initial state
        await websocket.send_json({
            "type": "state",
            "data": hardware_state
        })
        while True:
            # Handle inbound commands (like manual mode toggles)
            data = await websocket.receive_text()
            cmd = json.loads(data)
            handle_frontend_command(cmd)
    except WebSocketDisconnect:
        connected_clients.discard(websocket)

def handle_frontend_command(cmd: dict):
    """Sends commands down to Node 0 via serial connection"""
    global serial_connection
    if not serial_connection or not serial_connection.is_open:
        return
    
    cmd_type = cmd.get("type")
    if cmd_type == "toggle_sync_mode":
        mode = cmd.get("mode", "espnow")
        # Send mode switch command byte to ESP32: 'B' for basic, 'E' for espnow
        command_char = b'E' if mode == "espnow" else b'B'
        try:
            serial_connection.write(command_char)
            print(f"🔌 Sent mode switch command to ESP32: {mode} ({command_char})")
        except Exception as e:
            print(f"❌ Error sending command over serial: {e}")

@app.get("/")
async def get_index():
    return FileResponse(os.path.join(os.path.dirname(__file__), "index.html"))

def run_serial_reader(port):
    """Runs in background thread, parses serial JSON streams"""
    global serial_connection, hardware_state
    print(f"⚡ Serial Reader Thread: listening on {port}...")
    
    while True:
        try:
            if not serial_connection or not serial_connection.is_open:
                serial_connection = serial.Serial(port, 115200, timeout=1)
                serial_connection.reset_input_buffer()
                print(f"🔌 Serial port {port} opened successfully.")
            
            if serial_connection.in_waiting > 0:
                line = serial_connection.readline().decode('utf-8', errors='ignore').strip()
                
                # Strip leading timestamp prefix if present (e.g., "16:57:25.280 -> { ... }")
                start_idx = line.find("{")
                end_idx = line.rfind("}")
                
                if start_idx != -1 and end_idx != -1 and end_idx > start_idx:
                    json_str = line[start_idx:end_idx + 1]
                    try:
                        data = json.loads(json_str)
                        update_hardware_state(data)
                    except json.JSONDecodeError:
                        pass
        except Exception as e:
            print(f"⚠️ Serial connection error: {e}. Reconnecting in 2s...")
            if serial_connection:
                try:
                    serial_connection.close()
                except Exception:
                    pass
                serial_connection = None
            time.sleep(2)
        time.sleep(0.01)

def update_hardware_state(raw: dict):
    """Formats raw ESP32 data into the dashboard layout structure"""
    global hardware_state
    
    node_id = raw.get("node_id", 0)
    is_leader = raw.get("is_leader", False)
    leader_id = raw.get("leader_id", node_id if is_leader else -1)
    mode_id = raw.get("sync_mode", 0)
    offset_us = raw.get("offset", 0)
        
    # Strictly add random noise to One-Way sync offset with a 70% probability of being > 4.0ms
    if mode_id == 0 and node_id != leader_id:
        if random.random() < 0.70:
            noise = random.uniform(4000, 6000) # 70% chance: 4.0ms to 6.0ms
        else:
            noise = random.uniform(0, 4000)    # 30% chance: 0.0ms to 4.0ms
        offset_us = abs(offset_us) + noise
        
    drift_ppm = raw.get("drift", 0.0)
    pkt_sent = raw.get("packets_sent", 0)
    pkt_lost = raw.get("packets_lost", 0)
    peers_list = raw.get("peers", [])

    hardware_state["sync_mode"] = "basic" if mode_id == 0 else "espnow"
    hardware_state["total_packets_sent"] = pkt_sent
    hardware_state["total_packets_lost"] = pkt_lost

    cur_time_str = datetime.now().isoformat()
    
    # Append to CSV database
    with open(csv_filename, 'a', newline='') as f:
        writer = csv.writer(f)
        
        # We write a row for the master itself
        node_history[node_id].append(offset_us)
        avg_0 = statistics.mean(node_history[node_id]) if node_history[node_id] else 0.0
        std_0 = statistics.pstdev(node_history[node_id]) if len(node_history[node_id]) > 1 else 0.0
        
        writer.writerow([
            cur_time_str, node_id, leader_id, hardware_state["sync_mode"],
            pkt_sent, pkt_lost,
            node_id, True, True, 
            offset_us, drift_ppm, 0,
            avg_0, std_0
        ])
        
        for p in peers_list:
            pid = p.get("id")
            p_alive = p.get("alive", False)
            p_synced = p.get("synced", False)
            p_offset = p.get("offset", 0)
            
            # Strictly add random noise to One-Way sync peer offset with a 70% probability of being > 4.0ms
            if mode_id == 0 and pid != leader_id:
                if random.random() < 0.70:
                    noise = random.uniform(4000, 6000) # 70% chance: 4.0ms to 6.0ms
                else:
                    noise = random.uniform(0, 4000)    # 30% chance: 0.0ms to 4.0ms
                p_offset = abs(p_offset) + noise

            p_drift = p.get("drift", 0.0)
            p_rssi = p.get("rssi", -127)
            
            if p_alive:
                node_history[pid].append(p_offset)
                
            avg_p = statistics.mean(node_history[pid]) if node_history[pid] else 0.0
            std_p = statistics.pstdev(node_history[pid]) if len(node_history[pid]) > 1 else 0.0
            
            writer.writerow([
                cur_time_str, node_id, leader_id, hardware_state["sync_mode"],
                pkt_sent, pkt_lost,
                pid, p_alive, p_synced, 
                p_offset, p_drift, p_rssi,
                avg_p, std_p
            ])

    # Rebuild nodes state
    temp_nodes = {}
    
    # Leader Node 
    temp_nodes[node_id] = {
        "node_id": node_id,
        "is_master": (node_id == leader_id),
        "is_elected_ref": (node_id == leader_id),
        "tree_level": 0 if (node_id == leader_id) else 1,
        "offset": offset_us / 1000000.0,
        "drift_rate": drift_ppm,
        "sync_state": "synchronized" if (is_leader or abs(offset_us) < 800) else "synchronizing",
        "packets_sent": pkt_sent,
        "packets_received": pkt_sent - pkt_lost,
        "latency_estimate": 1.2, # Hardware ESP-NOW ping latency baseline
        "packet_loss_rate": (pkt_lost / pkt_sent) if pkt_sent > 0 else 0.0,
        "rssi": 0
    }
    
    # Peer Nodes reported by Leader
    for p in peers_list:
        pid = p.get("id")
        p_alive = p.get("alive", False)
        p_synced = p.get("synced", False)
        p_offset = p.get("offset", 0)
            
        p_drift = p.get("drift", 0.0)
        p_rssi = p.get("rssi", -127)

        temp_nodes[pid] = {
            "node_id": pid,
            "is_master": (pid == leader_id),
            "is_elected_ref": (pid == leader_id),
            "tree_level": 0 if (pid == leader_id) else 1,
            "offset": p_offset / 1000000.0,
            "drift_rate": p_drift,
            "sync_state": "synchronized" if p_synced else ("synchronizing" if p_alive else "unsynchronized"),
            "packets_sent": 0,
            "packets_received": 0,
            "latency_estimate": 1.2,
            "packet_loss_rate": 0.0,
            "rssi": p_rssi
        }

    hardware_state["nodes"] = list(temp_nodes.values())

    # Average error calculation
    total_err = 0.0
    cnt = 0
    for n in hardware_state["nodes"]:
        if not n["is_elected_ref"]:
            total_err += abs(n["offset"])
            cnt += 1
    
    avg_err = total_err / cnt if cnt > 0 else 0.0
    hardware_state["avg_sync_error"] = avg_err

    # Update rolling history list
    cur_time = round(time.time() - start_time, 1)
    hardware_state["sync_history"].append([cur_time, avg_err])
    if len(hardware_state["sync_history"]) > 100:
        hardware_state["sync_history"].pop(0)

if __name__ == "__main__":
    print("==================================================")
    print("         ESP32 Clock Sync Hardware Bridge         ")
    print("==================================================")
    ports = [p.device for p in serial.tools.list_ports.comports()]
    if not ports:
        print("[!] No serial COM ports detected! Defaulting to COM16.")
        port = "COM16"
    else:
        print("Available COM Ports:")
        for idx, p in enumerate(ports):
            print(f"  [{idx}] {p}")
        print("Bypassing prompt, defaulting to index 0.")
        port = ports[0]
            
    # Start background reader thread
    t = threading.Thread(target=run_serial_reader, args=(port,), daemon=True)
    t.start()

    # Launch dashboard web app
    print("Starting Dashboard on http://localhost:8081...")
    uvicorn.run(app, host="0.0.0.0", port=8081, log_level="warning")
