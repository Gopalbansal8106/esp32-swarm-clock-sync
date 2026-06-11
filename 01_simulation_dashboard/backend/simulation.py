"""
Distributed Wireless Clock Synchronization Network Simulation Engine

This module simulates a distributed system of wireless nodes (ESP32-inspired)
that synchronize their clocks using a master-slave synchronization algorithm.
Each node experiences independent clock drift and network latency.
"""

import time
import random
import math
import sys
import os
try:
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')
except Exception:
    pass
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Tuple, Optional
from enum import Enum
import threading

# Add parent directory to path for config import
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import config


class SyncState(Enum):
    """Synchronization state of a node"""
    UNSYNCHRONIZED = "unsynchronized"
    SYNCHRONIZING = "synchronizing"
    SYNCHRONIZED = "synchronized"
    DRIFTED = "drifted"


@dataclass
class NodeStats:
    """Statistics for a single node"""
    node_id: int
    local_time: float = 0.0  # Local clock value in seconds
    drift_rate: float = 0.0  # PPM (parts per million) - how much clock drifts
    offset: float = 0.0  # Offset from reference time
    sync_state: str = "unsynchronized"
    latency_estimate: float = 0.0  # Estimated network latency
    packet_loss_rate: float = 0.0  # Packet loss percentage
    packets_sent: int = 0
    packets_received: int = 0
    sync_error: float = 0.0  # How far from master
    last_sync_timestamp: float = 0.0
    is_master: bool = False
    is_faulty: bool = False
    
    # Advanced Kalman / ESP-NOW fields
    kalman_offset: float = 0.0
    kalman_drift: float = 0.0
    quality_metric: float = 100.0
    tree_level: int = 999
    is_elected_ref: bool = False
    virtual_time: float = 0.0
    
    # Internal Kalman Filter state matrices (not serialized by asdict)
    # x = [offset, drift_rate_diff_seconds_per_second]
    kf_x_offset: float = field(default=0.0, repr=False)
    kf_x_drift: float = field(default=0.0, repr=False)
    # Covariance matrix P (2x2) stored as elements P00, P01, P10, P11
    kf_p00: float = field(default=0.01, repr=False)
    kf_p01: float = field(default=0.0, repr=False)
    kf_p10: float = field(default=0.0, repr=False)
    kf_p11: float = field(default=0.0001, repr=False)


