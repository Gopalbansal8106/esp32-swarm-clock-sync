"""
FastAPI Backend for Distributed Sync Dashboard

Serves the frontend and maintains WebSocket connections for real-time updates.
Manages the simulation controller and broadcasts state changes to all connected clients.
"""

import sys
try:
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')
except Exception:
    pass

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
import asyncio
import json
import os
from typing import Set

from simulation import SimulationController

# Initialize FastAPI app
app = FastAPI(title="Distributed Sync Dashboard")

# Global simulation controller
simulation_controller = SimulationController()

# Set to track connected WebSocket clients
connected_clients: Set[WebSocket] = set()


# ============================================================================
# WebSocket Endpoint
# ============================================================================

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """
    WebSocket endpoint for real-time simulation updates.
    
    Clients connect here and receive continuous state updates.
    Also handles commands from clients to control the simulation.
    """
    await websocket.accept()
    connected_clients.add(websocket)
    
    try:
        # Start simulation if not running
        if not simulation_controller.is_running:
            simulation_controller.start()
        
        # Send initial state
        state = simulation_controller.simulation.get_state()
        await websocket.send_json({
            'type': 'state',
            'data': state
        })
        
        # Listen for commands from client
        while True:
            data = await websocket.receive_text()
            print(f"📨 WebSocket received: {data}")
            command = json.loads(data)
            print(f"🔧 Parsed command: {command}")
            await handle_command(command)
            
    except WebSocketDisconnect:
        connected_clients.discard(websocket)
    except Exception as e:
        print(f"WebSocket error: {e}")
        connected_clients.discard(websocket)


async def handle_command(command: dict):
    """Handle commands from frontend"""
    cmd_type = command.get('type')
    
    try:
        if cmd_type == 'start_sync':
            # Restart controller loop if it died (e.g. after reset)
            if not simulation_controller.is_running:
                simulation_controller.is_running = True
                simulation_controller.simulation.is_running = True
                import threading
                simulation_controller.thread = threading.Thread(
                    target=simulation_controller._run_loop, daemon=True
                )
                simulation_controller.thread.start()
            simulation_controller.simulation.start_synchronization()
            print(f"✅ Command: START SYNC - Sync active: {simulation_controller.simulation.sync_active}")
        
        elif cmd_type == 'stop_sync':
            simulation_controller.simulation.stop_synchronization()
            print(f"✅ Command: STOP SYNC")
        
        elif cmd_type == 'add_node':
            result = simulation_controller.simulation.add_node()
            print(f"✅ Command: ADD NODE - Success: {result}")
        
        elif cmd_type == 'remove_node':
            node_id = command.get('node_id')
            # If no node_id provided, remove the highest numbered slave node
            if node_id is None:
                slave_nodes = [n for n in simulation_controller.simulation.nodes.keys() 
                              if not simulation_controller.simulation.nodes[n].is_master]
                if slave_nodes:
                    node_id = max(slave_nodes)
            result = simulation_controller.simulation.remove_node(node_id)
            print(f"✅ Command: REMOVE NODE {node_id} - Success: {result}")
        
        elif cmd_type == 'reset':
            # Stop the loop, reset state, restart the loop
            simulation_controller.is_running = False
            simulation_controller.simulation.is_running = False
            if simulation_controller.thread:
                simulation_controller.thread.join(timeout=1)
            simulation_controller.simulation.reset()
            simulation_controller.is_running = True
            simulation_controller.simulation.is_running = True
            import threading
            simulation_controller.thread = threading.Thread(
                target=simulation_controller._run_loop, daemon=True
            )
            simulation_controller.thread.start()
            print(f"✅ Command: RESET - Loop restarted")
        
        elif cmd_type == 'inject_packet_loss':
            rate = command.get('rate', 0.1)
            simulation_controller.simulation.inject_packet_loss(rate)
            print(f"✅ Command: PACKET LOSS {rate*100}%")
        
        elif cmd_type == 'increase_drift':
            factor = command.get('factor', 1.5)
            simulation_controller.simulation.increase_drift(factor)
            print(f"✅ Command: INCREASE DRIFT x{factor}")
        
        elif cmd_type == 'simulate_faulty_node':
            node_id = command.get('node_id', 1)
            is_faulty = command.get('is_faulty', True)
            simulation_controller.simulation.simulate_faulty_node(node_id, is_faulty)
            print(f"✅ Command: FAULTY NODE #{node_id} - Faulty: {is_faulty}")
        
        elif cmd_type == 'set_convergence_speed':
            speed = command.get('speed', 0.05)
            simulation_controller.simulation.convergence_speed = max(0, min(1, speed))
            print(f"✅ Command: CONVERGENCE SPEED {speed}")
        
        elif cmd_type == 'toggle_sync_mode':
            mode = command.get('mode', 'espnow')
            simulation_controller.simulation.sync_mode = mode
            print(f"✅ Command: TOGGLE SYNC MODE to {mode}")
        
        elif cmd_type == 'set_node_drift':
            node_id = command.get('node_id')
            drift = command.get('drift', 0.0)
            if node_id is not None and node_id in simulation_controller.simulation.nodes:
                simulation_controller.simulation.nodes[node_id].drift_rate = drift
                print(f"✅ Command: SET NODE #{node_id} DRIFT to {drift} PPM")
        
        else:
            print(f"⚠️  Unknown command: {cmd_type}")
            
    except Exception as e:
        print(f"❌ Error handling command {cmd_type}: {e}")
        import traceback
        traceback.print_exc()


