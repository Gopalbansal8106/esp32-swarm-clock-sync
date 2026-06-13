# Simple serial status receiver and WebSocket broadcaster for physical ESP32 nodes.
# Reads JSON from COM port and pushes it directly to the dashboard.

import os
import sys
import json
import asyncio
import threading
import time
from typing import Set

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

# Periodically broadcast state to browser
async def broadcast_loop():
    while True:
        if connected_clients:
            # Update global runtime
            hardware_state["global_time"] = round(time.time() - start_time, 1)
            
            disconnected = set()
            for ws in connected_clients:
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
    mode_id = raw.get("sync_mode", 0)
    offset_us = raw.get("offset", 0)
    drift_ppm = raw.get("drift", 0.0)
    pkt_sent = raw.get("packets_sent", 0)
    pkt_lost = raw.get("packets_lost", 0)
    peers_list = raw.get("peers", [])

    hardware_state["sync_mode"] = "basic" if mode_id == 0 else "espnow"
    hardware_state["total_packets_sent"] = pkt_sent
    hardware_state["total_packets_lost"] = pkt_lost

    # Rebuild nodes state
    temp_nodes = {}
    
    # Leader Node 
    temp_nodes[node_id] = {
        "node_id": node_id,
        "is_master": is_leader,
        "is_elected_ref": is_leader,
        "tree_level": 0 if is_leader else 1,
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
            "is_master": False,
            "is_elected_ref": False,
            "tree_level": 1,
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
        print("[!] No serial COM ports detected! Check USB connections.")
        port = input("Enter COM port manually (e.g. COM3): ").strip()
    else:
        print("Available COM Ports:")
        for idx, p in enumerate(ports):
            print(f"  [{idx}] {p}")
        selection = input(f"Select COM port [0-{len(ports)-1}] (default 0): ").strip()
        try:
            port = ports[int(selection)] if selection else ports[0]
        except Exception:
            port = ports[0]
            
    # Start background reader thread
    t = threading.Thread(target=run_serial_reader, args=(port,), daemon=True)
    t.start()

    # Launch dashboard web app
    print("Starting Dashboard on http://localhost:8080...")
    uvicorn.run(app, host="0.0.0.0", port=8080, log_level="warning")