class DistributedSyncSimulation:
    """
    Main simulation engine for distributed clock synchronization.
    
    Simulates:
    - Independent clock drift for each node
    - Network latency and jitter
    - Packet loss
    - Master-slave synchronization algorithm
    - Real-time convergence of clocks
    """
    
    def __init__(self, num_nodes: int = 5):
        """Initialize simulation with specified number of nodes"""
        # Import config to get parameters
        import sys
        import os
        sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        import config
        
        self.num_nodes = num_nodes
        self.nodes: Dict[int, NodeStats] = {}
        self.is_running = False
        self.reference_time = time.time()
        self.simulation_start_time = 0
        self.global_time = 0.0  # Elapsed simulation time
        
        # Synchronization parameters from config
        self.sync_active = False
        self.sync_start_time = 0
        self.convergence_speed = config.CONVERGENCE_SPEED  # Load from config
        self.network_jitter_enabled = True
        self.packet_loss_enabled = False
        self.packet_loss_rate = 0.0
        self.sync_mode = "espnow"  # Toggles between "basic" and "espnow"
        # Statistics tracking
        self.total_packets_sent = 0
        self.total_packets_lost = 0
        self.sync_history: List[Tuple[float, float]] = []  # Time -> avg error
        
        # Create initial nodes
        self._initialize_nodes()
        
    def _initialize_nodes(self):
        """Create and initialize all nodes with random drift rates"""
        for i in range(self.num_nodes):
            # First node is the master
            is_master = (i == 0)
            
            # Random drift rate: use config values
            drift_rate = random.uniform(config.DRIFT_RATE_MIN, config.DRIFT_RATE_MAX)
            
            # Random initial offset: -5 to +5 seconds
            initial_offset = random.uniform(config.CLOCK_OFFSET_MIN, config.CLOCK_OFFSET_MAX)
            
            node = NodeStats(
                node_id=i,
                local_time=initial_offset,
                drift_rate=drift_rate,
                offset=initial_offset,
                is_master=is_master,
                sync_state="synchronized" if is_master else "unsynchronized",
                latency_estimate=random.uniform(10, 100),  # ms
                packet_loss_rate=0.05 if not is_master else 0.01,
                virtual_time=initial_offset,
                quality_metric=100.0 if is_master else random.uniform(70, 95),
                tree_level=0 if is_master else 999,
                is_elected_ref=is_master
            )
            
            self.nodes[i] = node
    
    def add_node(self):
        """Add a new node to the simulation"""
        if len(self.nodes) >= 20:  # Max 20 nodes
            return False
        
        new_id = max(self.nodes.keys()) + 1 if self.nodes else 0
        
        drift_rate = random.uniform(config.DRIFT_RATE_MIN, config.DRIFT_RATE_MAX)
        initial_offset = random.uniform(config.CLOCK_OFFSET_MIN, config.CLOCK_OFFSET_MAX)
        
        node = NodeStats(
            node_id=new_id,
            local_time=initial_offset,
            drift_rate=drift_rate,
            offset=initial_offset,
            is_master=False,
            sync_state="unsynchronized",
            latency_estimate=random.uniform(10, 100),
            packet_loss_rate=0.05,
            virtual_time=initial_offset,
            quality_metric=random.uniform(70, 95),
            tree_level=999,
            is_elected_ref=False
        )
        
        self.nodes[new_id] = node
        return True
    
    def remove_node(self, node_id: int) -> bool:
        """Remove a node from simulation (cannot remove master)"""
        if node_id not in self.nodes:
            return False
        if self.nodes[node_id].is_master:
            return False
        
        del self.nodes[node_id]
        return True
    
    def start_synchronization(self):
        """Start the synchronization algorithm"""
        self.sync_active = True
        self.sync_start_time = self.global_time
        
        # Set all non-master nodes to synchronizing
        for node in self.nodes.values():
            if not node.is_master:
                node.sync_state = "synchronizing"
        
        print(f"🟢 SYNCHRONIZATION STARTED - {len([n for n in self.nodes.values() if not n.is_master])} slaves syncing")
    
    def stop_synchronization(self):
        """Stop the synchronization algorithm"""
        self.sync_active = False
        for node in self.nodes.values():
            if not node.is_master:
                node.sync_state = "unsynchronized"
        
        print(f"🔴 SYNCHRONIZATION STOPPED")
    
    def inject_packet_loss(self, rate: float):
        """Inject packet loss into the network (0.0 to 1.0)"""
        self.packet_loss_rate = max(0, min(1, rate))
        self.packet_loss_enabled = rate > 0
    
    def increase_drift(self, factor: float = 1.5):
        """Increase drift rate of all nodes"""
        for node in self.nodes.values():
            if not node.is_master:
                node.drift_rate *= factor
    
    def simulate_faulty_node(self, node_id: int, make_faulty: bool = True):
        """Make a node faulty with extreme drift"""
        if node_id in self.nodes:
            node = self.nodes[node_id]
            node.is_faulty = make_faulty
            if make_faulty:
                node.drift_rate = random.choice([-500, 500])  # Extreme drift
    
    def reset(self):
        """Reset simulation to initial state (does NOT stop the controller loop)"""
        self.sync_active = False
        self.global_time = 0.0
        self.total_packets_sent = 0
        self.total_packets_lost = 0
        self.sync_history.clear()
        
        # Reinitialize nodes
        self.nodes.clear()
        self._initialize_nodes()
    
    def update(self, dt: float = 0.016):  # ~60 FPS timestep
        """
        Update simulation state.
        
        dt: Time delta in seconds since last update
        """
        if not self.is_running:
            return
        
        self.global_time += dt
        
        # Update each node's local clock
        for node in self.nodes.values():
            # Clock advances based on drift rate (PPM = parts per million)
            drift_factor = 1.0 + (node.drift_rate / 1_000_000)
            
            # Add realistic oscillator jitter (±0.1%)
            jitter = random.uniform(-0.0001, 0.0001) if self.network_jitter_enabled else 0
            
            node.local_time += dt * (drift_factor + jitter)
            
            # Adjust node quality metric slowly based on uptime/faulty status
            if node.is_faulty:
                node.quality_metric = max(0.0, node.quality_metric - dt * 10)
            else:
                node.quality_metric = min(100.0, node.quality_metric + dt * 0.5)

            if self.sync_mode == "basic":
                node.virtual_time = node.local_time
                node.is_elected_ref = node.is_master
                node.tree_level = 0 if node.is_master else 1
            else:
                # Predict step for Kalman Filter (time propagation)
                if not node.is_elected_ref:
                    # Propagate offset state using estimated drift
                    node.kf_x_offset += node.kf_x_drift * dt
                    
                    # Covariance update P = F*P*F' + Q
                    p00 = node.kf_p00 + dt * (node.kf_p10 + node.kf_p01 + dt * node.kf_p11)
                    p01 = node.kf_p01 + dt * node.kf_p11
                    p10 = node.kf_p10 + dt * node.kf_p11
                    p11 = node.kf_p11
                    
                    # Add process noise spectral density
                    s_theta = 1e-6
                    s_omega = 1e-8
                    node.kf_p00 = p00 + s_theta * dt
                    node.kf_p01 = p01
                    node.kf_p10 = p10
                    node.kf_p11 = p11 + s_omega * dt

                # In ESP-NOW mode, the virtual clock is the corrected time
                node.virtual_time = node.local_time + node.kf_x_offset
                node.kalman_offset = node.kf_x_offset
                node.kalman_drift = node.kf_x_drift * 1_000_000  # Convert to PPM-like drift rate
        
        # Perform synchronization if active
        if self.sync_active:
            self._perform_synchronization_step(dt)
        else:
            # If sync stopped, reset ref nodes & states
            for node in self.nodes.values():
                node.is_elected_ref = node.is_master
                node.tree_level = 0 if node.is_master else 999
                node.sync_state = "unsynchronized"
        
        # Calculate synchronization error for all nodes
        self._calculate_sync_errors()
        
        # Update packet statistics
        self._update_packet_stats()
    
    def _perform_synchronization_step(self, dt: float):
        """Perform synchronization step based on sync_mode"""
        # 1. Dynamic Reference Election (ESP-NOW mode)
        if self.sync_mode == "espnow":
            # Reset elected status
            for node in self.nodes.values():
                node.is_elected_ref = False
                node.tree_level = 999
            
            # Elect non-faulty node with highest quality
            candidates = [n for n in self.nodes.values() if not n.is_faulty]
            if candidates:
                ref_node = max(candidates, key=lambda n: (n.quality_metric, -n.node_id))
                ref_node.is_elected_ref = True
                ref_node.tree_level = 0
                ref_node.virtual_time = ref_node.local_time
                ref_node.kf_x_offset = 0.0
                ref_node.kf_x_drift = 0.0
                ref_node.sync_state = "synchronized"
            else:
                return
        else:
            # Basic Mode: Master is fixed node 0
            ref_node = self.nodes.get(0)
            if not ref_node:
                return
            ref_node.is_elected_ref = True
            ref_node.tree_level = 0
            ref_node.sync_state = "synchronized"

        packets_this_round = 0
        
        # 2. Timing Exchange and Update
        for node in self.nodes.values():
            if node.is_elected_ref:
                continue
                
            node.tree_level = 1  # Standard star connection in simulation
            
            # Check for packet loss
            if random.random() < self.packet_loss_rate:
                self.total_packets_lost += 1
                node.sync_state = "drifted" if node.sync_state == "synchronized" else "synchronizing"
                continue
            
            self.total_packets_sent += 2  # Two-way exchange uses 2 packets
            packets_this_round += 2
            node.packets_received += 1
            
            if self.sync_mode == "basic":
                # One-way sync: master time + simple convergence
                master_time = ref_node.local_time
                network_latency = random.gauss(20, 5) / 1000
                estimated_master_time = master_time + network_latency
                offset = estimated_master_time - node.local_time
                node.local_time += offset * self.convergence_speed
                node.offset = node.local_time - master_time
                node.sync_state = "synchronized" if abs(offset) < 0.001 else "synchronizing"
            else:
                # Advanced Mode: Two-way ESP-NOW exchange & Kalman Filter Update
                # Uplink and Downlink delays
                d_u = random.gauss(15, 3) / 1000  # ESP-NOW has lower latency
                d_d = random.gauss(15, 3) / 1000
                
                # Actual offset between reference virtual time and local time
                true_offset = ref_node.virtual_time - node.local_time
                
                # Timestamps
                t1 = node.local_time
                t2 = t1 + true_offset + d_u
                t3 = t2 + 0.001  # Processing delay on reference
                t4 = t3 - true_offset + d_d
                
                # Measured offset and round-trip delay
                measured_offset = ((t2 - t1) + (t3 - t4)) / 2
                rtt = (t4 - t1) - (t3 - t2)
                node.latency_estimate = (rtt / 2) * 1000  # ms
                
                # Kalman Filter Measurement Update
                # Innovation (measurement residual)
                y = measured_offset - node.kf_x_offset
                
                # Measurement Noise Variance R (simulating wireless jitter)
                r_noise = 1e-5
                
                # Innovation covariance S
                s = node.kf_p00 + r_noise
                
                # Kalman Gain K
                k0 = node.kf_p00 / s
                k1 = node.kf_p10 / s
                
                # State Update
                node.kf_x_offset += k0 * y
                node.kf_x_drift += k1 * y
                
                # Covariance Update
                node.kf_p00 = (1.0 - k0) * node.kf_p00
                node.kf_p01 = (1.0 - k0) * node.kf_p01
                node.kf_p10 = node.kf_p10 - k1 * node.kf_p00
                node.kf_p11 = node.kf_p11 - k1 * node.kf_p01
                
                # Update offset from reference
                node.offset = ref_node.virtual_time - node.virtual_time
                node.sync_state = "synchronized" if abs(node.offset) < 0.001 else "synchronizing"
                node.last_sync_timestamp = self.global_time
        
        # Log sync activity periodically
        if int(self.global_time * 10) % 30 == 0:
            print(f"📡 SYNC MODE: {self.sync_mode.upper()} - Time: {self.global_time:.2f}s, Packets: {packets_this_round}, Total Sent: {self.total_packets_sent}")
            
    def _calculate_sync_errors(self):
        """Calculate synchronization error for each node relative to elected reference"""
        ref_node = None
        for node in self.nodes.values():
            if node.is_elected_ref:
                ref_node = node
                break
        
        if not ref_node:
            return
        
        total_error = 0
        error_count = 0
        
        for node in self.nodes.values():
            if not node.is_elected_ref:
                # Error is the absolute difference in virtual/corrected times
                error = abs(node.virtual_time - ref_node.virtual_time)
                node.sync_error = error
                total_error += error
                error_count += 1
        
        if error_count > 0:
            avg_error = total_error / error_count
            self.sync_history.append((self.global_time, avg_error))
            
            if len(self.sync_history) > 500:
                self.sync_history.pop(0)
            
            if int(self.global_time * 10) % 30 == 0:
                print(f"📊 ERROR CALC ({self.sync_mode}) - Avg Error: {avg_error*1000:.2f}ms, History: {len(self.sync_history)}")
    
    def _update_packet_stats(self):
        """Update packet statistics for each node"""
        for node in self.nodes.values():
            if not node.is_elected_ref:
                node.packets_sent = int(self.global_time * 10)
                if self.sync_mode == "basic":
                    node.latency_estimate = random.gauss(20, 5)  # ms
                # packet loss rates
                node.packet_loss_rate = self.packet_loss_rate if self.packet_loss_enabled else 0.01
    
    def get_state(self) -> Dict:
        """Get complete simulation state for frontend"""
        master = None
        for node in self.nodes.values():
            if node.is_master:
                master = node
                break
        
        avg_sync_error = 0
        if len(self.sync_history) > 0:
            avg_sync_error = self.sync_history[-1][1]
        
        return {
            'global_time': self.global_time,
            'sync_active': self.sync_active,
            'nodes': [asdict(node) for node in self.nodes.values()],
            'master_id': master.node_id if master else None,
            'total_packets_sent': self.total_packets_sent,
            'total_packets_lost': self.total_packets_lost,
            'avg_sync_error': avg_sync_error,
            'sync_history': self.sync_history[-100:],  # Last 100 points
            'num_nodes': len(self.nodes),
            'sync_mode': self.sync_mode,
        }