# ============================================================================
# Broadcast System
# ============================================================================

import threading
from concurrent.futures import ThreadPoolExecutor

executor = ThreadPoolExecutor(max_workers=1)
_loop = None
_loop_lock = threading.Lock()

def set_event_loop(event_loop):
    """Set the event loop for broadcasting"""
    global _loop
    with _loop_lock:
        _loop = event_loop

def get_event_loop():
    """Get the current event loop safely"""
    try:
        return asyncio.get_running_loop()
    except RuntimeError:
        # No running loop, try to get the stored one
        with _loop_lock:
            return _loop

def broadcast_state(state: dict):
    """Broadcast simulation state to all connected clients"""
    async def broadcast():
        if not connected_clients:
            return
        
        disconnected = set()
        for client in connected_clients:
            try:
                await client.send_json({
                    'type': 'state',
                    'data': state
                })
            except Exception as e:
                disconnected.add(client)
        
        # Clean up disconnected clients
        connected_clients.difference_update(disconnected)
    
    # Try to get the current event loop
    current_loop = get_event_loop()
    
    if current_loop is not None:
        try:
            # If we're in the event loop thread, schedule directly
            if threading.current_thread() == threading.main_thread():
                asyncio.create_task(broadcast())
            else:
                # If we're in a different thread, use run_coroutine_threadsafe
                asyncio.run_coroutine_threadsafe(broadcast(), current_loop)
        except Exception as e:
            print(f"⚠️  Broadcast error: {e}")
    else:
        # Fallback: run in executor
        try:
            asyncio.run(broadcast())
        except Exception as e:
            print(f"⚠️  Broadcast fallback error: {e}")


# Register the broadcast callback with simulation
def state_update_callback(state: dict):
    """Callback when simulation state updates"""
    broadcast_state(state)


simulation_controller.register_callback(state_update_callback)


# ============================================================================
# HTTP Routes
# ============================================================================

@app.get("/")
async def serve_root():
    """Serve the main HTML file"""
    html_path = os.path.join(os.path.dirname(__file__), '..', 'frontend', 'index.html')
    if os.path.exists(html_path):
        return FileResponse(html_path)
    else:
        return {"error": "Frontend not found"}


@app.get("/api/status")
async def get_status():
    """Get current simulation status"""
    state = simulation_controller.simulation.get_state()
    return {
        'running': simulation_controller.is_running,
        'sync_active': state['sync_active'],
        'num_nodes': state['num_nodes'],
        'avg_sync_error': state['avg_sync_error'],
    }


@app.post("/api/start")
async def start_simulation():
    """Start the simulation"""
    if not simulation_controller.is_running:
        simulation_controller.start()
    return {"status": "started"}


@app.post("/api/stop")
async def stop_simulation():
    """Stop the simulation"""
    simulation_controller.stop()
    return {"status": "stopped"}


@app.on_event("startup")
async def startup():
    """Initialize simulation on startup"""
    try:
        current_loop = asyncio.get_running_loop()
        set_event_loop(current_loop)
        print("🚀 Distributed Sync Dashboard starting...")
        print(f"📡 Simulating {simulation_controller.simulation.num_nodes} nodes")
        print("✅ WebSocket ready for connections")
        print(f"🔄 Event loop set: {current_loop}")
    except Exception as e:
        print(f"❌ Startup error: {e}")
        import traceback
        traceback.print_exc()


@app.on_event("shutdown")
async def shutdown():
    """Clean up on shutdown"""
    print("🛑 Shutting down...")
    simulation_controller.stop()


# ============================================================================
# Main Entry Point
# ============================================================================

if __name__ == "__main__":
    import uvicorn
    
    print("""
    +---------------------------------------------------------+
    |  Distributed Wireless Clock Synchronization Dashboard   |
    |                    FastAPI Backend                      |
    +---------------------------------------------------------+
    """)
    
    uvicorn.run(
        app,
        host="0.0.0.0",
        port=8000,
        reload=False,
        log_level="info"
    )