class SimulationController:
    """Manages simulation lifecycle and threading"""
    
    def __init__(self):
        import sys
        import os
        sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        import config
        self.simulation = DistributedSyncSimulation(num_nodes=config.INITIAL_NODES)
        self.is_running = False
        self.thread = None
        self.update_callbacks: List[callable] = []
        
    def start(self):
        """Start the simulation loop in a background thread"""
        if self.is_running:
            return
        
        self.is_running = True
        self.simulation.is_running = True
        self.thread = threading.Thread(target=self._run_loop, daemon=True)
        self.thread.start()
    
    def stop(self):
        """Stop the simulation"""
        self.is_running = False
        self.simulation.is_running = False
        if self.thread:
            self.thread.join(timeout=2)
    
    def _run_loop(self):
        """Main simulation loop"""
        print(f"🚀 Simulation loop starting - is_running: {self.is_running}")
        last_time = time.time()
        loop_count = 0
        
        while self.is_running:
            current_time = time.time()
            dt = min(current_time - last_time, 0.05)  # Cap at 50ms
            last_time = current_time
            
            self.simulation.update(dt)
            
            # Notify all callbacks
            for callback in self.update_callbacks:
                try:
                    callback(self.simulation.get_state())
                except Exception as e:
                    print(f"Callback error: {e}")
            
            # Log every 60 frames (~1 second)
            loop_count += 1
            if loop_count % 60 == 0:
                print(f"⏱️  Simulation loop running - Time: {self.simulation.global_time:.2f}s, Sync: {self.simulation.sync_active}")
            
            # Sleep to maintain ~60 FPS
            time.sleep(0.016)
        
        print(f"🛑 Simulation loop stopped")
    
    def register_callback(self, callback: callable):
        """Register a callback to be called when simulation updates"""
        self.update_callbacks.append(callback)
    
    def add_callback(self, callback: callable):
        """Alias for register_callback"""
        self.register_callback(callback)
